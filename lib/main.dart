import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:screen_retriever/screen_retriever.dart';
import 'package:window_manager/window_manager.dart';

import 'native_bridge.dart';
import 'scroll_button_overlay.dart';
import 'settings.dart';

const _overlayWindowSize = Size(50, 110);
const _settingsWindowSize = Size(420, 430);

/// 轻舟 / Skiff - 鼠标滚轮模拟器
///
/// 启动后显示悬浮滚动按钮，系统托盘管理。
/// 主窗口默认隐藏，通过托盘菜单打开设置界面。
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // 初始化 window_manager
  await windowManager.ensureInitialized();

  // 配置窗口：无边框、透明、始终在上、不在任务栏显示
  const windowOptions = WindowOptions(
    size: _overlayWindowSize,
    backgroundColor: Colors.transparent,
    titleBarStyle: TitleBarStyle.hidden,
    windowButtonVisibility: false,
    alwaysOnTop: true,
    skipTaskbar: true,
  );

  await windowManager.waitUntilReadyToShow(windowOptions, () async {
    await windowManager.setAsFrameless();
    await windowManager.setAlwaysOnTop(true);
    await windowManager.setSkipTaskbar(true);
    await windowManager.setBackgroundColor(Colors.transparent);
    await windowManager.show();
    await windowManager.setSize(_overlayWindowSize);
    await windowManager.setMinimumSize(_overlayWindowSize);
    await windowManager.setMaximumSize(_overlayWindowSize);
  });

  // 加载设置
  var settings = await SettingsService.load();

  // 初始化原生桥接
  final nativeBridge = NativeBridge();
  await nativeBridge.initialize();
  await nativeBridge.setMiddleClickEnabled(settings.middleClickEnabled);
  if (Platform.isLinux) {
    final actualAutoStart = await nativeBridge.getAutoStart();
    if (settings.autoStart && !actualAutoStart) {
      await nativeBridge.setAutoStart(true);
    } else if (!settings.autoStart && actualAutoStart) {
      settings = settings.copyWith(autoStart: true);
      await SettingsService.save(settings);
    }
  }
  await nativeBridge.setOverlayMode(true);

  runApp(SkiffApp(nativeBridge: nativeBridge, initialSettings: settings));
}

/// 主应用 Widget
class SkiffApp extends StatefulWidget {
  final NativeBridge nativeBridge;
  final SettingsData initialSettings;

  const SkiffApp({
    super.key,
    required this.nativeBridge,
    required this.initialSettings,
  });

  @override
  State<SkiffApp> createState() => _SkiffAppState();
}

class _SkiffAppState extends State<SkiffApp> with WindowListener {
  late SettingsData _settings;
  bool _showSettings = false;
  Timer? _savePosTimer;

  @override
  void initState() {
    super.initState();
    _settings = widget.initialSettings;

    // 监听窗口移动事件（用于保存拖拽位置）
    windowManager.addListener(this);

    // 注册原生回调
    widget.nativeBridge.onOverlayTap = _handleOverlayTap;
    widget.nativeBridge.onMiddleClickGesture = _handleMiddleClickGesture;
    widget.nativeBridge.onTrayAction = _handleTrayAction;

    // 设置初始窗口位置
    _applyWindowPosition();

    // macOS 辅助功能权限检查
    if (Platform.isMacOS) {
      _checkMacOSAccessibility();
    }
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    super.dispose();
  }

  @override
  void onWindowMove() {
    // 防抖：拖拽结束后 500ms 才保存位置
    _savePosTimer?.cancel();
    _savePosTimer = Timer(const Duration(milliseconds: 500), () {
      windowManager.getPosition().then((pos) {
        _settings = _settings.copyWith(buttonX: pos.dx, buttonY: pos.dy);
        SettingsService.save(_settings);
      });
    });
  }

  /// 应用窗口位置设置
  Future<void> _applyWindowPosition() async {
    if (_settings.buttonX >= 0 && _settings.buttonY >= 0) {
      await windowManager.setPosition(
        Offset(_settings.buttonX, _settings.buttonY),
      );
    } else {
      // 默认右下角
      await _setWindowToBottomRight();
    }
  }

  /// 将窗口移动到屏幕右下角
  Future<void> _setWindowToBottomRight() async {
    await Future.delayed(const Duration(milliseconds: 200));
    try {
      final display = await screenRetriever.getPrimaryDisplay();
      final screenWidth = display.visibleSize?.width ?? display.size.width;
      final screenHeight = display.visibleSize?.height ?? display.size.height;
      final x = screenWidth - 50 - 20;
      final y = screenHeight - 110 - 60;
      await windowManager.setPosition(Offset(x, y));
      _settings = _settings.copyWith(buttonX: x, buttonY: y);
      await SettingsService.save(_settings);
    } catch (e) {
      await windowManager.setPosition(const Offset(1800, 900));
    }
  }

