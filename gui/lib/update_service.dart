import 'dart:convert';
import 'dart:io';
import 'package:http/http.dart' as http;
import 'package:package_info_plus/package_info_plus.dart';
import 'package:dio/dio.dart';

class UpdateInfo {
  final String version;
  final String releaseNotes;
  final String downloadUrl;

  UpdateInfo({
    required this.version,
    required this.releaseNotes,
    required this.downloadUrl,
  });
}

class UpdateService {
  static final UpdateService instance = UpdateService._internal();
  UpdateService._internal();

  static const String _repoApiUrl = 'https://api.github.com/repos/kamedev02/VNXKey/releases/latest';

  Future<UpdateInfo?> checkUpdate() async {
    try {
      final packageInfo = await PackageInfo.fromPlatform();
      final currentVersion = packageInfo.version;

      final response = await http.get(Uri.parse(_repoApiUrl));
      if (response.statusCode == 200) {
        final data = json.decode(response.body);
        final String tagName = data['tag_name'] ?? ''; // e.g. "v26.06.008"
        final String remoteVersion = tagName.replaceAll('v', '');
        
        // Simple comparison: if versions differ, assume an update is available.
        // For semantic versioning comparison, we can split by dot.
        if (_isNewer(currentVersion, remoteVersion)) {
          final List assets = data['assets'] ?? [];
          String? downloadUrl;
          for (var asset in assets) {
            final name = asset['name'] as String;
            if (name.endsWith('_amd64.deb')) {
              downloadUrl = asset['browser_download_url'];
              break;
            }
          }

          if (downloadUrl != null) {
            return UpdateInfo(
              version: remoteVersion,
              releaseNotes: data['body'] ?? 'Cập nhật hệ thống & Cải thiện hiệu suất',
              downloadUrl: downloadUrl,
            );
          }
        }
      }
    } catch (e) {
      print('Lỗi khi kiểm tra cập nhật: $e');
    }
    return null;
  }

  bool _isNewer(String current, String remote) {
    if (current == remote) return false;
    
    List<int> cParts = current.split('.').map((s) => int.tryParse(s) ?? 0).toList();
    List<int> rParts = remote.split('.').map((s) => int.tryParse(s) ?? 0).toList();

    for (int i = 0; i < 3; i++) {
      int c = i < cParts.length ? cParts[i] : 0;
      int r = i < rParts.length ? rParts[i] : 0;
      if (r > c) return true;
      if (r < c) return false;
    }
    return false; // Equal
  }

  Future<bool> downloadAndInstallUpdate(String url, Function(int, int) onProgress) async {
    try {
      final dio = Dio();
      final savePath = '/tmp/vnxkey_update.deb';
      
      await dio.download(
        url, 
        savePath,
        onReceiveProgress: (received, total) {
          if (total != -1) {
            onProgress(received, total);
          }
        },
      );

      // Execute pkexec dpkg -i
      final process = await Process.start('pkexec', ['dpkg', '-i', savePath]);
      final exitCode = await process.exitCode;
      
      return exitCode == 0;
    } catch (e) {
      print('Lỗi khi tải hoặc cài đặt: $e');
      return false;
    }
  }
}
