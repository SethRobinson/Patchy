#!/usr/bin/env bash
# Interactive GIMP 3 plug-in runs on Linux in a private Xvfb display (development only,
# docs/gimp-plugins.md "Development tools"). Patchy runs headless; each plug-in's dialog
# opens on the private display, is saved as a PNG once it has settled, then answered with
# Return (OK) through xdotool. Never touches the desktop session.
#
#   scripts/dev/gimp/interactive-x11.sh <patchy> <out-folder> <procedure> [<procedure> ...]
#
# Needs Xvfb, xdotool, xwd and ImageMagick's convert. GIMP's Flatpak binds the desktop's
# Wayland socket whatever DISPLAY says: run with
#   flatpak override --user --nosocket=wayland --socket=x11 org.gimp.GIMP
# and undo it afterwards with flatpak override --user --reset org.gimp.GIMP.
set -u
PATCHY=${1:?patchy binary}
OUT=${2:?output folder}
shift 2
mkdir -p "$OUT"
DISPLAY_NUMBER=87
Xvfb ":$DISPLAY_NUMBER" -screen 0 1600x1000x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
XVFB=$!
trap 'kill "$XVFB" 2>/dev/null' EXIT
sleep 1
export DISPLAY=":$DISPLAY_NUMBER"
unset WAYLAND_DISPLAY
# GIMP's sandbox would find the desktop's Wayland socket otherwise.
export GDK_BACKEND=x11
SCRIPT="$OUT/run.js"
cat > "$SCRIPT" <<'EOF'
var ids = String(patchy.args.only).split(",");
for (var i = 0; i < ids.length; ++i) {
  var doc = app.newDocument(256, 192, {name: ids[i]});
  var layer = doc.activeLayer;
  for (var y = 0; y < 192; y += 16)
    for (var x = 0; x < 256; x += 16)
      layer.fillRect(x, y, 16, 16, "#" + [(x * 255 / 256) | 0, (y * 255 / 192) | 0, (x ^ y) & 255]
        .map(function (v) { return ("0" + v.toString(16)).slice(-2); }).join(""));
  doc.selection.selectRect(32, 24, 160, 120);
  var before = new Uint8Array(layer.getPixels().data);
  var entry = {procedure: ids[i]};
  try {
    layer.applyGimpPlugin("gimp." + ids[i], {dialog: true, timeoutMs: 60000});
    var after = new Uint8Array(layer.getPixels().data);
    var changed = 0;
    for (var k = 0; k < after.length; ++k) if (after[k] !== before[k]) ++changed;
    entry.result = changed > 0 ? "applied" : "unchanged";
    entry.changed = changed;
  } catch (e) {
    entry.result = "failed";
    entry.error = String(e.message || e).slice(0, 300);
  }
  console.log(JSON.stringify(entry));
  doc.close();
}
EOF
ONLY=$(IFS=,; echo "$*")
QT_QPA_PLATFORM=offscreen "$PATCHY" --headless --run-script "$SCRIPT" --script-arg "only=$ONLY" \
  --script-output "$OUT/results.txt" > "$OUT/console.txt" 2>&1 &
PATCHY_PID=$!
for procedure in "$@"; do
  window=""
  for _ in $(seq 1 60); do
    window=$(xdotool search --onlyvisible --name "." 2>/dev/null | tail -1)
    [ -n "$window" ] && break
    sleep 0.5
  done
  if [ -z "$window" ]; then
    echo "$procedure: no dialog appeared" >> "$OUT/driver.txt"
    continue
  fi
  sleep 3
  xwd -silent -id "$window" -display "$DISPLAY" | convert xwd:- "$OUT/$procedure.png"
  echo "$procedure: window $window $(xdotool getwindowname "$window")" >> "$OUT/driver.txt"
  xdotool windowfocus --sync "$window" key Return
  for _ in $(seq 1 40); do
    xdotool search --onlyvisible --name "." >/dev/null 2>&1 || break
    sleep 0.5
  done
done
wait "$PATCHY_PID"
