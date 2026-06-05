# Skiff 当前问题总结

## 项目状态

**已完成功能：**
- ✅ Flutter 桌面项目框架（Windows/macOS/Linux）
- ✅ 系统托盘（右键菜单：显示/隐藏按钮、中键手势开关、退出）
- ✅ 中键手势检测（Linux 用 XQueryPointer 轮询，每 10ms 一次）
- ✅ 滚轮模拟（Linux 用 XTestFakeButtonEvent）
- ✅ 悬浮按钮 UI（三区域：▲滚动 | ≡拖拽 | ▼滚动）
- ✅ 持久化设置（按钮位置、中键开关、滚动行数）
- ✅ macOS 辅助功能权限检测

**Linux 平台特有问题：**（以下均针对 Linux，Windows/macOS 原生实现未测试）

---

## 问题一：悬浮按钮点击抢焦点（Linux/X11 已处理，待验证）

### 现象
点击悬浮按钮的 ▲/▼ 区域后，焦点转移到悬浮窗口，导致：
- 模拟的滚轮事件发给了悬浮窗口自身，目标应用不滚动
- 目标应用窗口闪烁（焦点切换）

### 根因
`XTestFakeButtonEvent` 将事件发送到**指针所在的窗口**。点击悬浮按钮时，指针在悬浮窗上，所以滚轮事件发给了悬浮窗。

### 已尝试的方案

| 方案 | 结果 |
|------|------|
| `windowManager.hide()` → 滚轮 → `show()` | 闪烁，GTK hide 有动画延迟 |
| `windowManager.setPosition(-10000,-10000)` → 滚轮 → 移回 | 闪烁，窗口会短暂出现在左上角 |
| `TapRegion` + `Focus(canRequestFocus: false)` | 点击事件仍被悬浮窗接收，滚轮还是发到悬浮窗 |
| `XUnmapWindow` 原生隐藏 | 需要跨平台原生代码，实现复杂 |
| `X11 Input Shape` 临时穿透 → `XTestFakeButtonEvent` → 恢复 | ✅ 当前 Linux/X11 实现，无窗口闪烁 |

### 当前实现

点击 ▲/▼ 时，Dart 调用 `simulateScrollDirect()`，Linux 原生层短暂将悬浮窗 input shape 设置为空，
发出 XTest 滚轮事件后立即恢复。悬浮按钮模式下还会禁止窗口主动接受焦点，进入设置页时恢复。

### 后续验证方向

1. **多窗口管理器验证**：GNOME Shell、KDE、XFCE、i3 等 X11 环境。

2. **Wayland 专用方案**：当前方案依赖 X11 input shape 和 XTest，Wayland 下不可用。

3. **XSendEvent 备选方案**：如果某些 WM 下 input shape 穿透失效，可继续验证直接向目标窗口发送事件。

---

## 问题二：拖拽不跟手

### 现象
使用 `windowManager.setPosition()` 拖拽时，窗口移动明显滞后于鼠标。

### 根因
`windowManager.setPosition()` 通过 MethodChannel 平台通道：
```
Flutter → MethodChannel → Dart plugin → GTK XMoveWindow
```
每次调用开销约 15-30ms，指针移动事件每秒 60-120 次，导致严重积压。

### 已尝试的方案

| 方案 | 结果 |
|------|------|
| 节流 8ms (~120fps) | 仍然卡顿，调用频率高于通道处理速度 |
| 节流 32ms (~30fps) | 好一些但仍有延迟感 |
| 原生 X11 `XMoveWindow` 直接调用 | 需要找到 Flutter 窗口的 X11 Window ID，`gdk_x11_window_get_xid` 未成功 |
| `windowManager.startDragging()` | ✅ **流畅！** 使用 GTK 原生窗口拖拽，无平台通道开销 |

### 当前状态
- **拖拽已用 `startDragging()` 解决**，流畅无延迟
- 需要确认 `WindowListener.onWindowMove()` 在原生拖拽时是否正确触发（用于保存位置）

---

## 问题三：中键手势检测方式

### 现象
中键手势功能已可用，但实现方式不够理想。

### 已尝试的方案

| 方案 | 结果 |
|------|------|
| XInput2 `XI_RawButtonPress` | ❌ 检测到鼠标移动(evtype=17)但检测不到中键按下(evtype=15)，GNOME 拦截了中键 |
| X Record 扩展 | ❌ `XRecordEnableContext` 失败，可能是权限或兼容性问题 |
| XQueryPointer 轮询 | ✅ **可用**，每 10ms 检查 `Button2Mask`，能检测中键按下/松开 |

### 当前状态
- 用 XQueryPointer 轮询方式，CPU 开销约 0.1%（每秒 100 次 X11 调用）
- 检测延迟约 10ms（可接受）
- 手势阈值 20px，防抖有效

---

## 问题四：滚轮模拟的目标窗口

### 现象
`XTestFakeButtonEvent` 发送的滚轮事件总是到**指针所在的窗口**，而不是**当前激活的窗口**。

### 影响
- 当悬浮窗在指针下方时，普通 `simulateScroll` 会把滚轮发给悬浮窗（无效果）
- 当悬浮窗移走后，滚轮发给指针下方的窗口（正确）
- 当前 `simulateScrollDirect` 会在 Linux/X11 下临时让悬浮窗输入穿透，因此不需要移走窗口

### 可能的解决方向
- 已采用：X11 Input Shape 临时穿透 + XTest 滚轮
- 备选：用 `XSendEvent` + `XGetInputFocus` 直接发到焦点窗口

---

## 问题五：跨平台兼容性

### Linux
- 仅支持 X11，Wayland 下全局输入监听不可用
- `GtkStatusIcon` 已废弃但仍可用
- 需要安装 `libgtk-3-dev`, `libxtst-dev`, `libxi-dev`

### macOS
- 需要辅助功能权限（CGEventTap）
- 需要关闭 App Sandbox（entitlements）
- 代码已写但未测试

### Windows
- `SendInput` + `WH_MOUSE_LL` 方案已写但未测试
- `Shell_NotifyIcon` 托盘已写但未测试

---

## 建议的优先修复顺序

1. **验证问题一/四的新实现** — 在多个 X11 桌面环境确认 input shape 穿透稳定
2. **问题二（拖拽）** — 已有 `startDragging()` 方案，只需验证位置保存
3. **问题三（中键）** — 当前轮询方案可用，可后续优化
4. **Wayland 兼容性** — 需要独立设计，当前 X11 方案无法覆盖
