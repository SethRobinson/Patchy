#!/usr/bin/env bash
# Headless GNOME Shell (mutter) rig for Wayland bug repros (docs/testing.md GNOME variant).
#   gnome-rig.sh start-shell            starts gnome-shell --headless on its own session bus, writes env to build/gnome-rig/env
#                                       (RIG_DESKTOP=<name> sets XDG_CURRENT_DESKTOP for the shell and the portals; default GNOME)
#   gnome-rig.sh patchy <scale> [args]  launches the installed Flatpak on it
#   gnome-rig.sh script <file.js>       forwards --run-script to the running instance
#   gnome-rig.sh drive <commands...>    runs gnome-input.py with the given stdin lines (use printf | ...)
#   gnome-rig.sh stop                   stops patchy and the shell by recorded pids
set -u
export XDG_RUNTIME_DIR=/run/user/$(id -u)
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
[ -n "$ROOT" ] && [ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: could not find the repository root" >&2; exit 1; }
OUT=$ROOT/build/gnome-rig
mkdir -p "$OUT"
DISPLAY_NAME=wayland-patchy-gnome
case "$1" in
  start-shell)
    rm -f "$OUT/env"
    # The shell D-Bus-activates xdg-desktop-portal while it starts, so the portal's Wayland
    # display and desktop name must already be in the bus's activation environment: a
    # portal started without them never shows a dialog and the app waits forever.
    nohup dbus-run-session -- bash -c "echo \"export DBUS_SESSION_BUS_ADDRESS='\$DBUS_SESSION_BUS_ADDRESS'\" > '$OUT/env';
      echo \"export WAYLAND_DISPLAY=$DISPLAY_NAME\" >> '$OUT/env';
      dbus-update-activation-environment WAYLAND_DISPLAY=$DISPLAY_NAME XDG_CURRENT_DESKTOP=${RIG_DESKTOP:-GNOME} XDG_SESSION_TYPE=wayland;
      XDG_CURRENT_DESKTOP=${RIG_DESKTOP:-GNOME} XDG_SESSION_TYPE=wayland exec gnome-shell --headless --wayland --no-x11 --unsafe-mode --virtual-monitor ${MON:-1400x900} --wayland-display $DISPLAY_NAME" > "$OUT/shell.log" 2>&1 &
    echo $! > "$OUT/wrapper.pid"
    for _ in $(seq 60); do [ -S "$XDG_RUNTIME_DIR/$DISPLAY_NAME" ] && break; sleep 1; done
    sleep 3
    pgrep -f "gnome-shell --headless.*--wayland-display $DISPLAY_NAME" > "$OUT/shell.pid"
    ls -la "$XDG_RUNTIME_DIR/$DISPLAY_NAME" && cat "$OUT/env" && echo "shell pid $(tr '\n' ' ' < "$OUT/shell.pid")"
    ;;
  patchy)
    . "$OUT/env"; scale=$2; shift 2
    rm -rf "$OUT/settings"; mkdir -p "$OUT/settings"
    nohup flatpak run --user --socket=wayland --filesystem=home --env=XDG_CURRENT_DESKTOP=GNOME --env=QT_QPA_PLATFORM=wayland \
      --env=QT_SCALE_FACTOR="$scale" --env=PATCHY_SETTINGS_DIR="$OUT/settings" "$@" com.rtsoft.patchy > "$OUT/patchy.log" 2>&1 &
    echo "$scale" > "$OUT/scale"
    sleep 12; tail -3 "$OUT/patchy.log"
    ;;
  script)
    . "$OUT/env"; cp "$2" "$OUT/forward.js"; rm -f "$OUT/forward.out"
    timeout 60 flatpak run --user --socket=wayland --filesystem=home --env=XDG_CURRENT_DESKTOP=GNOME --env=QT_QPA_PLATFORM=wayland \
      --env=QT_SCALE_FACTOR="$(cat "$OUT/scale")" --env=PATCHY_SETTINGS_DIR="$OUT/settings" com.rtsoft.patchy --run-script "$OUT/forward.js" --script-output "$OUT/forward.out"
    sleep 2; cat "$OUT/forward.out" 2>/dev/null
    ;;
  drive)
    . "$OUT/env"; shift
    MON=${MON:-1400x900} python3 "$HERE/gnome-input.py" "$OUT"
    ;;
  stop)
    flatpak kill com.rtsoft.patchy 2>/dev/null
    # shell.pid can hold two pids (the launcher and the shell); kill each one.
    for f in shell.pid wrapper.pid; do
      [ -f "$OUT/$f" ] && for p in $(cat "$OUT/$f"); do kill "$p" 2>/dev/null; done
    done
    sleep 1; pgrep -af "gnome-shell --headless" | grep -v pgrep
    ;;
esac