  /// 处理悬浮按钮点击
  /// Linux/X11 下由原生层短暂穿透悬浮窗后发滚轮，避免窗口移动闪烁。
  Future<void> _handleOverlayTap(String zone) async {
    final dy = (zone == 'up') ? -_settings.scrollLines : _settings.scrollLines;
    await widget.nativeBridge.simulateScrollDirect(0, dy);
  }

  /// 处理中键手势
  void _handleMiddleClickGesture(String direction) {
    switch (direction) {
      case 'up':
        widget.nativeBridge.simulateScroll(0, -_settings.scrollLines);
      case 'down':
        widget.nativeBridge.simulateScroll(0, _settings.scrollLines);
      case 'left':
        widget.nativeBridge.simulateScroll(-_settings.scrollLines, 0);
      case 'right':
        widget.nativeBridge.simulateScroll(_settings.scrollLines, 0);
    }
  }

  /// 处理托盘菜单动作
  void _handleTrayAction(String action) {
    unawaited(_handleTrayActionAsync(action));
  }

  Future<void> _handleTrayActionAsync(String action) async {
    try {
      await _runTrayAction(action);
    } catch (e, stackTrace) {
      debugPrint('托盘动作失败: $action\n$e\n$stackTrace');
    }
  }

  Future<void> _runTrayAction(String action) async {
    if (action == 'show_settings') {
      await _showSettingsWindow();
    } else if (action == 'toggle_button') {
      await _toggleButtonVisibility();
    } else if (action == 'toggle_gesture') {
      await _toggleMiddleClick();
    } else if (action == 'toggle_autostart') {
      await _toggleAutoStart();
    } else if (action.startsWith('scroll_')) {
      final lines = int.tryParse(action.substring(7));
      if (lines != null && lines >= 1 && lines <= 10) {
        await _setScrollLines(lines);
      }
    } else if (action == 'quit') {
      await widget.nativeBridge.quit();
    }
  }

  /// 切换悬浮按钮可见性
  Future<void> _toggleButtonVisibility() async {
    final newVisible = !_settings.buttonVisible;
    _settings = _settings.copyWith(buttonVisible: newVisible);
    await SettingsService.save(_settings);
    await widget.nativeBridge.setOverlayVisible(newVisible);

    if (newVisible) {
      await windowManager.show();
    } else {
      await windowManager.hide();
    }
    setState(() {});
  }

  /// 切换中键手势
  Future<void> _toggleMiddleClick() async {
    final newEnabled = !_settings.middleClickEnabled;
    _settings = _settings.copyWith(middleClickEnabled: newEnabled);
    await SettingsService.save(_settings);
    await widget.nativeBridge.setMiddleClickEnabled(newEnabled);
    setState(() {});
  }

  /// 切换开机自启
  Future<void> _toggleAutoStart() async {
    final newAutoStart = !_settings.autoStart;
    _settings = _settings.copyWith(autoStart: newAutoStart);
    await SettingsService.save(_settings);
    await widget.nativeBridge.setAutoStart(newAutoStart);
    setState(() {});
  }

  /// 设置滚动行数
  Future<void> _setScrollLines(int lines) async {
    _settings = _settings.copyWith(scrollLines: lines);
    await SettingsService.save(_settings);
    setState(() {});
  }

  /// 显示设置窗口
  Future<void> _showSettingsWindow() async {
    if (_showSettings) return;

    await widget.nativeBridge.setOverlayMode(false);
    await windowManager.setAlwaysOnTop(false);
    await windowManager.setSkipTaskbar(false);
    await windowManager.setTitle('Skiff 设置');
    await windowManager.setMaximumSize(_settingsWindowSize);
    await windowManager.setMinimumSize(_settingsWindowSize);
    await windowManager.setSize(_settingsWindowSize);
    await windowManager.center();
    await windowManager.show();
    await windowManager.focus();

    if (mounted) {
      setState(() => _showSettings = true);
    }
  }

  /// 关闭设置窗口，回到悬浮按钮模式
  Future<void> _closeSettings() async {
    await windowManager.setAlwaysOnTop(true);
    await windowManager.setSkipTaskbar(true);
    await windowManager.setMinimumSize(_overlayWindowSize);
    await windowManager.setMaximumSize(_overlayWindowSize);
    await windowManager.setSize(_overlayWindowSize);
    await _applyWindowPosition();
    await widget.nativeBridge.setOverlayMode(true);

    if (mounted) {
      setState(() => _showSettings = false);
    }

    if (_settings.buttonVisible) {
      await windowManager.show();
    } else {
      await windowManager.hide();
    }
  }

