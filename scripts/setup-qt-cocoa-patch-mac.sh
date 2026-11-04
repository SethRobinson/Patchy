#!/usr/bin/env bash
# One-time (idempotent) build of the patched Qt cocoa platform plugin the macOS app ships
# (docs/building-qt.md, "Patched Qt cocoa plugin"; GitHub issue 95): libqcocoa.dylib from the
# pinned qtbase 6.8.3 source with packaging/macos/qt-patches/*.patch applied, built for the
# host architecture and Patchy's deployment target, into .deps (gitignored). The Qt kit in
# .deps/Qt is never modified; packaging/macos/make-dmg.sh puts this plugin in the bundle.
#
#   bash scripts/setup-qt-cocoa-patch-mac.sh            # provisions <main checkout>/.deps
#   bash scripts/setup-qt-cocoa-patch-mac.sh /some/deps  # explicit deps folder
# Rerunning is a fast no-op until the patch file changes.
#
# The plugin must drop into the aqt kit's frameworks, so the build is checked against them
# before it is accepted: identical QtCore/QtGui feature headers, the same @rpath libraries
# and rpath as the stock plugin, and every symbol it imports from QtCore/QtGui exported by
# the kit's frameworks.
set -euo pipefail

QT_VERSION="6.8.3"
DEPLOYMENT_TARGET="12.0"
QTBASE_SHA256="56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80"
QTBASE_URL="https://download.qt.io/archive/qt/6.8/$QT_VERSION/submodules/qtbase-everywhere-src-$QT_VERSION.tar.xz"

[[ "$(uname -s)" == "Darwin" ]] || { echo "setup-qt-cocoa-patch-mac: macOS only" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(dirname "$SCRIPT_DIR")"
[[ -f "$REPO/CMakeLists.txt" ]] || { echo "repository root not found at $REPO" >&2; exit 1; }
PATCH_DIR="$REPO/packaging/macos/qt-patches"

if [[ $# -ge 1 ]]; then
  DEPS_DIR="$1"
else
  COMMON_DIR="$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir)"
  MAIN_CHECKOUT="$(dirname "$COMMON_DIR")"
  [[ -f "$MAIN_CHECKOUT/CMakeLists.txt" ]] || { echo "main checkout not found at $MAIN_CHECKOUT; pass a deps folder" >&2; exit 1; }
  DEPS_DIR="$MAIN_CHECKOUT/.deps"
fi
KIT="$DEPS_DIR/Qt/$QT_VERSION/macos"
STOCK_PLUGIN="$KIT/plugins/platforms/libqcocoa.dylib"
[[ -f "$STOCK_PLUGIN" ]] || { echo "Qt $QT_VERSION kit not found at $KIT (run scripts/remote/setup-mac.sh)" >&2; exit 1; }

DEST_NAME="qt-cocoa-$QT_VERSION-patched"
DEST="$DEPS_DIR/$DEST_NAME"
PATCHES=("$PATCH_DIR"/qtbase-"$QT_VERSION"-*.patch)
[[ -f "${PATCHES[0]}" ]] || { echo "no qtbase $QT_VERSION patches in $PATCH_DIR" >&2; exit 1; }
PATCH_STAMP="$(cat "${PATCHES[@]}" | shasum -a 256 | cut -d' ' -f1)"
if [[ -f "$DEST/platforms/libqcocoa.dylib" && -f "$DEST/patches.sha256" \
      && "$(cat "$DEST/patches.sha256")" == "$PATCH_STAMP" ]]; then
  echo "patched Qt $QT_VERSION cocoa plugin is in place: $DEST"
  exit 0
fi

ARCH="$(uname -m)"
STOCK_ARCHS="$(lipo -archs "$STOCK_PLUGIN")"
[[ " $STOCK_ARCHS " == *" $ARCH "* ]] || { echo "the kit's cocoa plugin has no $ARCH slice ($STOCK_ARCHS)" >&2; exit 1; }

# Qt 6.8.3 links AGL, which the macOS 26 SDK dropped, and was validated against SDK 15
# (the kit itself was built with 14.4). Build against a macOS 15 SDK; the Command Line
# Tools keep one even when Xcode is newer.
SDK=""
for candidate in /Library/Developer/CommandLineTools/SDKs/MacOSX15*.sdk \
                 "$(xcode-select -p)"/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15*.sdk; do
  [[ -d "$candidate" ]] && SDK="$candidate"
done
[[ -n "$SDK" ]] || { echo "no macOS 15 SDK found; install the Command Line Tools for Xcode 16 (they carry MacOSX15.x.sdk)" >&2; exit 1; }
echo "building against $SDK ($ARCH)"

# The pip-installed cmake and ninja from scripts/remote/setup-mac.sh, when present.
export PATH="$HOME/.patchy-tools/bin:$PATH"
command -v cmake >/dev/null && command -v ninja >/dev/null || { echo "setup-qt-cocoa-patch-mac: needs cmake and ninja on PATH" >&2; exit 1; }

# Qt refuses a source or build path that goes through a symlink (/tmp and /var are), so
# the scratch folder is used by its physical path.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/patchy-qtcocoa.XXXXXX")"
WORK="$(cd -P "$WORK" && pwd)"
cleanup() {
  case "$WORK" in
    */patchy-qtcocoa.?*) rm -rf "$WORK" ;;
    *) echo "WARNING: not deleting unexpected scratch path '$WORK'" >&2 ;;
  esac
}
trap cleanup EXIT

echo "downloading $QTBASE_URL"
curl -fsSL --retry 3 -o "$WORK/qtbase.tar.xz" "$QTBASE_URL"
actual="$(shasum -a 256 "$WORK/qtbase.tar.xz" | cut -d' ' -f1)"
if [[ "$actual" != "$QTBASE_SHA256" ]]; then
  rm -f "$WORK/qtbase.tar.xz"
  echo "checksum mismatch for qtbase $QT_VERSION: expected $QTBASE_SHA256, got $actual" >&2
  exit 1
fi
tar -xJf "$WORK/qtbase.tar.xz" -C "$WORK"
SRC="$WORK/qtbase-everywhere-src-$QT_VERSION"
for patch_file in "${PATCHES[@]}"; do
  echo "applying $(basename "$patch_file")"
  patch -d "$SRC" -p1 --forward --batch < "$patch_file"
done

# Apple clang 17+ only declares the __yield builtin qyieldcpu.h calls through arm_acle.h,
# which Qt 6.8.3 does not include.
EXTRA_FLAGS=()
if [[ "$ARCH" == "arm64" ]]; then
  EXTRA_FLAGS=("-DCMAKE_CXX_FLAGS=-include arm_acle.h" "-DCMAKE_OBJCXX_FLAGS=-include arm_acle.h")
fi
BUILD="$WORK/build"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DQT_BUILD_TESTS=OFF \
  -DQT_BUILD_EXAMPLES=OFF \
  -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
  -DCMAKE_OSX_SYSROOT="$SDK" \
  -DCMAKE_INSTALL_PREFIX="$WORK/install" \
  ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} > "$WORK/configure.log" 2>&1 \
  || { tail -40 "$WORK/configure.log" >&2; exit 1; }

