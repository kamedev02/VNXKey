import 'dart:io';
import 'dart:async';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:url_launcher/url_launcher_string.dart';
import 'config_service.dart';
import 'main.dart';
import 'update_service.dart';

class SettingsPage extends StatefulWidget {
  const SettingsPage({super.key});

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  VnxConfig _config = const VnxConfig();
  bool _isLoading = true;
  bool _isSaving = false;
  String _version = '';

  bool _isCheckingUpdate = false;
  bool _isDownloading = false;
  double _downloadProgress = 0.0;

  Timer? _debounce;

  @override
  void initState() {
    super.initState();
    _loadConfigAndInfo();
  }

  Future<void> _loadConfigAndInfo() async {
    final config = await ConfigService.instance.readConfig();
    PackageInfo packageInfo = await PackageInfo.fromPlatform();

    setState(() {
      _config = config;
      _version = packageInfo.version;
      _isLoading = false;
    });
  }

  Future<void> _updateConfig(VnxConfig newConfig) async {
    setState(() {
      _config = newConfig;
      _isSaving = true;
    });

    await ConfigService.instance.writeConfig(newConfig);

    if (mounted) {
      setState(() {
        _isSaving = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Đã lưu cấu hình.'),
          duration: Duration(seconds: 1),
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
  }

  void _stopService() {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text(
          'Thoát & Dừng dịch vụ',
          style: TextStyle(color: Colors.black),
        ),
        content: const Text(
          'Hành động này sẽ dừng tiến trình VNXKey đang chạy ngầm và đóng giao diện. Bạn có thể cần nhập mật khẩu sudo.\n\nTiếp tục?',
          style: TextStyle(color: Colors.black87),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Huỷ', style: TextStyle(color: Colors.black54)),
          ),
          TextButton(
            onPressed: () async {
              Navigator.pop(context);
              // Lệnh dừng service (yêu cầu polkit để hỏi pass)
              try {
                // Hiển thị loading overlay nếu cần thiết
                await Process.run('pkexec', ['systemctl', 'stop', 'vnxkey']);
                Future.delayed(const Duration(milliseconds: 500), () {
                  exit(0);
                });
              } catch (e) {
                // Ignore
                exit(0);
              }
            },
            child: const Text('Thoát', style: TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
  }

  void _restartService() {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text(
          'Khởi động lại dịch vụ',
          style: TextStyle(color: Colors.black),
        ),
        content: const Text(
          'Hành động này sẽ khởi động lại tiến trình VNXKey đang chạy ngầm. Bạn có thể cần nhập mật khẩu sudo.\n\nTiếp tục?',
          style: TextStyle(color: Colors.black87),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Huỷ', style: TextStyle(color: Colors.black54)),
          ),
          TextButton(
            onPressed: () async {
              Navigator.pop(context);
              try {
                await Process.run('pkexec', ['systemctl', 'restart', 'vnxkey']);
                if (mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('Đã khởi động lại dịch vụ!')),
                  );
                }
              } catch (e) {
                // Ignore
              }
            },
            child: const Text('Khởi động lại', style: TextStyle(color: Colors.blue)),
          ),
        ],
      ),
    );
  }

  @override
  void dispose() {
    _debounce?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_isLoading) {
      return const Scaffold(
        body: Center(child: CircularProgressIndicator(color: Colors.black)),
      );
    }

