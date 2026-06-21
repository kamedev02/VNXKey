/// config_service.dart
/// Service quản lý đọc/ghi config JSON cho VNXKey daemon
library;

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:flutter/foundation.dart';


/// Model cho cấu hình VNXKey
class VnxConfig {
  final bool enabled;
  final String inputMethod; // "telex" | "vni"
  final String charset; // "unicode" | "tcvn3" | "vni_win"
  final String toggleShortcut; // "Ctrl+Shift", "Alt+Z", etc.
  final bool emergencyStop;

  // Cài đặt cơ bản
  final bool allowFjwz;
  final bool autoCap;
  final bool standardSendKey;
  final bool spellcheck;
  final bool disableNonUs;

  // Hệ thống
  final bool startup;
  final List<String> excludedApps;

  const VnxConfig({
    this.enabled = true,
    this.inputMethod = 'telex',
    this.charset = 'unicode',
    this.toggleShortcut = 'Ctrl+Shift',
    this.emergencyStop = false,
    
    this.allowFjwz = true,
    this.autoCap = false,
    this.standardSendKey = true,
    this.spellcheck = false,
    this.disableNonUs = false,

    this.startup = false,
    this.excludedApps = const [],
  });

  VnxConfig copyWith({
    bool? enabled,
    String? inputMethod,
    String? charset,
    String? toggleShortcut,
    bool? emergencyStop,
    bool? allowFjwz,
    bool? autoCap,
    bool? standardSendKey,
    bool? spellcheck,
    bool? disableNonUs,
    bool? startup,
    List<String>? excludedApps,
  }) {
    return VnxConfig(
      enabled: enabled ?? this.enabled,
      inputMethod: inputMethod ?? this.inputMethod,
      charset: charset ?? this.charset,
      toggleShortcut: toggleShortcut ?? this.toggleShortcut,
      emergencyStop: emergencyStop ?? this.emergencyStop,
      allowFjwz: allowFjwz ?? this.allowFjwz,
      autoCap: autoCap ?? this.autoCap,
      standardSendKey: standardSendKey ?? this.standardSendKey,
      spellcheck: spellcheck ?? this.spellcheck,
      disableNonUs: disableNonUs ?? this.disableNonUs,
      startup: startup ?? this.startup,
      excludedApps: excludedApps ?? this.excludedApps,
    );
  }

  Map<String, dynamic> toJson() => {
    'enabled': enabled,
    'input_method': inputMethod,
    'charset': charset,
    'toggle_shortcut': toggleShortcut,
    'emergency_stop': emergencyStop,
    'allow_fjwz': allowFjwz,
    'auto_cap': autoCap,
    'standard_send_key': standardSendKey,
    'spellcheck': spellcheck,
    'disable_non_us': disableNonUs,
    'startup': startup,
    'excluded_apps': excludedApps,
  };

  factory VnxConfig.fromJson(Map<String, dynamic> json) {
    return VnxConfig(
      enabled: json['enabled'] as bool? ?? true,
      inputMethod: json['input_method'] as String? ?? 'telex',
      charset: json['charset'] as String? ?? 'unicode',
      toggleShortcut: json['toggle_shortcut'] as String? ?? 'Ctrl+Shift',
      emergencyStop: json['emergency_stop'] as bool? ?? false,
      allowFjwz: json['allow_fjwz'] as bool? ?? true,
      autoCap: json['auto_cap'] as bool? ?? false,
      standardSendKey: json['standard_send_key'] as bool? ?? true,
      spellcheck: json['spellcheck'] as bool? ?? false,
      disableNonUs: json['disable_non_us'] as bool? ?? false,
      startup: json['startup'] as bool? ?? false,
      excludedApps: (json['excluded_apps'] as List?)?.map((e) => e.toString()).toList() ?? [],
    );
  }
}

/// Service đọc/ghi config file
class ConfigService {
  static const String _configFileName = 'config.json';
  static const String _configDirName = 'vnxkey';

  static ConfigService? _instance;
  static ConfigService get instance => _instance ??= ConfigService._();
  ConfigService._();

  String? _configPath;

  /// Lấy đường dẫn config file
  Future<String> get configPath async {
    if (_configPath != null) return _configPath!;

    // Sử dụng /var/tmp/vnxkey để daemon (root) và GUI (user) cùng truy cập
    final configDir = '/var/tmp/$_configDirName';

    final dir = Directory(configDir);
    if (!await dir.exists()) {
      await dir.create(recursive: true);
    }

    _configPath = '$configDir/$_configFileName';
    return _configPath!;
  }

  /// Đọc config từ file
  Future<VnxConfig> readConfig() async {
    try {
      final path = await configPath;
      final file = File(path);

      if (!await file.exists()) {
        // File chưa tồn tại -> tạo với default values
        final defaultConfig = const VnxConfig();
        await writeConfig(defaultConfig);
        return defaultConfig;
      }

      final content = await file.readAsString();
      if (_lastContent == content && !_forceRead) {
        return _lastConfig!;
      }
      _lastContent = content;
      _forceRead = false;
      final json = jsonDecode(content) as Map<String, dynamic>;
      _lastConfig = VnxConfig.fromJson(json);
      return _lastConfig!;
    } catch (e) {
      // Lỗi đọc file -> trả về default
      return const VnxConfig();
    }
  }

  /// Ghi config ra file
  Future<void> writeConfig(VnxConfig config) async {
    try {
      final path = await configPath;
      final file = File(path);

      final json = const JsonEncoder.withIndent('  ').convert(config.toJson());
      _lastContent = json; // Avoid self-trigger
      _lastConfig = config;
      await file.writeAsString(json);
    } catch (e) {
      debugPrint('[ConfigService] Error writing config: $e');
    }
  }

  String? _lastContent;
  VnxConfig? _lastConfig;
  bool _forceRead = true;

  // Lắng nghe thay đổi của file config
  final _configStreamController = StreamController<VnxConfig>.broadcast();
  Stream<VnxConfig> get configStream => _configStreamController.stream;
  Timer? _watchTimer;

  /// Bắt đầu lắng nghe thay đổi file
  void startWatching() {
    _watchTimer?.cancel();
    _lastContent = null;
    _forceRead = true;
    _watchTimer = Timer.periodic(const Duration(milliseconds: 300), (timer) async {
      try {
        final path = await configPath;
        final file = File(path);
        if (!await file.exists()) return;
        final content = await file.readAsString();
        if (content != _lastContent) {
          _lastContent = content;
          final json = jsonDecode(content) as Map<String, dynamic>;
          final config = VnxConfig.fromJson(json);
          _lastConfig = config;
          _configStreamController.add(config);
        }
      } catch (_) {}
    });
  }

  void stopWatching() {
    _watchTimer?.cancel();
    _watchTimer = null;
  }

  void debugPrint(String msg) => print(msg);
}
