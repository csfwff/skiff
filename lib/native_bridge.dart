import 'package:flutter/services.dart';

/// 原生平台通信桥接层
/// 通过 MethodChannel 与各平台原生代码通信
class NativeBridge {
  static const _channel = MethodChannel('com.skiff/native');

  // 回调函数 - 由 main.dart 设置
  Function(String zone)? onOverlayTap;
  Function(String direction)? onMiddleClickGesture;
  Function(String action)? onTrayAction;

  /// 初始化原生平台，注册回调
  Future<void> initialize() async {
    _channel.setMethodCallHandler(_handleMethodCall);
    try {
      await _channel.invokeMethod('initialize');
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略，Dart 层仍可正常运行
    }
  }

  /// 处理来自原生端的方法调用
  Future<dynamic> _handleMethodCall(MethodCall call) async {
    switch (call.method) {
      case 'onOverlayTap':
        final zone = call.arguments['zone'] as String;
        onOverlayTap?.call(zone);
      case 'onMiddleClickGesture':
        final direction = call.arguments['direction'] as String;
        onMiddleClickGesture?.call(direction);
      case 'onTrayAction':
        final action = call.arguments['action'] as String;
        onTrayAction?.call(action);
    }
  }

  /// 模拟系统级滚轮滚动
  Future<void> simulateScroll(int dx, int dy) async {
    try {
      await _channel.invokeMethod('simulateScroll', {'dx': dx, 'dy': dy});
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 原子化滚轮。
  ///
  /// Linux/X11 下会短暂让悬浮窗对输入穿透，再发送 XTest 滚轮事件，
  /// 避免 Dart 层移动窗口造成闪烁。其他平台未实现时回退到普通滚轮。
  Future<void> simulateScrollDirect(int dx, int dy) async {
    try {
      await _channel.invokeMethod('simulateScrollDirect', {'dx': dx, 'dy': dy});
    } on MissingPluginException {
      await simulateScroll(dx, dy);
    }
  }

  /// 设置当前窗口是否处于悬浮按钮模式。
  ///
  /// Linux 下悬浮按钮模式会禁止窗口主动接受焦点；进入设置页时恢复。
  Future<void> setOverlayMode(bool enabled) async {
    try {
      await _channel.invokeMethod('setOverlayMode', {'enabled': enabled});
    } on MissingPluginException {
      // 非 Linux 平台暂未实现该模式切换
    }
  }

  /// 原生 X11 移动窗口（低延迟）
  Future<void> moveWindow(int x, int y) async {
    try {
      await _channel.invokeMethod('moveWindow', {'x': x, 'y': y});
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 设置悬浮按钮可见性
  Future<void> setOverlayVisible(bool visible) async {
    try {
      await _channel.invokeMethod('setOverlayVisible', {'visible': visible});
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 启用/禁用中键手势
  Future<void> setMiddleClickEnabled(bool enabled) async {
    try {
      await _channel.invokeMethod('setMiddleClickEnabled', {
        'enabled': enabled,
      });
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 显示主窗口（设置界面）
  Future<void> showMainWindow() async {
    try {
      await _channel.invokeMethod('showMainWindow');
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 隐藏主窗口
  Future<void> hideMainWindow() async {
    try {
      await _channel.invokeMethod('hideMainWindow');
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 退出应用
  Future<void> quit() async {
    try {
      await _channel.invokeMethod('quit');
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 检查 macOS 辅助功能权限
  Future<bool> checkAccessibilityPermission() async {
    try {
      final result = await _channel.invokeMethod(
        'checkAccessibilityPermission',
      );
      return result['granted'] as bool? ?? false;
    } on MissingPluginException {
      return true; // 非 macOS 平台默认返回 true
    }
  }

  /// 请求 macOS 辅助功能权限
  Future<void> requestAccessibilityPermission() async {
    try {
      await _channel.invokeMethod('requestAccessibilityPermission');
    } on MissingPluginException {
      // 非 macOS 平台无需请求辅助功能权限
    }
  }

  /// 设置开机自启
  Future<void> setAutoStart(bool enabled) async {
    try {
      await _channel.invokeMethod('setAutoStart', {'enabled': enabled});
    } on MissingPluginException {
      // 平台原生代码尚未实现时忽略
    }
  }

  /// 查询开机自启状态
  Future<bool> getAutoStart() async {
    try {
      final result = await _channel.invokeMethod('getAutoStart');
      return result['enabled'] as bool? ?? false;
    } on MissingPluginException {
      // 平台原生代码尚未实现时默认关闭
      return false;
    }
  }
}