    return DefaultTabController(
      length: 3,
      child: Scaffold(
        appBar: AppBar(
          title: const Text(
            'Cài đặt VNXKey',
            style: TextStyle(fontWeight: FontWeight.bold),
          ),
          backgroundColor: Colors.white,
          foregroundColor: Colors.black,
          elevation: 0,
          actions: [
            CupertinoSwitch(
              value: _config.enabled,
              activeColor: Colors.black,
              onChanged: _isSaving
                  ? null
                  : (val) => _updateConfig(_config.copyWith(enabled: val)),
            ),
            const SizedBox(width: 16),
          ],
          bottom: const TabBar(
            labelColor: Colors.black,
            unselectedLabelColor: Colors.grey,
            indicatorColor: Colors.black,
            indicatorWeight: 2.0,
            tabs: [
              Tab(text: 'Cài đặt'),
              Tab(text: 'Khẩn cấp'),
              Tab(text: 'Thông tin'),
            ],
          ),
        ),
        body: TabBarView(
          children: [
            _buildSettingsTab(),
            _buildEmergencyTab(),
            _buildAboutTab(),
          ],
        ),
      ),
    );
  }

  Widget _buildSettingsTab() {
    return ListView(
      padding: const EdgeInsets.all(24),
      children: [
        // Bảng mã và Kiểu gõ
        Card(
          child: Column(
            children: [
              _buildDropdownRow(
                label: 'Bảng mã',
                value: _config.charset,
                items: const [
                  DropdownMenuItem(
                    value: 'unicode',
                    child: Text('Unicode (Khuyên dùng)'),
                  ),
                  DropdownMenuItem(value: 'tcvn3', child: Text('TCVN3 (ABC)')),
                  DropdownMenuItem(
                    value: 'vni_win',
                    child: Text('VNI Windows'),
                  ),
                ],
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(charset: val as String)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildDropdownRow(
                label: 'Kiểu gõ',
                value: _config.inputMethod,
                items: const [
                  DropdownMenuItem(value: 'telex', child: Text('Telex')),
                  DropdownMenuItem(value: 'vni', child: Text('VNI')),
                ],
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(inputMethod: val as String)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildDropdownRow(
                label: 'Phím tắt chuyển E/V',
                value: _config.toggleShortcut,
                items: const [
                  DropdownMenuItem(
                    value: 'Ctrl+Shift',
                    child: Text('Ctrl + Shift'),
                  ),
                  DropdownMenuItem(value: 'Alt+Z', child: Text('Alt + Z')),
                  DropdownMenuItem(
                    value: 'Ctrl+Space',
                    child: Text('Ctrl + Space'),
                  ),
                ],
                onChanged: (val) => _updateConfig(
                  _config.copyWith(toggleShortcut: val as String),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 24),

        // Cài đặt cơ bản
        Card(
          child: Column(
            children: [
              _buildSwitchRow(
                label: "Cho phép 'f j w z' làm phụ âm",
                value: _config.allowFjwz,
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(allowFjwz: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Tự động viết hoa sau "Enter . ! ?"',
                value: _config.autoCap,
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(autoCap: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Gửi phím kiểu chuẩn',
                value: _config.standardSendKey,
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(standardSendKey: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Bật kiểm tra chính tả',
                value: _config.spellcheck,
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(spellcheck: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Tắt TV khi layout bàn phím khác US',
                value: _config.disableNonUs,
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(disableNonUs: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Khởi động cùng hệ thống',
                value: _config.startup,
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(startup: val)),
              ),
            ],
          ),
        ),
        const SizedBox(height: 24),

        // Cài đặt ngoại trừ
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Danh sách ứng dụng ngoại trừ (Excluded Apps)',
                  style: TextStyle(fontWeight: FontWeight.w600, fontSize: 16),
                ),
                const SizedBox(height: 8),
                const Text(
                  'Nhập WM_CLASS của ứng dụng để tự động tắt tiếng Việt khi ứng dụng đó đang mở (cách nhau bởi dấu phẩy). VD: gnome-terminal, code, alacritty',
                  style: TextStyle(color: Colors.black54, fontSize: 13),
                ),
                const SizedBox(height: 12),
                TextFormField(
                  initialValue: _config.excludedApps.join(', '),
                  decoration: const InputDecoration(
                    border: OutlineInputBorder(),
                    hintText: 'gnome-terminal, alacritty...',
                    contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                  ),
                  onChanged: (val) {
                    if (_debounce?.isActive ?? false) _debounce!.cancel();
                    _debounce = Timer(const Duration(milliseconds: 1000), () {
                      final apps = val.split(',')
                          .map((e) => e.trim())
                          .where((e) => e.isNotEmpty)
                          .toList();
                      _updateConfig(_config.copyWith(excludedApps: apps));
                    });
                  },
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 24),
      ],
    );
  }

  Widget _buildEmergencyTab() {
    return ListView(
      padding: const EdgeInsets.all(24),
      children: [
        Card(
          color: const Color(0xFFFFF0F0),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(8),
            side: const BorderSide(color: Colors.redAccent, width: 1),
          ),
          child: Column(
            children: [
              _buildSwitchRow(
                label: 'DỪNG KHẨN CẤP (Kill Switch)',
                subtitle:
                    'Nhả toàn bộ quyền điều khiển bàn phím. Sử dụng khi gõ bị kẹt hoặc lỗi phím.',
                value: _config.emergencyStop,
                onChanged: (val) =>
                    _updateConfig(_config.copyWith(emergencyStop: val)),
                isDestructive: true,
              ),
            ],
          ),
        ),
        const SizedBox(height: 40),
        SizedBox(
          width: double.infinity,
          height: 50,
          child: OutlinedButton(
            style: OutlinedButton.styleFrom(
              foregroundColor: Colors.blue,
              side: const BorderSide(color: Colors.blue, width: 1),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(8),
              ),
            ),
            onPressed: _restartService,
            child: const Text(
              'Khởi động lại Service',
              style: TextStyle(fontWeight: FontWeight.bold, fontSize: 15),
            ),
          ),
        ),
        const SizedBox(height: 16),
        SizedBox(
          width: double.infinity,
          height: 50,
          child: OutlinedButton(
            style: OutlinedButton.styleFrom(
              foregroundColor: Colors.red,
              side: const BorderSide(color: Colors.red, width: 1),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(8),
              ),
            ),
            onPressed: _stopService,
            child: const Text(
              'Thoát & Dừng Service',
              style: TextStyle(fontWeight: FontWeight.bold, fontSize: 15),
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildAboutTab() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Container(
            width: 80,
            height: 80,
            decoration: BoxDecoration(borderRadius: BorderRadius.circular(20)),
            clipBehavior: Clip.antiAlias,
            child: Image.asset('assets/logo/128x128.png', fit: BoxFit.cover),
          ),
          const SizedBox(height: 24),
          const Text(
            'VNXKey',
            style: TextStyle(
              fontSize: 28,
              fontWeight: FontWeight.bold,
              color: Colors.black,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            'Phiên bản hiện tại: v$_version',
            style: const TextStyle(
              fontSize: 16,
              color: Colors.black54,
              fontWeight: FontWeight.w500,
            ),
          ),
          const SizedBox(height: 16),

          ValueListenableBuilder<UpdateInfo?>(
            valueListenable: globalUpdateNotifier,
            builder: (context, updateInfo, child) {
              if (updateInfo != null) {
                return Container(
                  margin: const EdgeInsets.symmetric(horizontal: 24),
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: const Color(0xFFF0FDF4),
                    border: Border.all(color: const Color(0xFF86EFAC)),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Column(
                    children: [
                      Text(
                        'Có phiên bản mới: v${updateInfo.version}',
                        style: const TextStyle(
                          fontWeight: FontWeight.bold,
                          color: Color(0xFF166534),
                          fontSize: 16,
                        ),
                      ),
                      const SizedBox(height: 8),
                      Text(
                        updateInfo.releaseNotes,
                        style: const TextStyle(
                          color: Color(0xFF15803D),
                          fontSize: 14,
                        ),
                        textAlign: TextAlign.center,
                        maxLines: 3,
                        overflow: TextOverflow.ellipsis,
                      ),
                      const SizedBox(height: 16),
                      if (_isDownloading)
                        Column(
                          children: [
                            LinearProgressIndicator(
                              value: _downloadProgress,
                              backgroundColor: Colors.white,
                              color: const Color(0xFF22C55E),
                              minHeight: 8,
                            ),
                            const SizedBox(height: 8),
                            Text(
                              'Đang tải... ${(_downloadProgress * 100).toStringAsFixed(1)}%',
                              style: const TextStyle(color: Color(0xFF166534)),
                            ),
                          ],
                        )
                      else
                        ElevatedButton.icon(
                          onPressed: () async {
                            setState(() {
                              _isDownloading = true;
                              _downloadProgress = 0;
                            });

                            bool success = await UpdateService.instance
                                .downloadAndInstallUpdate(
                                  updateInfo.downloadUrl,
                                  (received, total) {
                                    setState(() {
                                      _downloadProgress = received / total;
                                    });
                                  },
                                );

                            setState(() {
                              _isDownloading = false;
                            });

                            if (success) {
                              showDialog(
                                context: context,
                                barrierDismissible: false,
                                builder: (context) => AlertDialog(
                                  title: const Text('Cập nhật thành công'),
                                  content: const Text(
                                    'VNXKey đã được cài đặt phiên bản mới. Ứng dụng sẽ thoát để bạn có thể khởi động lại.',
                                  ),
                                  actions: [
                                    TextButton(
                                      onPressed: () => exit(0),
                                      child: const Text('OK'),
                                    ),
                                  ],
                                ),
                              );
                            } else {
                              ScaffoldMessenger.of(context).showSnackBar(
                                const SnackBar(
                                  content: Text(
                                    'Cập nhật thất bại. Vui lòng thử lại.',
                                  ),
                                ),
                              );
                            }
                          },
                          icon: const Icon(Icons.cloud_download),
                          label: const Text('Cập nhật ngay'),
                          style: ElevatedButton.styleFrom(
                            backgroundColor: const Color(0xFF22C55E),
                            foregroundColor: Colors.white,
                            elevation: 0,
                          ),
                        ),
                    ],
                  ),
                );
              }

              return OutlinedButton.icon(
                onPressed: _isCheckingUpdate
                    ? null
                    : () async {
                        setState(() => _isCheckingUpdate = true);
                        final update = await UpdateService.instance
                            .checkUpdate();
                        setState(() => _isCheckingUpdate = false);

                        if (update != null) {
                          globalUpdateNotifier.value = update;
                        } else {
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(
                              content: Text(
                                'Bạn đang sử dụng phiên bản mới nhất!',
                              ),
                            ),
                          );
                        }
                      },
                icon: _isCheckingUpdate
                    ? const SizedBox(
                        width: 16,
                        height: 16,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: Colors.black,
                        ),
                      )
                    : const Icon(Icons.update, color: Colors.black),
                label: Text(
                  _isCheckingUpdate ? 'Đang kiểm tra...' : 'Kiểm tra cập nhật',
                ),
                style: OutlinedButton.styleFrom(foregroundColor: Colors.black),
              );
            },
          ),
          const SizedBox(height: 32),
          const Text(
            'Vietnamese Input Method Manager\nPhát triển dựa trên Linux Evdev/Uinput và Unikey Core.',
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 14, color: Colors.black87, height: 1.5),
          ),
          const SizedBox(height: 32),
          const Text(
            'Tác giả lõi Unikey: Phạm Kim Long\n'
            'Phát triển UI & Linux Daemon: KameDev',
            textAlign: TextAlign.center,
            style: TextStyle(
              fontSize: 14,
              color: Colors.black87,
              fontWeight: FontWeight.w500,
            ),
          ),
          const SizedBox(height: 32),
          TextButton(
            onPressed: () {
              launchUrl(
                Uri.parse('https://github.com/kamedev02/vnxkey'),
                mode: LaunchMode.externalApplication,
              );
            },
            child: Text(
              'Github: kamedev02',
              style: TextStyle(color: Colors.black54),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDropdownRow({
    required String label,
    required String value,
    required List<DropdownMenuItem<String>> items,
    required ValueChanged<String?> onChanged,
  }) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: const TextStyle(fontSize: 15)),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            decoration: BoxDecoration(
              color: Colors.black.withAlpha(10),
              borderRadius: BorderRadius.circular(8),
            ),
            child: DropdownButtonHideUnderline(
              child: DropdownButton<String>(
                value: value,
                items: items,
                onChanged: _isSaving ? null : onChanged,
                style: const TextStyle(
                  color: Colors.black,
                  fontSize: 14,
                  fontWeight: FontWeight.w500,
                ),
                dropdownColor: Colors.white,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSwitchRow({
    required String label,
    required bool value,
    required ValueChanged<bool> onChanged,
    String? subtitle,
    bool isDestructive = false,
  }) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(
                    fontSize: 15,
                    color: isDestructive ? Colors.red : Colors.black,
                    fontWeight: isDestructive
                        ? FontWeight.w600
                        : FontWeight.normal,
                  ),
                ),
                if (subtitle != null) ...[
                  const SizedBox(height: 4),
                  Text(
                    subtitle,
                    style: const TextStyle(fontSize: 12, color: Colors.black54),
                  ),
                ],
              ],
            ),
          ),
          CupertinoSwitch(
            value: value,
            activeColor: isDestructive ? Colors.redAccent : Colors.black,
            onChanged: _isSaving ? null : onChanged,
          ),
        ],
      ),
    );
  }
}
