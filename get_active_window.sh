#!/bin/bash
if [ "$WAYLAND_DISPLAY" ]; then
  # Try hyprctl
  if command -v hyprctl &> /dev/null; then
    hyprctl activewindow -j | grep -oP "(?<=\"class\": \")[^\"]+"
    exit 0
  fi
  # Try gnome (requires extension or specific setup, mostly fallback to empty)
  # KDE: qdbus org.kde.KWin /KWin org.kde.KWin.activeWindow
  if command -v qdbus &> /dev/null; then
    # Very rudimentary for KDE
    qdbus org.kde.KWin /KWin org.kde.KWin.activeWindow | grep -oP "(?<=class: )[^\"]+" || echo ""
    exit 0
  fi
else
  # X11
  if command -v xprop &> /dev/null; then
    id=$(xprop -root -f _NET_ACTIVE_WINDOW 0x " \\$0\\n" _NET_ACTIVE_WINDOW | awk "{print \$2}")
    if [ "$id" != "0x0" ]; then
      xprop -id $id WM_CLASS | grep -oP "(?<=\")[^\"]+(?=\")" | tail -1
      exit 0
    fi
  fi
fi

