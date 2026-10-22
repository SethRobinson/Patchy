#!/usr/bin/env bash
# Container half of packaging/linux/make-appimage.sh (docs/appimage.md). Runs inside the
# Ubuntu 22.04 image from ./Containerfile with the repository mounted at /src.
#   build-in-container.sh package <version>   build, stage, bundle, pack, audit, smoke check
#   build-in-container.sh tests [filter]      build the tests on this toolchain and run both suites
set -euo pipefail

MODE="${1:?usage: build-in-container.sh package <version> | tests [filter]}"
ROOT=/src
[ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: the repository is not mounted at $ROOT" >&2; exit 1; }
cd "$ROOT"
JOBS="${JOBS:-4}"
QT_DIR=/opt/qt/6.8.3/gcc_64
TOOLS=/opt/appimage-tools
ORT_LIB_DIR=/opt/deps/onnxruntime/1.30.0/linux-x64/lib

if [ "$MODE" = "tests" ]; then
  FILTER="${2:-}"
  cmake --preset linux-appimage-tests
  nice -n 10 cmake --build --preset linux-appimage-tests -j "$JOBS"
  echo "== patchy_core_tests (gcc-12, Qt 6.8.3) =="
  # The filter usually names UI tests, and a filter that selects nothing fails a suite;
  # run the core suite only when the filter selects some of its tests.
  if [ -z "$FILTER" ] || (cd build/appimage-tests && ./patchy_core_tests --list "$FILTER" >/dev/null 2>&1); then
    (cd build/appimage-tests && nice -n 10 ./patchy_core_tests $FILTER)
  else
    echo "no core test matches \"$FILTER\"; core suite not run"
  fi
  echo "== patchy_ui_visual_tests (offscreen, gcc-12, Qt 6.8.3) =="
  (cd build/appimage-tests && QT_QPA_PLATFORM=offscreen nice -n 10 ./patchy_ui_visual_tests $FILTER)
  exit 0
fi
[ "$MODE" = "package" ] || { echo "ERROR: unknown mode $MODE" >&2; exit 2; }
VERSION="${2:?package needs the version}"

echo "== configure and build (linux-appimage: gcc-12, Qt 6.8.3, glibc 2.35) =="
cmake --preset linux-appimage
nice -n 10 cmake --build --preset linux-appimage --target patchy_refresh_build_stamp -j "$JOBS"
nice -n 10 cmake --build --preset linux-appimage -j "$JOBS"

echo "== stage the AppDir =="
APPDIR="$ROOT/build/appimage-appdir"
rm -rf "${ROOT:?}/build/appimage-appdir"
cmake --install build/appimage --prefix "$APPDIR/usr"
# AppRun starts the MCP connector as usr/bin/patchy --mcp-connector (GitHub issue 336), so
# the installed patchy-mcp wrapper script would only name a path inside one launch's mount.
rm -f "${APPDIR:?}/usr/bin/patchy-mcp"
# The app dlopens libonnxruntime.so.1 by name (segmentation_engine_onnx.cpp); the RUNPATH
# linuxdeploy writes into usr/bin/patchy ($ORIGIN/../lib) finds it in usr/lib.
mkdir -p "$APPDIR/usr/lib"
cp -a "$ORT_LIB_DIR"/libonnxruntime.so* "$APPDIR/usr/lib/"
# Qt's SPDX notices, as the Windows package ships them (build-release.bat CopyQtLicenseSbom;
# NOTICE-THIRD-PARTY.md). CMake already installs LICENSE, NOTICE-THIRD-PARTY.md and the
# G'MIC, pocketfft and ONNX Runtime notices.
QT_LICENSES="$APPDIR/usr/share/licenses/com.rtsoft.patchy/qt"
mkdir -p "$QT_LICENSES"
for module in qtbase qtimageformats qtsvg qtpdf qtdeclarative qtwayland; do
  sbom="$QT_DIR/sbom/$module-6.8.3.spdx"
  if [ -f "$sbom" ]; then
    cp "$sbom" "$QT_LICENSES/"
  elif [ "$module" != "qtwayland" ]; then
    echo "ERROR: required Qt license SBOM not found: $sbom" >&2
    exit 1
  fi
done
# SingleMainWindow is Desktop Entry 1.5, newer than 22.04's desktop-file-validate (0.26),
# which rejects it; the AppImage catalog lints with that validator. Only the AppImage
# drops it: the Flatpak keeps the tracked file as is.
sed -i '/^SingleMainWindow=/d' "$APPDIR/usr/share/applications/com.rtsoft.patchy.desktop"

echo "== bundle libraries and Qt plug-ins (linuxdeploy) =="
# xcb is the plug-in's default platform; wayland and offscreen (--headless, the MCP
# connector) are added. glibc, libstdc++, libGL/EGL, libX11/xcb core and fontconfig stay
# on the host per linuxdeploy's excludelist (a bundled copy of those is what crashes).
export QMAKE="$QT_DIR/bin/qmake"
export LD_LIBRARY_PATH="$QT_DIR/lib"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
# The Qt plug-in links every .qm file it finds into usr/translations (Patchy reads its own
# from usr/share/patchy/translations); without the folder it logs an error per file.
mkdir -p "$APPDIR/usr/translations"
# The entry point must be executable in the image whatever mode the checkout gave it.
cp "$ROOT/packaging/linux/appimage/AppRun" /tmp/AppRun
chmod 755 /tmp/AppRun
# Keep the plug-in's AppImage beside linuxdeploy: that is where linuxdeploy looks.
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/patchy" \
  --library "$APPDIR/usr/lib/libonnxruntime.so.1" \
  --desktop-file "$APPDIR/usr/share/applications/com.rtsoft.patchy.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/com.rtsoft.patchy.png" \
  --custom-apprun /tmp/AppRun \
  --plugin qt
unset LD_LIBRARY_PATH

# linuxdeploy wraps the custom AppRun (as AppRun.wrapped) in one that sources
# linuxdeploy-plugin-qt's hook, which forces QT_QPA_PLATFORMTHEME=gtk2 on GNOME and XFCE,
# over the user's own value too. Qt 6 has no gtk2 theme, so that only kept the bundled
# portal theme from loading (GitHub issue 156). Our AppRun picks the theme itself, so it
# goes back to being the entry point; fail if linuxdeploy ever adds another hook.
[ "$(ls -A "$APPDIR/apprun-hooks" 2>/dev/null)" = "linuxdeploy-plugin-qt-hook.sh" ] \
  || { echo "ERROR: unexpected linuxdeploy AppRun hooks: $(ls -A "$APPDIR/apprun-hooks" 2>/dev/null)" >&2; exit 1; }
mv -f "$APPDIR/AppRun.wrapped" "$APPDIR/AppRun"
rm -f "$APPDIR/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
rmdir "$APPDIR/apprun-hooks"
grep -q 'QT_QPA_PLATFORMTHEME=xdgdesktopportal' "$APPDIR/AppRun" \
  || { echo "ERROR: the AppDir's AppRun is not packaging/linux/appimage/AppRun" >&2; exit 1; }
[ -f "$APPDIR/usr/plugins/platformthemes/libqxdgdesktopportal.so" ] \
  || { echo "ERROR: Qt's xdgdesktopportal platform theme was not bundled" >&2; exit 1; }

# .DirIcon is the AppImage's thumbnail in file managers and the catalog, and the spec
# wants a PNG; linuxdeploy points it at the scalable SVG. The SVG stays in usr/share/icons.
rm -f "${APPDIR:?}/.DirIcon" "${APPDIR:?}/com.rtsoft.patchy.svg" "${APPDIR:?}/com.rtsoft.patchy.png"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/com.rtsoft.patchy.png" "$APPDIR/com.rtsoft.patchy.png"
ln -s com.rtsoft.patchy.png "$APPDIR/.DirIcon"
file -b "$(readlink -f "$APPDIR/.DirIcon")" | grep -q '^PNG image' \
  || { echo "ERROR: .DirIcon is not a PNG" >&2; exit 1; }
desktop-file-validate "$APPDIR/com.rtsoft.patchy.desktop" \
  || { echo "ERROR: the AppImage's desktop file fails 22.04's desktop-file-validate" >&2; exit 1; }

# linuxdeploy-plugin-qt bundles the wayland platform plug-ins but not the plug-in folders
# they load at run time (the xdg-shell integration, window decorations, the EGL client
# buffer integration); without them a Wayland session cannot open a window. Their Qt
# dependencies are already in usr/lib; the audit below checks that every one resolves.
for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
  rm -rf "${APPDIR:?}/usr/plugins/$dir"
  cp -a "$QT_DIR/plugins/$dir" "$APPDIR/usr/plugins/$dir"
  for plugin in "$APPDIR/usr/plugins/$dir"/*.so; do
    patchelf --set-rpath '$ORIGIN/../../lib' "$plugin"
    strip --strip-unneeded "$plugin"
    # Qt libraries only these plug-ins use (libQt6WlShellIntegration, ...) come from the kit.
    for missing in $(ldd "$plugin" | awk '/not found/ {print $1}'); do
      [ -f "$QT_DIR/lib/$missing" ] || continue
      cp -L "$QT_DIR/lib/$missing" "$APPDIR/usr/lib/$missing"
      patchelf --set-rpath '$ORIGIN' "$APPDIR/usr/lib/$missing"
      strip --strip-unneeded "$APPDIR/usr/lib/$missing"
    done
  done
done

# The SVG plug-ins draw every stylesheet image (checkbox ticks, flyout triangles; GitHub
# issue 93) and TLS carries the update check: fail rather than ship without them.
for required in platforms/libqxcb.so platforms/libqwayland-generic.so platforms/libqoffscreen.so \
                imageformats/libqsvg.so iconengines/libqsvgicon.so tls/libqopensslbackend.so \
                xcbglintegrations/libqxcb-glx-integration.so; do
  [ -f "$APPDIR/usr/plugins/$required" ] || { echo "ERROR: Qt plug-in $required was not bundled" >&2; exit 1; }
done
for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
  if [ ! -d "$APPDIR/usr/plugins/$dir" ]; then
    echo "ERROR: Qt plug-in folder $dir was not bundled" >&2
    exit 1
  fi
done

echo "== ABI audit (glibc <= 2.35, GLIBCXX <= 3.4.30, CXXABI <= 1.3.13) =="
# The guard for "runs on Ubuntu 22.04": every ELF file in the AppDir, bundled libraries
# included, may only need symbol versions 22.04's own glibc and libstdc++ provide.
audit_failed=0
max_version() { grep -oE "$1_[0-9][0-9.]*" | sed "s/^$1_//" | sort -Vu | tail -1; }
newer_than() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" != "$2" ]; }
while IFS= read -r -d '' file; do
  file -b "$file" | grep -q '^ELF' || continue
  symbols=$(objdump -T "$file" 2>/dev/null || true)
  for check in "GLIBC 2.35" "GLIBCXX 3.4.30" "CXXABI 1.3.13"; do
    set -- $check
    needed=$(printf '%s\n' "$symbols" | max_version "$1" || true)
    if [ -n "$needed" ] && newer_than "$needed" "$2"; then
      echo "ERROR: ${file#"$APPDIR"/} needs $1_$needed (limit $2)" >&2
      audit_failed=1
    fi
  done
  if ldd "$file" 2>/dev/null | grep -q 'not found'; then
    echo "ERROR: ${file#"$APPDIR"/} has unresolved libraries:" >&2
    ldd "$file" | grep 'not found' >&2
    audit_failed=1
  fi
done < <(find "$APPDIR" -type f \( -perm -u+x -o -name '*.so*' \) -print0)
[ "$audit_failed" = "0" ] || exit 1
echo "ABI audit passed."

echo "== pack (appimagetool, static type2 runtime) =="
# The zsync file names the AppImage it describes, so both are made under their published
# names (PatchyLinux.AppImage and .zsync, docs/release-process.md) and only renamed to the
# versioned build names afterwards. The update information lets AppImageUpdate, Gear Lever
# and AppImageLauncher fetch the next release from GitHub.
STAGE="$ROOT/build/appimage-out"
rm -rf "${ROOT:?}/build/appimage-out"
mkdir -p "$STAGE" "$ROOT/build/package"
(cd "$STAGE" && ARCH=x86_64 VERSION="$VERSION" "$TOOLS/appimagetool-x86_64.AppImage" --no-appstream \
  --runtime-file "$TOOLS/runtime-x86_64" \
  -u "gh-releases-zsync|SethRobinson|Patchy|latest|PatchyLinux.AppImage.zsync" \
  "$APPDIR" PatchyLinux.AppImage)
[ -s "$STAGE/PatchyLinux.AppImage" ] && [ -s "$STAGE/PatchyLinux.AppImage.zsync" ] \
  || { echo "ERROR: appimagetool did not write the AppImage and its zsync file" >&2; exit 1; }

echo "== smoke check inside the 22.04 container (extracted run) =="
SMOKE=$(mktemp -d /tmp/patchy-appimage-smoke.XXXXXX)
mkdir -p "$SMOKE/settings"
echo 'console.log("headless smoke")' > "$SMOKE/smoke.js"
timeout 180 env PATCHY_SETTINGS_DIR="$SMOKE/settings" PATCHY_NO_SOUND=1 \
  "$STAGE/PatchyLinux.AppImage" --headless --run-script "$SMOKE/smoke.js" --script-output "$SMOKE/out.txt" \
  > "$SMOKE/console.txt" 2>&1 || { echo "ERROR: headless smoke check failed:" >&2; cat "$SMOKE/console.txt" >&2; exit 1; }
[ "$(tail -n 1 "$SMOKE/out.txt" 2>/dev/null)" = "[done]" ] \
  || { echo "ERROR: headless smoke output did not end with [done]" >&2; cat "$SMOKE/out.txt" >&2 || true; exit 1; }
timeout 180 "$STAGE/PatchyLinux.AppImage" patchy-mcp --check
case "$SMOKE" in /tmp/patchy-appimage-smoke.?*) rm -rf "$SMOKE" ;; esac

mv "$STAGE/PatchyLinux.AppImage" "$ROOT/build/package/Patchy-$VERSION-x86_64.AppImage"
mv "$STAGE/PatchyLinux.AppImage.zsync" "$ROOT/build/package/Patchy-$VERSION-x86_64.AppImage.zsync"
echo "AppImage written: build/package/Patchy-$VERSION-x86_64.AppImage (+ .zsync)"
