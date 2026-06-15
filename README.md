# 轻舟 / Skiff

鼠标滚轮坏了？用轻舟代替。

轻舟是一个~~跨平台~~Linux桌面工具，当鼠标滚轮失灵时，提供两种替代方式来滚动页面：

1. **悬浮按钮** — 屏幕上常驻一个半透明的滚动条，点击上下箭头即可滚动
2. **中键手势** — 按住鼠标中键，移动鼠标后松开，触发一次滚动

> 📖 **[使用教程](docs/TUTORIAL.md)** — 安装、基本使用、设置、常见问题

## 功能一览

| 功能 | 说明 |
|------|------|
| 悬浮滚动按钮 | 50×110px 三区域：▲向上滚动 / ≡拖拽手柄 / ▼向下滚动 |
| 中键手势滚动 | 按住中键 → 移动 >20px → 松开 → 滚动（支持上下左右） |
| 中键拖动反向 | 可反转中键手势滚动方向 |
| 右键长按召回 | 右键按住 2 秒，将悬浮窗移动到鼠标当前位置 |
| 系统托盘 | 右键菜单管理所有设置（见下方） |
| 滚动行数 | 可调节每次滚动 1-10 行（默认 3 行） |
| 开机自启 | 登录系统时自动启动（Linux: ~/.config/autostart/） |
| 设置持久化 | 按钮位置、可见性、手势开关、反向拖动、滚动行数、开机自启自动保存 |
| ~~跨平台~~ bug改不动，不跨平台了 | 仅支持Linux x11 ！~~支持 Windows、macOS、Linux~~ |

## 托盘菜单

右键点击系统托盘图标，菜单如下：

```
┌─────────────────────────┐
│  显示/隐藏按钮           │
│  ☑ 启用中键手势          │
│  ☐ 中键拖动反向          │
│─────────────────────────│
│  滚动行数 →  ┌────────┐ │
│              │ ☑ 3    │ │
│              │   1    │ │
│              │   2    │ │
│              │  ...   │ │
│              │   10   │ │
│              └────────┘ │
│  ☐ 开机自启              │
│─────────────────────────│
│  退出                    │
└─────────────────────────┘
```

## 下载

| 平台 | 文件 | 大小 |
|------|------|------|
| Linux x64 | `skiff-linux-x64.tar.gz` | ~9MB |

## 快速开始

### Linux（直接运行）

```bash
# 下载解压
tar xzf skiff-linux-x64.tar.gz
cd bundle

# 后台运行（推荐，不占终端）
./skiff.sh

# 或直接运行
./skiff
```

### Linux（从源码构建）

```bash
sudo apt install cmake ninja-build libgtk-3-dev libxtst-dev libxi-dev clang
git clone <repo-url> skiff && cd skiff
flutter pub get
flutter build linux --release
build/linux/x64/release/bundle/skiff
```

### macOS(需要自己修bug)

```bash
# 安装依赖（需要 Xcode）
flutter pub get
flutter run -d macos
flutter build macos --release

# 注意：需要在系统设置中授予辅助功能权限
# 系统设置 → 隐私与安全性 → 辅助功能 → 添加 Skiff
```

### Windows(需要自己修bug)

```bash
# 需要 Visual Studio + C++ 桌面开发工作负载
flutter pub get
flutter run -d windows
flutter build windows --release
```

## 架构设计

```
┌─────────────────────────────────────────────────┐
│                   Dart 层                        │
│                                                  │
│  main.dart ─── 窗口管理、状态、托盘动作处理      │
│  native_bridge.dart ─── MethodChannel 桥接       │
│  scroll_button_overlay.dart ─── 悬浮按钮 UI      │
│  settings.dart ─── JSON 持久化                   │
│                                                  │
├──────────── MethodChannel("com.skiff/native") ───┤
│                                                  │
│               原生层（每平台独立实现）             │
│                                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │  Linux   │  │  macOS   │  │ Windows  │       │
│  │ GTK/X11  │  │ Cocoa/CG │  │ Win32    │       │
│  └──────────┘  └──────────┘  └──────────┘       │
│                                                  │
│  每个平台实现：                                   │
│  • 滚轮模拟（XTest / CGEvent / SendInput）       │
│  • 鼠标钩子（轮询 / CGEventTap / WH_MOUSE_LL）  │
│  • 系统托盘（GtkStatusIcon / NSStatusItem / Shell_NotifyIcon）│
└─────────────────────────────────────────────────┘
```

### 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| 入口 | `lib/main.dart` | 窗口初始化、状态管理、托盘动作处理 |
| 桥接 | `lib/native_bridge.dart` | Dart ↔ 原生通信，统一接口 |
| 悬浮按钮 | `lib/scroll_button_overlay.dart` | 三区域 UI、原生拖拽、点击滚动 |
| 设置 | `lib/settings.dart` | JSON 读写、跨平台路径 |

### 原生模块（以 Linux 为例）

| 模块 | 文件 | 技术 |
|------|------|------|
| 插件注册 | `skiff_platform.cc` | GObject + FlMethodChannel |
| 滚轮模拟 | `scroll_simulator.cc` | XTestFakeButtonEvent + Input Shape 穿透 |
| 鼠标钩子 | `mouse_hook.cc` | XQueryPointer 轮询（10ms） |
| 系统托盘 | `tray_manager.cc` | GtkStatusIcon + 子菜单 |

## 技术要点

### 1. 悬浮窗焦点问题

**问题**：点击悬浮按钮时，GTK 自动将焦点转移到悬浮窗，导致模拟的滚轮事件发给悬浮窗自身。

