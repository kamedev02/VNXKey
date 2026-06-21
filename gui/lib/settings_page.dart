import 'dart:io';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'config_service.dart';

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
        title: const Text('Thoát & Dừng dịch vụ', style: TextStyle(color: Colors.black)),
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
                Process.run('pkexec', ['systemctl', 'stop', 'vnxkey']);
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
          title: const Text('Cài đặt VNXKey', style: TextStyle(fontWeight: FontWeight.bold)),
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
                  DropdownMenuItem(value: 'unicode', child: Text('Unicode (Khuyên dùng)')),
                  DropdownMenuItem(value: 'tcvn3', child: Text('TCVN3 (ABC)')),
                  DropdownMenuItem(value: 'vni_win', child: Text('VNI Windows')),
                ],
                onChanged: (val) => _updateConfig(_config.copyWith(charset: val as String)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildDropdownRow(
                label: 'Kiểu gõ',
                value: _config.inputMethod,
                items: const [
                  DropdownMenuItem(value: 'telex', child: Text('Telex')),
                  DropdownMenuItem(value: 'vni', child: Text('VNI')),
                ],
                onChanged: (val) => _updateConfig(_config.copyWith(inputMethod: val as String)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildDropdownRow(
                label: 'Phím tắt chuyển E/V',
                value: _config.toggleShortcut,
                items: const [
                  DropdownMenuItem(value: 'Ctrl+Shift', child: Text('Ctrl + Shift')),
                  DropdownMenuItem(value: 'Alt+Z', child: Text('Alt + Z')),
                  DropdownMenuItem(value: 'Ctrl+Space', child: Text('Ctrl + Space')),
                ],
                onChanged: (val) => _updateConfig(_config.copyWith(toggleShortcut: val as String)),
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
                onChanged: (val) => _updateConfig(_config.copyWith(allowFjwz: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Tự động viết hoa sau "Enter . ! ?"',
                value: _config.autoCap,
                onChanged: (val) => _updateConfig(_config.copyWith(autoCap: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Gửi phím kiểu chuẩn',
                value: _config.standardSendKey,
                onChanged: (val) => _updateConfig(_config.copyWith(standardSendKey: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Bật kiểm tra chính tả',
                value: _config.spellcheck,
                onChanged: (val) => _updateConfig(_config.copyWith(spellcheck: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Tắt TV khi layout bàn phím khác US',
                value: _config.disableNonUs,
                onChanged: (val) => _updateConfig(_config.copyWith(disableNonUs: val)),
              ),
              const Divider(height: 1, color: Color(0xFFE2E8F0)),
              _buildSwitchRow(
                label: 'Khởi động cùng hệ thống',
                value: _config.startup,
                onChanged: (val) => _updateConfig(_config.copyWith(startup: val)),
              ),
            ],
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
                subtitle: 'Tạm thời nhả toàn bộ bàn phím để khắc phục lỗi kẹt phím hoặc lặp phím',
                value: _config.emergencyStop,
                onChanged: (val) => _updateConfig(_config.copyWith(emergencyStop: val)),
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
              foregroundColor: Colors.red,
              side: const BorderSide(color: Colors.red, width: 1),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
            ),
            onPressed: _stopService,
            child: const Text('Thoát & Dừng Service', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 15)),
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
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(20),
            ),
            clipBehavior: Clip.antiAlias,
            child: Image.asset('assets/logo/128x128.png', fit: BoxFit.cover),
          ),
          const SizedBox(height: 24),
          const Text(
            'VNXKey',
            style: TextStyle(fontSize: 28, fontWeight: FontWeight.bold, color: Colors.black),
          ),
          const SizedBox(height: 8),
          Text(
            'Phiên bản: v$_version',
            style: const TextStyle(fontSize: 16, color: Colors.black54, fontWeight: FontWeight.w500),
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
            style: TextStyle(fontSize: 14, color: Colors.black87, fontWeight: FontWeight.w500),
          ),
          const SizedBox(height: 32),
          TextButton.icon(
            onPressed: () {},
            icon: const Icon(CupertinoIcons.scribble, color: Colors.black54),
            label: const Text('Github: kamedev02', style: TextStyle(color: Colors.black54)),
          )
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
                style: const TextStyle(color: Colors.black, fontSize: 14, fontWeight: FontWeight.w500),
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
                    fontWeight: isDestructive ? FontWeight.w600 : FontWeight.normal,
                  ),
                ),
                if (subtitle != null) ...[
                  const SizedBox(height: 4),
                  Text(
                    subtitle,
                    style: const TextStyle(fontSize: 12, color: Colors.black54),
                  ),
                ]
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
