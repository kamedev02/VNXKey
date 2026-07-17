/// main.dart
/// VNXKey - Vietnamese Input Method Manager
/// Flutter Desktop Linux Application
library;

import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';
import 'package:system_tray/system_tray.dart';
import 'package:google_fonts/google_fonts.dart';
import 'dart:io';
import 'settings_page.dart';
import 'config_service.dart';
import 'update_service.dart';
import 'window_poller.dart';
import 'dart:async';
import 'dart:isolate';

final ValueNotifier<UpdateInfo?> globalUpdateNotifier = ValueNotifier(null);


void main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();

  // Khởi tạo window manager
  await windowManager.ensureInitialized();

  // Cấu hình cửa sổ - mặc định ẩn khỏi taskbar
  const windowOptions = WindowOptions(
    size: Size(450, 550),
    minimumSize: Size(400, 500),
    center: true,
    title: 'VNXKey',
    skipTaskbar: true,
    titleBarStyle: TitleBarStyle.normal,
  );

  bool isAutostart = args.contains('--autostart');

  windowManager.waitUntilReadyToShow(windowOptions, () async {
    await windowManager.setPreventClose(true);
    if (!isAutostart) {
      await windowManager.show();
      await windowManager.focus();
    } else {
      await windowManager.hide();
    }
  });

  runApp(const VnxKeyApp());
}

class VnxKeyApp extends StatefulWidget {
  const VnxKeyApp({super.key});

  @override
  State<VnxKeyApp> createState() => _VnxKeyAppState();
}

class _VnxKeyAppState extends State<VnxKeyApp> with WindowListener {
  final AppWindow _appWindow = AppWindow();
  final SystemTray _systemTray = SystemTray();
  final Menu _menu = Menu();
  Timer? _pollerTimer;
  VnxConfig _currentConfig = const VnxConfig();

  @override
  void initState() {
    super.initState();
    windowManager.addListener(this);
    _initSystemTray();
    
    ConfigService.instance.startWatching();
    ConfigService.instance.configStream.listen((config) {
      _currentConfig = config;
      _updateTrayIcon(config.enabled);
      _rebuildTrayMenu(config);
    });

    ConfigService.instance.readConfig().then((config) {
      _currentConfig = config;
    });

    _startActiveWindowPoller();
    _checkForUpdates();
  }

  bool _isPolling = false;

  void _startActiveWindowPoller() {
    _pollerTimer = Timer.periodic(const Duration(milliseconds: 500), (timer) async {
      if (_isPolling) return;
      _isPolling = true;
      try {
        bool isExcluded = false;
        
        if (_currentConfig.excludedApps.isNotEmpty) {
          final activeClass = await Isolate.run(() => WindowPoller.getActiveWindowClass());
          if (activeClass.isNotEmpty) {
            isExcluded = _currentConfig.excludedApps.any((app) => 
              app.toLowerCase() == activeClass.toLowerCase());
          }
        }

        final file = File('/dev/shm/vnxkey_excluded');
        final currentContent = await file.exists() ? await file.readAsString() : '';

        final newContent = isExcluded ? '1' : '0';
        if (currentContent != newContent) {
          await file.writeAsString(newContent);
        }
      } catch (e) {
        // ignore
      } finally {
        _isPolling = false;
      }
    });
  }

  void _checkForUpdates() async {
    final update = await UpdateService.instance.checkUpdate();
    if (update != null) {
      globalUpdateNotifier.value = update;
      final config = await ConfigService.instance.readConfig();
      _rebuildTrayMenu(config);
    }
  }

  @override
  void dispose() {
    _pollerTimer?.cancel();
    windowManager.removeListener(this);
    ConfigService.instance.stopWatching();
    super.dispose();
  }
  
  @override
  void onWindowClose() {
    // Override default close to hide instead
    _appWindow.hide();
  }

  Future<void> _initSystemTray() async {
    final config = await ConfigService.instance.readConfig();
    await _rebuildTrayMenu(config);

    String iconPath = config.enabled ? 'assets/icons/V_128x128.png' : 'assets/icons/E_128x128.png';

    await _systemTray.initSystemTray(
      title: "VNXKey",
      iconPath: iconPath,
    );

    await _systemTray.setContextMenu(_menu);

    _systemTray.registerSystemTrayEventHandler((eventName) {
      if (eventName == kSystemTrayEventClick) {
        _appWindow.show();
        windowManager.focus();
      }
    });
  }
  