  /// 检查 macOS 辅助功能权限
  Future<void> _checkMacOSAccessibility() async {
    final granted = await widget.nativeBridge.checkAccessibilityPermission();
    if (!granted && mounted) {
      await Future.delayed(const Duration(seconds: 1));
      if (!mounted) return;
      showDialog(
        context: context,
        builder: (ctx) => AlertDialog(
          title: const Text('需要辅助功能权限'),
          content: const Text(
            'Skiff 需要辅助功能权限才能监听鼠标中键手势。\n\n'
            '请在系统设置中授予 Skiff 辅助功能权限，'
            '然后重启应用。',
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('稍后再说'),
            ),
            FilledButton(
              onPressed: () {
                Navigator.pop(ctx);
                widget.nativeBridge.requestAccessibilityPermission();
              },
              child: const Text('前往设置'),
            ),
          ],
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Skiff',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blueGrey,
          brightness: Brightness.dark,
        ),
      ),
      home: _showSettings ? _buildSettingsPage() : _buildOverlayPage(),
    );
  }

  /// 构建悬浮按钮页面
  Widget _buildOverlayPage() {
    return Material(
      color: Colors.transparent,
      child: Center(
        child: ScrollButtonOverlay(
          onScrollUp: () => _handleOverlayTap('up'),
          onScrollDown: () => _handleOverlayTap('down'),
        ),
      ),
    );
  }

  /// 构建设置页面
  Widget _buildSettingsPage() {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Skiff 设置'),
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: _closeSettings,
          tooltip: '返回悬浮按钮',
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          // ── 悬浮按钮开关 ──
          SwitchListTile(
            title: const Text('显示悬浮按钮'),
            subtitle: const Text('在屏幕上显示滚动按钮悬浮条'),
            value: _settings.buttonVisible,
            onChanged: (v) async {
              _settings = _settings.copyWith(buttonVisible: v);
              await SettingsService.save(_settings);
              await widget.nativeBridge.setOverlayVisible(v);
              setState(() {});
            },
          ),
          const Divider(),

          // ── 中键手势开关 ──
          SwitchListTile(
            title: const Text('启用中键手势'),
            subtitle: const Text('按住鼠标中键 + 移动 > 20px → 松开触发滚动'),
            value: _settings.middleClickEnabled,
            onChanged: (v) async {
              _settings = _settings.copyWith(middleClickEnabled: v);
              await SettingsService.save(_settings);
              await widget.nativeBridge.setMiddleClickEnabled(v);
              setState(() {});
            },
          ),
          const Divider(),

          // ── 滚动行数 ──
          ListTile(
            title: const Text('每次滚动行数'),
            subtitle: Row(
              children: [
                Expanded(
                  child: Slider(
                    value: _settings.scrollLines.toDouble(),
                    min: 1,
                    max: 10,
                    divisions: 9,
                    label: '${_settings.scrollLines} 行',
                    onChanged: (v) {
                      setState(() {
                        _settings = _settings.copyWith(scrollLines: v.round());
                      });
                    },
                    onChangeEnd: (v) async {
                      await SettingsService.save(_settings);
                    },
                  ),
                ),
                SizedBox(
                  width: 40,
                  child: Text(
                    '${_settings.scrollLines}',
                    textAlign: TextAlign.center,
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                ),
              ],
            ),
          ),
          const Divider(),

          // ── 开机自启 ──
          SwitchListTile(
            title: const Text('开机自启'),
            subtitle: const Text('登录系统时自动启动 Skiff'),
            value: _settings.autoStart,
            onChanged: (v) async {
              _settings = _settings.copyWith(autoStart: v);
              await SettingsService.save(_settings);
              await widget.nativeBridge.setAutoStart(v);
              setState(() {});
            },
          ),
          const Divider(),

          // ── macOS 辅助功能权限 ──
          if (Platform.isMacOS) ...[
            ListTile(
              leading: const Icon(Icons.accessibility),
              title: const Text('检查辅助功能权限'),
              subtitle: const Text('中键手势需要辅助功能权限'),
              onTap: () => widget.nativeBridge.requestAccessibilityPermission(),
            ),
            const Divider(),
          ],

          // ── 使用说明 ──
          const ListTile(
            title: Text('使用说明'),
            subtitle: Text(
              '• 点击悬浮按钮的 ▲/▼ 区域滚动页面\n'
              '• 拖拽 ≡ 手柄可移动位置\n'
              '• 按住中键移动鼠标后松开触发滚动\n'
              '• 右键点击系统托盘图标查看更多选项',
            ),
          ),
        ],
      ),
    );
  }
}
