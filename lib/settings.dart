import 'dart:convert';
import 'dart:io';

/// 持久化设置数据
class SettingsData {
  final bool buttonVisible;
  final bool middleClickEnabled;
  final double buttonX;
  final double buttonY;
  final int scrollLines;
  final bool autoStart;

  const SettingsData({
    this.buttonVisible = true,
    this.middleClickEnabled = true,
    this.buttonX = -1,
    this.buttonY = -1,
    this.scrollLines = 3,
    this.autoStart = false,
  });

  SettingsData copyWith({
    bool? buttonVisible,
    bool? middleClickEnabled,
    double? buttonX,
    double? buttonY,
    int? scrollLines,
    bool? autoStart,
  }) {
    return SettingsData(
      buttonVisible: buttonVisible ?? this.buttonVisible,
      middleClickEnabled: middleClickEnabled ?? this.middleClickEnabled,
      buttonX: buttonX ?? this.buttonX,
      buttonY: buttonY ?? this.buttonY,
      scrollLines: scrollLines ?? this.scrollLines,
      autoStart: autoStart ?? this.autoStart,
    );
  }

  Map<String, dynamic> toJson() => {
        'buttonVisible': buttonVisible,
        'middleClickEnabled': middleClickEnabled,
        'buttonX': buttonX,
        'buttonY': buttonY,
        'scrollLines': scrollLines,
        'autoStart': autoStart,
      };

  factory SettingsData.fromJson(Map<String, dynamic> json) => SettingsData(
        buttonVisible: json['buttonVisible'] as bool? ?? true,
        middleClickEnabled: json['middleClickEnabled'] as bool? ?? true,
        buttonX: (json['buttonX'] as num?)?.toDouble() ?? -1,
        buttonY: (json['buttonY'] as num?)?.toDouble() ?? -1,
        scrollLines: json['scrollLines'] as int? ?? 3,
        autoStart: json['autoStart'] as bool? ?? false,
      );
}

/// 设置持久化服务
/// 存储位置:
///   Windows: %APPDATA%/Skiff/settings.json
///   macOS:   ~/Library/Application Support/Skiff/settings.json
///   Linux:   ~/.config/skiff/settings.json
class SettingsService {
  static SettingsData? _cached;

  static Future<File> _file() async {
    String dir;
    if (Platform.isWindows) {
      dir = Platform.environment['APPDATA']!;
    } else if (Platform.isMacOS) {
      dir = '${Platform.environment['HOME']}/Library/Application Support';
    } else {
      dir = Platform.environment['XDG_CONFIG_HOME'] ??
          '${Platform.environment['HOME']}/.config';
    }
    final directory = Directory('$dir/Skiff');
    if (!await directory.exists()) {
      await directory.create(recursive: true);
    }
    return File('${directory.path}/settings.json');
  }

  static Future<SettingsData> load() async {
    if (_cached != null) return _cached!;
    try {
      final f = await _file();
      if (!await f.exists()) {
        _cached = const SettingsData();
        return _cached!;
      }
      final json = jsonDecode(await f.readAsString()) as Map<String, dynamic>;
      _cached = SettingsData.fromJson(json);
      return _cached!;
    } catch (_) {
      _cached = const SettingsData();
      return _cached!;
    }
  }

  static Future<void> save(SettingsData data) async {
    _cached = data;
    final f = await _file();
    await f.writeAsString(const JsonEncoder.withIndent('  ').convert(data.toJson()));
  }
}
