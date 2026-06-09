import 'dart:async';
import 'dart:math' as math;
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:screen_retriever/screen_retriever.dart';
import 'package:window_manager/window_manager.dart';

import 'native_bridge.dart';
import 'scroll_button_overlay.dart';
import 'settings.dart';

const _overlayWindowSize = Size(50, 110);

/// 轻舟 / Skiff - 鼠标滚轮模拟器
///
/// 启动后显示悬浮滚动按钮，系统托盘管理。
/// 托盘菜单提供显示/隐藏、手势、滚动行数和退出操作。
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // 初始化 window_manager
  await windowManager.ensureInitialized();

  // 加载设置，决定启动后悬浮窗的初始可见性
  var settings = await SettingsService.load();

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
    await windowManager.setSize(_overlayWindowSize);
    await windowManager.setMinimumSize(_overlayWindowSize);
    await windowManager.setMaximumSize(_overlayWindowSize);
    if (settings.buttonVisible) {
      await windowManager.show();
    } else {
      await windowManager.hide();
    }
  });

  // 初始化原生桥接
  final nativeBridge = NativeBridge();
  await nativeBridge.initialize();
  await nativeBridge.setMiddleClickEnabled(settings.middleClickEnabled);
  await nativeBridge.setMiddleDragReversed(settings.middleDragReversed);
  await nativeBridge.setScrollLines(settings.scrollLines);
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
  bool _suspendOverlayPositionSaving = false;
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
    widget.nativeBridge.onRightButtonHold = (x, y) {
      unawaited(_handleRightButtonHold(x, y));
    };
    widget.nativeBridge.onTrayAction = _handleTrayAction;

    // 按当前设置恢复悬浮窗状态
    unawaited(_restoreOverlayWindowState());

    // macOS 辅助功能权限检查
    if (Platform.isMacOS) {
      _checkMacOSAccessibility();
    }
  }

  @override
  void dispose() {
    _savePosTimer?.cancel();
    windowManager.removeListener(this);
    super.dispose();
  }

  @override
  void onWindowMove() {
    if (_suspendOverlayPositionSaving) {
      return;
    }

    // 防抖：拖拽结束后 500ms 才保存位置
    _savePosTimer?.cancel();
    _savePosTimer = Timer(const Duration(milliseconds: 500), () {
      unawaited(_saveOverlayPositionIfNeeded());
    });
  }

  Rect _displayBounds(dynamic display) {
    final position = display.visiblePosition ?? Offset.zero;
    final size = display.visibleSize ?? display.size;
    return Rect.fromLTWH(position.dx, position.dy, size.width, size.height);
  }

  Future<List<Rect>> _getDisplayBounds() async {
    try {
      final displays = await screenRetriever.getAllDisplays();
      if (displays.isNotEmpty) {
        return displays.map(_displayBounds).toList();
      }
    } catch (e) {
      debugPrint('读取屏幕列表失败: $e');
    }

    try {
      return [_displayBounds(await screenRetriever.getPrimaryDisplay())];
    } catch (e) {
      debugPrint('读取主屏幕信息失败: $e');
      return const [];
    }
  }

  Rect _pickDisplayForPosition(List<Rect> displays, Offset position) {
    final windowCenter = Offset(
      position.dx + _overlayWindowSize.width / 2,
      position.dy + _overlayWindowSize.height / 2,
    );
    for (final display in displays) {
      if (display.contains(windowCenter)) {
        return display;
      }
    }

    var bestDisplay = displays.first;
    var bestDistance = double.infinity;
    for (final display in displays) {
      final center = display.center;
      final dx = windowCenter.dx - center.dx;
      final dy = windowCenter.dy - center.dy;
      final distance = dx * dx + dy * dy;
      if (distance < bestDistance) {
        bestDistance = distance;
        bestDisplay = display;
      }
    }
    return bestDisplay;
  }

  Future<Offset> _normalizeOverlayPosition(Offset position) async {
    final displays = await _getDisplayBounds();
    if (displays.isEmpty) {
      return const Offset(40, 40);
    }

    final display = _pickDisplayForPosition(displays, position);
    final minX = display.left;
    final maxX = math.max(
      display.left,
      display.right - _overlayWindowSize.width,
    );
    final minY = display.top;
    final maxY = math.max(
      display.top,
      display.bottom - _overlayWindowSize.height,
    );

    return Offset(
      position.dx.clamp(minX, maxX).toDouble(),
      position.dy.clamp(minY, maxY).toDouble(),
    );
  }

  /// 应用窗口位置设置
  Future<void> _applyWindowPosition() async {
    if (_settings.buttonX >= 0 && _settings.buttonY >= 0) {
      final normalizedPosition = await _normalizeOverlayPosition(
        Offset(_settings.buttonX, _settings.buttonY),
      );
      await windowManager.setPosition(normalizedPosition);

      if ((normalizedPosition.dx - _settings.buttonX).abs() > 0.5 ||
          (normalizedPosition.dy - _settings.buttonY).abs() > 0.5) {
        _settings = _settings.copyWith(
          buttonX: normalizedPosition.dx,
          buttonY: normalizedPosition.dy,
        );
        await SettingsService.save(_settings);
      }
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
      final x = math.max(0.0, screenWidth - _overlayWindowSize.width - 20);
      final y = math.max(0.0, screenHeight - _overlayWindowSize.height - 60);
      final position = Offset(x, y);
      await windowManager.setPosition(position);
      _settings = _settings.copyWith(buttonX: x, buttonY: y);
      await SettingsService.save(_settings);
    } catch (e) {
      debugPrint('设置默认悬浮窗位置失败: $e');
      const fallback = Offset(40, 40);
      await windowManager.setPosition(fallback);
      _settings = _settings.copyWith(
        buttonX: fallback.dx,
        buttonY: fallback.dy,
      );
      await SettingsService.save(_settings);
    }
  }

  Future<void> _saveOverlayPositionIfNeeded() async {
    if (_suspendOverlayPositionSaving) {
      return;
    }

    _suspendOverlayPositionSaving = true;
    try {
      final currentPosition = await windowManager.getPosition();
      final normalizedPosition = await _normalizeOverlayPosition(
        currentPosition,
      );

      if ((normalizedPosition.dx - currentPosition.dx).abs() > 0.5 ||
          (normalizedPosition.dy - currentPosition.dy).abs() > 0.5) {
        await windowManager.setPosition(normalizedPosition);
      }

      _settings = _settings.copyWith(
        buttonX: normalizedPosition.dx,
        buttonY: normalizedPosition.dy,
      );
      await SettingsService.save(_settings);
    } catch (e, stackTrace) {
      debugPrint('保存悬浮窗位置失败: $e\n$stackTrace');
    } finally {
      _suspendOverlayPositionSaving = false;
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
    final lines = _settings.middleDragReversed
        ? -_settings.scrollLines
        : _settings.scrollLines;
    if (direction == 'up') {
      widget.nativeBridge.simulateScroll(0, -lines);
    } else if (direction == 'down') {
      widget.nativeBridge.simulateScroll(0, lines);
    } else if (direction == 'left') {
      widget.nativeBridge.simulateScroll(-lines, 0);
    } else if (direction == 'right') {
      widget.nativeBridge.simulateScroll(lines, 0);
    }
  }

  /// 右键按住 2 秒后，将悬浮窗召回到鼠标当前位置。
  Future<void> _handleRightButtonHold(double x, double y) async {
    try {
      final targetPosition = Offset(
        x - _overlayWindowSize.width / 2,
        y - _overlayWindowSize.height / 2,
      );
      await _moveOverlayWindowTo(targetPosition);
    } catch (e, stackTrace) {
      debugPrint('右键长按移动悬浮窗失败: $e\n$stackTrace');
    }
  }

  Future<void> _restoreOverlayWindowState() async {
    try {
      if (_settings.buttonVisible) {
        await _showOverlayWindow();
      } else {
        await _hideOverlayWindow();
      }
    } catch (e, stackTrace) {
      debugPrint('恢复悬浮窗状态失败: $e\n$stackTrace');
    }
  }

  Future<void> _showOverlayWindow() async {
    _suspendOverlayPositionSaving = true;
    try {
      await widget.nativeBridge.setOverlayMode(true);
      await windowManager.setTitle('Skiff');
      await windowManager.setAlwaysOnTop(true);
      await windowManager.setSkipTaskbar(true);
      await windowManager.setMinimumSize(_overlayWindowSize);
      await windowManager.setMaximumSize(_overlayWindowSize);
      await windowManager.setSize(_overlayWindowSize);
      await _applyWindowPosition();
      await widget.nativeBridge.setOverlayVisible(true);
      await windowManager.show();
    } finally {
      _suspendOverlayPositionSaving = false;
    }
  }

  Future<void> _moveOverlayWindowTo(Offset position) async {
    _suspendOverlayPositionSaving = true;
    try {
      final normalizedPosition = await _normalizeOverlayPosition(position);
      await widget.nativeBridge.setOverlayMode(true);
      await windowManager.setTitle('Skiff');
      await windowManager.setAlwaysOnTop(true);
      await windowManager.setSkipTaskbar(true);
      await windowManager.setMinimumSize(_overlayWindowSize);
      await windowManager.setMaximumSize(_overlayWindowSize);
      await windowManager.setSize(_overlayWindowSize);
      await windowManager.setPosition(normalizedPosition);
      await widget.nativeBridge.setOverlayVisible(true);
      await windowManager.show();

      _settings = _settings.copyWith(
        buttonVisible: true,
        buttonX: normalizedPosition.dx,
        buttonY: normalizedPosition.dy,
      );
      await SettingsService.save(_settings);
    } finally {
      _suspendOverlayPositionSaving = false;
    }

    if (mounted) {
      setState(() {});
    }
  }

  Future<void> _hideOverlayWindow() async {
    await widget.nativeBridge.setOverlayVisible(false);
    await windowManager.hide();
  }

  Future<void> _updateButtonVisibility(bool visible) async {
    _settings = _settings.copyWith(buttonVisible: visible);
    await SettingsService.save(_settings);

    if (visible) {
      await _showOverlayWindow();
    } else {
      await _hideOverlayWindow();
    }

    if (mounted) {
      setState(() {});
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
    if (action == 'toggle_button') {
      await _toggleButtonVisibility();
    } else if (action == 'toggle_gesture') {
      await _toggleMiddleClick();
    } else if (action == 'toggle_middle_drag_reverse') {
      await _toggleMiddleDragReversed();
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
    await _updateButtonVisibility(!_settings.buttonVisible);
  }

  /// 切换中键手势
  Future<void> _toggleMiddleClick() async {
    final newEnabled = !_settings.middleClickEnabled;
    _settings = _settings.copyWith(middleClickEnabled: newEnabled);
    await SettingsService.save(_settings);
    await widget.nativeBridge.setMiddleClickEnabled(newEnabled);
    setState(() {});
  }

  /// 切换中键拖动反向
  Future<void> _toggleMiddleDragReversed() async {
    final reversed = !_settings.middleDragReversed;
    _settings = _settings.copyWith(middleDragReversed: reversed);
    await SettingsService.save(_settings);
    await widget.nativeBridge.setMiddleDragReversed(reversed);
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
    await widget.nativeBridge.setScrollLines(lines);
    setState(() {});
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
            'Skiff 需要辅助功能权限才能监听鼠标中键手势和右键长按召回。\n\n'
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
      home: _buildOverlayPage(),
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
}
