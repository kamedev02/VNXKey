import 'dart:io';

class WindowPoller {
  static const String _script = '''
if [ "\$WAYLAND_DISPLAY" ]; then
  if command -v hyprctl &> /dev/null; then
    hyprctl activewindow -j | grep -oP '(?<="class": ")[^"]+'
    exit 0
  fi
  if command -v qdbus &> /dev/null; then
    qdbus org.kde.KWin /KWin org.kde.KWin.activeWindow | grep -oP '(?<=class: )[^"]+' || echo ""
    exit 0
  fi
  if command -v gdbus &> /dev/null; then
    # [WORKING][CRITICAL] GNOME Window Calls Extension fallback - DO NOT MODIFY UNLESS NECESSARY
    result=\$(gdbus call --session --dest org.gnome.Shell --object-path /org/gnome/Shell/Extensions/Windows --method org.gnome.Shell.Extensions.Windows.GetActiveWindow 2>/dev/null | grep -oP '(?<="wm_class": ")[^"]+')
    if [ -n "\$result" ]; then
      echo "\$result"
      exit 0
    fi
    # [WORKING][CRITICAL] GNOME Window Calls Extended fallback - DO NOT MODIFY UNLESS NECESSARY
    result=\$(gdbus call --session --dest org.gnome.Shell --object-path /org/gnome/Shell/Extensions/WindowsExt --method org.gnome.Shell.Extensions.WindowsExt.FocusWindow 2>/dev/null | grep -oP '(?<="wm_class": ")[^"]+')
    if [ -n "\$result" ]; then
      echo "\$result"
      exit 0
    fi
    echo ""
  fi
else
  if command -v xprop &> /dev/null; then
    id=\$(xprop -root -f _NET_ACTIVE_WINDOW 0x " \\\$0\\n" _NET_ACTIVE_WINDOW 2>/dev/null | awk '{print \$2}')
    if [ "\$id" != "0x0" ] && [ -n "\$id" ]; then
      xprop -id "\$id" WM_CLASS 2>/dev/null | grep -oP '(?<=")[^"]+(?=")' | tail -1
    fi
  fi
fi
''';

  static Future<String> getActiveWindowClass() async {
    try {
      final result = await Process.run('bash', ['-c', _script]);
      return result.stdout.toString().trim();
    } catch (e) {
      return '';
    }
  }
}
