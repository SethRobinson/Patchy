#!/usr/bin/env bash
# Headless KWin (Plasma 6) rig for Wayland bug repros on the Linux build host: kwin_wayland --virtual
# in a rootless podman container (an image with kwin, python3-dbus and qt6-qttools; docs/linux-testing.md,
# "Foreign drags and the KDE rig"), driven from the host through org_kde_kwin_fake_input.
#   kwin-rig.sh start [image]       starts KWin; socket $OUT/rt/kwin-rig (default image localhost/patchy-226-kde-py)
#   kwin-rig.sh launch <exe|flatpak> [file]   starts Patchy on it: a host binary, "flatpak" for the installed
#                                   Flatpak, or BUILD=<dir> flatpak for a build dir run on the SDK's Qt
#   kwin-rig.sh drive               kwin-input.py with stdin commands (move, rel, rpress, kcdown 56, ...)
#   kwin-rig.sh pointer | windows   prints KWin's pointer position / the windows (and maximizes Patchy)
#   kwin-rig.sh shot NAME [cursor]  writes $OUT/NAME.png through ScreenShot2
#   kwin-rig.sh stop                stops a host binary (recorded pid) and the container; stop a Flatpak
#                                   Patchy by its instance (flatpak ps, flatpak kill <instance>)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
[ -n "$ROOT" ] && [ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: could not find the repository root" >&2; exit 1; }
OUT=$ROOT/build/kwin-rig
NAME=patchy-kwin-rig
SOCKET=$OUT/rt/kwin-rig
in_rig() { podman exec -e DBUS_SESSION_BUS_ADDRESS="$(cat "$OUT/bus")" "$NAME" "$@"; }
case "$1" in
  start)
    mkdir -p "$OUT/rt" "$OUT/share/applications"; chmod 700 "$OUT/rt"
    rm -f "$SOCKET" "$SOCKET.lock" "$OUT/bus"
    cp "$HERE"/kwin-shot.py "$HERE"/kwin-pointer.js "$HERE"/kwin-windows.js "$OUT/"
    # ScreenShot2 answers only a caller whose executable a desktop file lists with this restricted interface.
    cp "$HERE/rigshot.desktop" "$OUT/share/applications/"
    podman rm -f "$NAME" >/dev/null 2>&1
    # The copied kwin_wayland drops the file capability that fails rootless; KDIRWATCH_METHOD=Stat because
    # the host has no inotify instances left; QT_FORCE_STDERR_LOGGING keeps script print() in kwin.log.
    podman run -d --name "$NAME" --userns=keep-id --device /dev/dri -v "$OUT:/rig" \
      -e XDG_RUNTIME_DIR=/rig/rt -e HOME=/rig/home -e XDG_DATA_DIRS=/rig/share:/usr/local/share:/usr/share \
      -e KDIRWATCH_METHOD=Stat -e KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 -e KWIN_COMPOSE=O2 -e QT_FORCE_STDERR_LOGGING=1 \
      -e QT_LOGGING_RULES='*.debug=false;js.debug=true;kwin_scripting.debug=true' \
      -e RIG_WIDTH="${RIG_WIDTH:-1400}" -e RIG_HEIGHT="${RIG_HEIGHT:-900}" \
      "${2:-localhost/patchy-226-kde-py}" bash -c 'cp /usr/sbin/kwin_wayland /tmp/kwin_wayland; mkdir -p /rig/home;
        kbuildsycoca6 >/dev/null 2>&1; exec dbus-run-session -- bash -c "echo \$DBUS_SESSION_BUS_ADDRESS > /rig/bus;
          exec /tmp/kwin_wayland --virtual --width ${RIG_WIDTH:-1400} --height ${RIG_HEIGHT:-900} --socket kwin-rig \
            --no-lockscreen --no-global-shortcuts > /rig/kwin.log 2>&1"' >/dev/null
    for _ in $(seq 30); do [ -S "$SOCKET" ] && break; sleep 1; done
    ls -la "$SOCKET"
    ;;
  launch)
    rm -rf "$OUT/settings"; mkdir -p "$OUT/settings"
    file=${3:-}
    if [ "$2" = flatpak ]; then
      if [ -n "${BUILD:-}" ]; then app=(--command="$BUILD/patchy" org.kde.Sdk//6.11); else app=(com.rtsoft.patchy); fi
      setsid nohup flatpak run --user --filesystem=home --env=WAYLAND_DISPLAY="$SOCKET" --env=QT_QPA_PLATFORM=wayland \
        --env=XDG_CURRENT_DESKTOP=KDE --env=PATCHY_SETTINGS_DIR="$OUT/settings" --env=PATCHY_NO_SINGLE_INSTANCE=1 \
        "${app[@]}" ${file:+"$file"} > "$OUT/patchy.log" 2>&1 < /dev/null &
    else
      WAYLAND_DISPLAY="$SOCKET" QT_QPA_PLATFORM=wayland XDG_CURRENT_DESKTOP=KDE APPIMAGE_EXTRACT_AND_RUN=1 \
        PATCHY_SETTINGS_DIR="$OUT/settings" PATCHY_NO_SINGLE_INSTANCE=1 \
        setsid nohup "$2" ${file:+"$file"} > "$OUT/patchy.log" 2>&1 < /dev/null &
    fi
    echo $! > "$OUT/patchy.pid"
    ;;
  drive)
    python3 "$HERE/kwin-input.py" "$SOCKET"
    ;;
  pointer|windows)
    before=$(grep -c "js: " "$OUT/kwin.log")
    id=$(in_rig qdbus-qt6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "/rig/kwin-$1.js" "rig-$1-$RANDOM")
    in_rig qdbus-qt6 org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.run >/dev/null
    sleep 0.3
    in_rig qdbus-qt6 org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.stop >/dev/null 2>&1
    grep "js: " "$OUT/kwin.log" | tail -n +$((before + 1))
    ;;
  shot)
    dims=$(in_rig python3 /rig/kwin-shot.py "/rig/$2.raw" ${3:-})
    set -- "$2" $dims
    # QImage formats 16-18 are RGBA in memory; the ARGB32 family is BGRA.
    case "$5" in 16|17|18) order=rgba ;; *) order=bgra ;; esac
    convert -size "${2}x${3}" -depth 8 "$order:$OUT/$1.raw" -alpha off "$OUT/$1.png" && rm -f "$OUT/$1.raw"
    echo "shot $OUT/$1.png"
    ;;
  stop)
    if [ -f "$OUT/patchy.pid" ]; then
      for p in $(pgrep -P "$(cat "$OUT/patchy.pid")") "$(cat "$OUT/patchy.pid")"; do kill "$p" 2>/dev/null; done
      rm -f "$OUT/patchy.pid"
    fi
    podman rm -f "$NAME" >/dev/null 2>&1; echo stopped
    ;;
esac