  Future<void> _updateTrayIcon(bool enabled) async {
    String iconPath = enabled ? 'assets/icons/V_128x128.png' : 'assets/icons/E_128x128.png';
    await _systemTray.setImage(iconPath);
  }

  Future<void> _rebuildTrayMenu(VnxConfig config) async {
    final update = globalUpdateNotifier.value;
    
    List<MenuItemBase> items = [];
    
    if (update != null) {
      items.addAll([
        MenuItemLabel(
          label: '🚀 Cập nhật bản ${update.version}!',
          onClicked: (menuItem) {
            _appWindow.show();
            windowManager.focus();
          },
        ),
        MenuSeparator(),
      ]);
    }

    items.addAll([
      MenuItemLabel(
        label: config.enabled ? 'Đang bật: Tiếng Việt' : 'Đang bật: Tiếng Anh',
        onClicked: (menuItem) async {
          final newConfig = config.copyWith(enabled: !config.enabled);
          await ConfigService.instance.writeConfig(newConfig);
        },
      ),
      MenuItemLabel(
        label: 'Kiểu gõ: ${config.inputMethod == "telex" ? "Telex" : "VNI"}',
        onClicked: (menuItem) async {
          final newConfig = config.copyWith(inputMethod: config.inputMethod == "telex" ? "vni" : "telex");
          await ConfigService.instance.writeConfig(newConfig);
        },
      ),
      MenuSeparator(),
      MenuItemLabel(label: 'Cài đặt', onClicked: (menuItem) => _appWindow.show()),
      MenuItemLabel(
        label: config.emergencyStop ? 'Tiếp tục bàn phím' : 'Dừng khẩn cấp',
        onClicked: (menuItem) async {
          final newConfig = config.copyWith(emergencyStop: !config.emergencyStop);
          await ConfigService.instance.writeConfig(newConfig);
        },
      ),
      MenuSeparator(),
      MenuItemLabel(label: 'Thoát', onClicked: (menuItem) {
        try {
          Process.run('pkexec', ['systemctl', 'stop', 'vnxkey']);
        } catch (_) {}
        exit(0);
      }),
    ]);
    
    await _menu.buildFrom(items);
    await _systemTray.setContextMenu(_menu);
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'VNXKey',
      debugShowCheckedModeBanner: false,
      theme: _buildTheme(),
      home: const SettingsPage(),
    );
  }

  ThemeData _buildTheme() {
    const background = Color(0xFF0F1117);
    const surface = Color(0xFF1A1D2E);

    return ThemeData(
      useMaterial3: true,
      colorScheme: const ColorScheme.light(
        primary: Colors.black,
        onPrimary: Colors.white,
        surface: Colors.white,
        onSurface: Colors.black,
        error: Colors.redAccent,
        outline: Color(0xFFE2E8F0),
      ),
      scaffoldBackgroundColor: const Color(0xFFF8F9FA),
      cardTheme: CardThemeData(
        color: Colors.white,
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
          side: const BorderSide(color: Color(0xFFE2E8F0), width: 1),
        ),
      ),
      switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith((states) =>
          states.contains(WidgetState.selected) ? Colors.white : Colors.white),
        trackColor: WidgetStateProperty.resolveWith((states) =>
          states.contains(WidgetState.selected) ? Colors.black : const Color(0xFFE2E8F0)),
      ),
      textTheme: GoogleFonts.interTextTheme(const TextTheme(
        displaySmall: TextStyle(
          fontSize: 28,
          fontWeight: FontWeight.bold,
          color: Colors.black,
          letterSpacing: -0.5,
        ),
        titleLarge: TextStyle(
          fontSize: 18,
          fontWeight: FontWeight.w600,
          color: Colors.black,
        ),
        titleMedium: TextStyle(
          fontSize: 15,
          fontWeight: FontWeight.w500,
          color: Colors.black87,
        ),
        bodyMedium: TextStyle(
          fontSize: 14,
          color: Colors.black54,
        ),
        labelMedium: TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w500,
          color: Color(0xFF9FA8C7),
          letterSpacing: 0.5,
        ),
      )),
    );
  }
}