**解决方案**：使用 X11 Input Shape Passthrough — 临时将悬浮窗的输入区域设为空，让鼠标事件穿透到下层窗口：

```c
// 设置空输入区域（事件穿透）
cairo_region_t* empty = cairo_region_create();
gdk_window_input_shape_combine_region(gdk_win, empty, 0, 0);
cairo_region_destroy(empty);

// 发送滚轮事件（到达下层窗口）
XTestFakeButtonEvent(display, button, True, CurrentTime);

// 恢复输入区域
gdk_window_input_shape_combine_region(gdk_win, NULL, 0, 0);
```

详见 [docs/OVERLAY_CLICK_ISSUE.md](docs/OVERLAY_CLICK_ISSUE.md)

### 2. 中键手势检测

**问题**：XInput2 和 X Record 扩展在 GNOME 下无法检测中键事件（被桌面环境拦截）。

**解决方案**：XQueryPointer 轮询，每 10ms 检查 `Button2Mask`：

```c
while (running) {
    XQueryPointer(display, root, ...&mask);
    bool middle_pressed = (mask & Button2Mask) != 0;
    // 状态机：IDLE → PRESSED → 检测移动距离 → 触发滚动
    g_usleep(10 * 1000);  // 10ms
}
```

- CPU 开销：~0.1%
- 检测延迟：~10ms
- 手势阈值：20px

### 3. 原生窗口拖拽

**问题**：`windowManager.setPosition()` 通过 MethodChannel 通信，每次 15-30ms 延迟，拖拽严重卡顿。

**解决方案**：使用 `windowManager.startDragging()` 委托给 GTK 原生窗口拖拽，零延迟。

### 4. 单 MethodChannel 架构

所有平台共享同一个 `"com.skiff/native"` 通道，方法名统一：

| 方法 | 方向 | 说明 |
|------|------|------|
| `initialize` | Dart→Native | 初始化托盘和钩子 |
| `simulateScroll` | Dart→Native | 发送滚轮事件 |
| `setMiddleClickEnabled` | Dart→Native | 启停中键手势 |
| `setMiddleDragReversed` | Dart→Native | 同步中键拖动反向状态 |
| `setScrollLines` | Dart→Native | 同步托盘滚动行数状态 |
| `setOverlayVisible` | Dart→Native | 显示/隐藏悬浮窗 |
| `setAutoStart` | Dart→Native | 设置开机自启 |
| `quit` | Dart→Native | 退出应用 |
| `onMiddleClickGesture` | Native→Dart | 中键手势回调 |
| `onRightButtonHold` | Native→Dart | 右键长按召回悬浮窗 |
| `onTrayAction` | Native→Dart | 托盘菜单回调 |

Dart 层通过 `MissingPluginException` 捕获实现优雅降级。

## 依赖

### Dart

| 包 | 版本 | 用途 |
|----|------|------|
| `window_manager` | ^0.5.0 | 无边框窗口、置顶、拖拽 |
| `screen_retriever` | ^0.2.0 | 获取屏幕尺寸 |

### 系统库

| 平台 | 库 | 用途 |
|------|-----|------|
| Linux | `libgtk-3-dev` | 窗口管理、托盘 |
| Linux | `libxtst-dev` | XTest 滚轮模拟 |
| Linux | `libxi-dev` | X11 输入扩展 |
| macOS | Cocoa + CoreGraphics | CGEventTap、CGEvent |
| Windows | Win32 API | SendInput、WH_MOUSE_LL |

## 已知限制

- **Wayland 不支持**：所有 X11 API（XTest、XQueryPointer、Input Shape）在 Wayland 下不可用
- **GtkStatusIcon 已废弃**：GTK 3.14 起废弃，但仍广泛支持
- **macOS/Windows 未测试**：代码已实现但未经实际验证
- **仅支持 X11**：GNOME、KDE、XFCE 等使用 X11 的桌面环境

## 项目结构

```
skiff/
├── lib/
│   ├── main.dart                    # 入口、窗口管理、托盘动作处理
│   ├── native_bridge.dart           # MethodChannel 桥接
│   ├── scroll_button_overlay.dart   # 悬浮按钮组件
│   └── settings.dart                # 设置持久化
├── linux/runner/
│   ├── skiff_platform.cc/.h         # 插件注册、MethodChannel 处理
│   ├── scroll_simulator.cc/.h       # XTest 滚轮模拟 + Input Shape
│   ├── mouse_hook.cc/.h             # XQueryPointer 中键检测
│   ├── tray_manager.cc/.h           # GtkStatusIcon 托盘 + 子菜单
│   ├── my_application.cc/.h         # GTK 应用壳
│   └── main.cc                      # 入口
├── macos/Runner/
│   ├── SkiffPlatform.swift          # 插件注册
│   ├── ScrollSimulator.swift        # CGEvent 滚轮
│   ├── MouseHook.swift              # CGEventTap 中键
│   ├── TrayManager.swift            # NSStatusItem 托盘
│   └── AccessibilityHelper.swift    # 辅助功能权限
├── windows/runner/
│   ├── skiff_platform.cpp/.h        # 插件注册
│   ├── scroll_simulator.cpp/.h      # SendInput 滚轮
│   ├── mouse_hook.cpp/.h            # WH_MOUSE_LL 中键
│   └── tray_manager.cpp/.h          # Shell_NotifyIcon 托盘
├── assets/
│   └── tray_icon.png                # 托盘图标
├── docs/
│   ├── KNOWN_ISSUES.md              # 已知问题
│   └── OVERLAY_CLICK_ISSUE.md       # 悬浮窗焦点问题分析
└── pubspec.yaml
```

## 许可证

MIT
