# 悬浮按钮点击滚动问题分析

## 原始需求

用户点击悬浮按钮的 ▲/▼ 区域时，**当前激活的应用窗口**应该响应滚轮滚动。

例如：用户正在看浏览器，点击悬浮按钮的 ▲，浏览器应该向上滚动。

---

## 当前实现（已修复）

**Linux/X11 下采用 X11 Input Shape 穿透方案。**

点击 ▲/▼ 后，Dart 不再把悬浮窗移到屏幕外，而是调用
`nativeBridge.simulateScrollDirect()`。Linux 原生层会：

1. 获取 Flutter 顶层 GTK 窗口的 `GdkWindow`
2. 用 `gdk_window_input_shape_combine_region()` 临时设置空 input shape
3. `gdk_display_sync()`，确保 X server 已应用穿透状态
4. 用 `XTestFakeButtonEvent` 发送滚轮事件
5. 恢复 input shape

这样悬浮窗视觉上不会移动或隐藏，但 X11 的鼠标命中测试会跳过悬浮窗，
滚轮事件会发给悬浮窗下方的目标窗口。

同时，悬浮按钮模式下会设置窗口不主动接受焦点；进入设置页时再恢复焦点能力。

---

## 为什么悬浮窗会抢焦点

Flutter 窗口（包括悬浮窗）在接收到鼠标点击时，GTK 窗口管理器会自动将焦点转移到该窗口。这是操作系统的默认行为，无法通过 Flutter 层面阻止。

**事件流：**
```
用户点击悬浮按钮
  → GTK 将焦点转移到悬浮窗
    → XTestFakeButtonEvent 发送滚轮事件
      → 事件到达指针所在的窗口（即悬浮窗）
        → 悬浮窗不处理滚轮 → 无效果
```

**关键约束：** `XTestFakeButtonEvent` 将事件发送到**指针所在的窗口**，而不是**焦点窗口**。所以即使恢复焦点，只要指针在悬浮窗上，滚轮就发给悬浮窗。

---

## 旧妥协方案（已移除）

**把悬浮窗临时移到屏幕外（-10000, -10000），发完滚轮再移回。**

```dart
Future<void> _handleOverlayTap(String zone) async {
  final dy = (zone == 'up') ? -scrollLines : scrollLines;
  final pos = await windowManager.getPosition();
  await windowManager.setPosition(Offset(-10000, -10000)); // 移走
  await Future.delayed(Duration(milliseconds: 30));
  await nativeBridge.simulateScroll(0, dy);                // 发滚轮
  await windowManager.setPosition(pos);                     // 移回
}
```

**缺点：** 点击时会看到悬浮窗瞬间消失又出现（闪烁）。当前实现已不再使用该方案。

---

## 尝试过的所有方案

### 方案 1：windowManager.hide() / show()

```dart
await windowManager.hide();
await Future.delayed(Duration(milliseconds: 30));
await nativeBridge.simulateScroll(0, dy);
await windowManager.show();
```

**结果：❌ 闪烁更严重**
GTK 的 hide/show 有窗口管理器动画，延迟比移动更大。

---

### 方案 2：TapRegion + Focus(canRequestFocus: false)

```dart
TapRegion(
  onTapOutside: (_) => FocusManager.instance.primaryFocus?.unfocus(),
  child: Focus(
    focusNode: FocusNode(canRequestFocus: false),
    child: // 悬浮按钮
  ),
)
```

**结果：❌ 无效**
TapRegion 只管理 Flutter 层面的焦点，但 GTK 窗口管理器在更底层转移焦点，Flutter 无法阻止。

---

### 方案 3：windowManager.setFocusable(false)

```dart
await windowManager.setFocusable(false);
```

**结果：❌ 方法不存在**
`window_manager` 包没有 `setFocusable` 方法。

---

### 方案 4：原生 X11 XUnmapWindow

```c
XUnmapWindow(display, xid);  // 隐藏
scroll_simulator_scroll(dx, dy);
XMapWindow(display, xid);     // 显示
```

**结果：❌ 闪烁 + 实现复杂**
需要找到 Flutter 窗口的 X11 Window ID，跨平台实现复杂，且仍有闪烁。

---

### 方案 5：原生 X11 XMoveWindow 直接调用

```c
XMoveWindow(display, xid, -10000, -10000);  // 移走
scroll_simulator_scroll(dx, dy);
XMoveWindow(display, xid, orig_x, orig_y);  // 移回
```

**结果：⚠️ 可行但未成功集成**
通过 `gdk_x11_window_get_xid()` 获取窗口 ID 未成功（GDK 窗口遍历问题）。

---

### 方案 6：XSendEvent 直接发到焦点窗口

```c
Window focused;
XGetInputFocus(display, &focused, &revert_to);
// 构造 XButtonEvent
XSendEvent(display, focused, True, ButtonPressMask, &event);
```

**结果：❌ 未尝试**
`XSendEvent` 的事件会被标记为 `send_event=True`，部分应用会忽略合成事件。

---

### 方案 7：XTestFakeMotionEvent 移动指针

```c
// 先把指针移到目标窗口中心
XTestFakeMotionEvent(display, -1, target_x, target_y, CurrentTime);
// 再发滚轮
XTestFakeButtonEvent(display, button, True, CurrentTime);
```

**结果：⚠️ 可行但指针会跳动**
用户看到指针瞬间跳到别处再跳回来，体验差。

---

### 方案 8：X11 Input Shape

```c
// 让窗口对鼠标事件透明
XRegion region = XCreateRegion();
XShapeCombineMask(display, win, ShapeInput, 0, 0, region, ShapeSet);
```

**结果：✅ 已采用**
实际实现不是永久穿透，而是在发送滚轮事件前短暂设置空 input shape，发送后立即恢复。
因此悬浮按钮仍可点击和拖拽，同时滚轮事件会穿透到目标窗口。

---

## 总结

| 方案 | 闪烁 | 复杂度 | 状态 |
|------|------|--------|------|
| hide/show | 严重 | 低 | ❌ 放弃 |
| TapRegion | 无 | 低 | ❌ 无效 |
| setFocusable | 无 | 低 | ❌ 不存在 |
| XUnmapWindow | 有 | 中 | ❌ 放弃 |
| XMoveWindow | 有 | 中 | ⚠️ 当前方案 |
| XSendEvent | 无 | 中 | ❌ 未验证 |
| 移动指针 | 有 | 中 | ❌ 体验差 |
| Input Shape | 无 | 中 | ✅ 当前实现 |

**当前选择方案 8（X11 Input Shape）的原因：**
- 不移动或隐藏悬浮窗，避免闪烁
- 仍使用 XTest 生成真实滚轮事件
- 只在滚轮发送期间短暂穿透，不影响普通点击和拖拽

**后续关注：**
需要在不同 X11 窗口管理器下继续验证。Wayland 下该方案不可用，仍需要平台专用实现。