# Every core but four (at least one), like the remote builds.
JOBS=$(( $(sysctl -n hw.ncpu) - 4 ))
[[ "$JOBS" -ge 1 ]] || JOBS=1
nice -n 10 ninja -C "$BUILD" -j "$JOBS" QCocoaIntegrationPlugin > "$WORK/build.log" 2>&1 \
  || { grep -E -A12 '^FAILED' "$WORK/build.log" | head -60 >&2; exit 1; }
BUILT="$BUILD/plugins/platforms/libqcocoa.dylib"

echo "== checking the plugin against the kit =="
for pair in \
    "QtCore.framework/Versions/A/Headers/qtcore-config.h:src/corelib/qtcore-config.h" \
    "QtCore.framework/Versions/A/Headers/$QT_VERSION/QtCore/private/qtcore-config_p.h:src/corelib/qtcore-config_p.h" \
    "QtGui.framework/Versions/A/Headers/qtgui-config.h:src/gui/qtgui-config.h" \
    "QtGui.framework/Versions/A/Headers/$QT_VERSION/QtGui/private/qtgui-config_p.h:src/gui/qtgui-config_p.h"; do
  if ! diff -q "$KIT/lib/${pair%%:*}" "$BUILD/${pair#*:}" >/dev/null; then
    diff "$KIT/lib/${pair%%:*}" "$BUILD/${pair#*:}" >&2 || true
    echo "feature mismatch with the kit: ${pair#*:}" >&2
    exit 1
  fi
done

rpaths() { otool -l -arch "$ARCH" "$1" | awk '/cmd LC_RPATH/{getline; getline; print $2}'; }
qt_libs() { otool -L -arch "$ARCH" "$1" | awk '/@rpath\//{print $1}' | sort; }
for rpath in $(rpaths "$BUILT"); do
  install_name_tool -delete_rpath "$rpath" "$BUILT"
done
for rpath in $(rpaths "$STOCK_PLUGIN"); do
  install_name_tool -add_rpath "$rpath" "$BUILT"
done
[[ "$(rpaths "$BUILT")" == "$(rpaths "$STOCK_PLUGIN")" ]] || { echo "rpath mismatch with the stock plugin" >&2; exit 1; }
[[ "$(qt_libs "$BUILT")" == "$(qt_libs "$STOCK_PLUGIN")" ]] || {
  echo "linked Qt libraries differ from the stock plugin:" >&2
  qt_libs "$BUILT" >&2
  exit 1
}
for lib in QtCore QtGui; do
  nm -m -u "$BUILT" | sed -n -E "s/^.*external ([^ ]+) \\(from $lib\\)$/\\1/p" | sort -u > "$WORK/imports-$lib.txt"
  nm -gU -arch "$ARCH" "$KIT/lib/$lib.framework/Versions/A/$lib" | awk '{print $3}' | sort -u > "$WORK/exports-$lib.txt"
  missing="$(comm -23 "$WORK/imports-$lib.txt" "$WORK/exports-$lib.txt")"
  if [[ -n "$missing" ]]; then
    echo "symbols the plugin imports that the kit's $lib does not export:" >&2
    echo "$missing" | head -20 >&2
    exit 1
  fi
  echo "$lib: $(wc -l < "$WORK/imports-$lib.txt" | tr -d ' ') imports, all exported by the kit"
done
# install_name_tool invalidated the linker's signature; the dmg build re-signs it.
codesign --force -s - "$BUILT"

if [[ -d "$DEST" ]]; then
  rm -rf "${DEPS_DIR:?}/$DEST_NAME"
fi
rm -rf "${DEPS_DIR:?}/$DEST_NAME.partial"
mkdir -p "$DEPS_DIR/$DEST_NAME.partial/platforms"
cp "$BUILT" "$DEPS_DIR/$DEST_NAME.partial/platforms/libqcocoa.dylib"
echo "$PATCH_STAMP" > "$DEPS_DIR/$DEST_NAME.partial/patches.sha256"
mv "$DEPS_DIR/$DEST_NAME.partial" "$DEST"
echo "patched Qt $QT_VERSION cocoa plugin installed: $DEST/platforms/libqcocoa.dylib"
