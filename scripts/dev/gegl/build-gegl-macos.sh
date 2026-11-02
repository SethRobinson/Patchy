#!/usr/bin/env bash
# Builds Patchy's own GEGL for macOS (docs/gegl-filters.md, "Bundled GEGL") for Patchy's
# deployment target and stages it for CMake:
#
#   <repo>/.deps/gegl/<gegl version>/osx-arm64/{lib,lib/gegl-0.4,lib/babl-0.1,share/locale,licenses}
#
#   scripts/dev/gegl/build-gegl-macos.sh <repo>
#
# Homebrew's libraries target the macOS they were built on, so everything shipped is built
# here from release tarballs with MACOSX_DEPLOYMENT_TARGET 12.0: gettext's runtime (libintl,
# for GEGL's translations), GLib (PCRE2 through its own subproject; libffi and libiconv are
# macOS's), json-glib, babl and GEGL. Homebrew supplies only build-time pieces: meson, ninja,
# pkgconf, and jpeg-turbo and libpng, which GEGL's configure requires for the file loaders
# Patchy does not stage; the staged files are checked to name no Homebrew path. The modules
# staged are the ones the Windows script stages, GPL-3 module included, and GEGL is built
# without the operations common.sh names. Every source archive used, GLib's PCRE2
# subproject included, must be in sources.lock.
set -euo pipefail

REPO=${1:?usage: build-gegl-macos.sh <repo>}
REPO=$(cd "$REPO" && pwd)
[ -f "$REPO/CMakeLists.txt" ] || { echo "not a Patchy checkout: $REPO" >&2; exit 2; }
PATCHY_GEGL_SCRIPTS=$(cd "$(dirname "$0")" && pwd)
. "$PATCHY_GEGL_SCRIPTS/common.sh"
export PATH="/opt/homebrew/bin:$PATH"
export MACOSX_DEPLOYMENT_TARGET=12.0

GEGL_VERSION=0.4.72
GEGL_SHA256=ccbb8cdd1db56ecd4ece5dbabae0118ab2c46b5b3439c94f3cec467798ce956d
BABL_VERSION=0.1.128
BABL_SHA256=e676507b421d414d49db8627415dc1eadbd787536baa7d6fe94a1f1971cbe73e
GLIB_VERSION=2.84.4
GLIB_SHA256=8a9ea10943c36fc117e253f80c91e477b673525ae45762942858aef57631bb90
JSON_GLIB_VERSION=1.10.8
JSON_GLIB_SHA256=55c5c141a564245b8f8fbe7698663c87a45a7333c2a2c56f06f811ab73b212dd
# GNU signs gettext with PGP and publishes no checksum: this is the SHA-256 of the copy first
# fetched on November 2, 2026 (docs/gegl-provenance.md).
GETTEXT_VERSION=0.26
GETTEXT_SHA256=d1fb86e260cfe7da6031f94d2e44c0da55903dbae0a2fa0fae78c91ae1b56f00

WORK=$(mktemp -d /tmp/patchy-gegl-XXXXXX)
PREFIX="$WORK/install"
OUT="$REPO/.deps/gegl/$GEGL_VERSION/osx-arm64"
cd "$WORK"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export CPPFLAGS="-I$PREFIX/include"
export LDFLAGS="-L$PREFIX/lib -Wl,-headerpad_max_install_names"

fetch() {  # url sha256
  local file
  file=$(basename "$1")
  curl -sSfL -o "$file" "$1"
  echo "$2  $file" | shasum -a 256 -c - >/dev/null
  patchy_gegl_locked "$file" "$2" || { echo "$file is not in sources.lock" >&2; exit 3; }
  tar xf "$file"
}
fetch "https://ftp.gnu.org/gnu/gettext/gettext-$GETTEXT_VERSION.tar.xz" "$GETTEXT_SHA256"
fetch "https://download.gnome.org/sources/glib/${GLIB_VERSION%.*}/glib-$GLIB_VERSION.tar.xz" "$GLIB_SHA256"
fetch "https://download.gnome.org/sources/json-glib/${JSON_GLIB_VERSION%.*}/json-glib-$JSON_GLIB_VERSION.tar.xz" "$JSON_GLIB_SHA256"
fetch "https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz" "$BABL_SHA256"
fetch "https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz" "$GEGL_SHA256"
patchy_gegl_drop_operations "gegl-$GEGL_VERSION"

(cd "gettext-$GETTEXT_VERSION/gettext-runtime" &&
  ./configure --prefix="$PREFIX" --disable-static --disable-java --disable-csharp --disable-libasprintf \
    --disable-dependency-tracking >"$WORK/gettext-configure.log" &&
  make -j"$(sysctl -n hw.ncpu)" >"$WORK/gettext-build.log" && make install >>"$WORK/gettext-build.log")

meson_build() {  # source dir, extra options...
  local src=$1
  shift
  meson setup "build-$src" "$src" --prefix="$PREFIX" --libdir=lib --buildtype=release "$@" >"$src-setup.log"
  ninja -C "build-$src" install >"$src-build.log"
}
meson_build "glib-$GLIB_VERSION" -Dtests=false -Dintrospection=disabled -Dman-pages=disabled \
  -Ddocumentation=false -Dselinux=disabled -Dlibmount=disabled -Dsysprof=disabled --force-fallback-for=pcre2
# What meson downloaded for GLib's subprojects (PCRE2 and its build files) ships too.
missing=0
for archive in "glib-$GLIB_VERSION"/subprojects/packagecache/*; do
  [ -f "$archive" ] || continue
  sum=$(shasum -a 256 "$archive" | cut -d' ' -f1)
  if ! patchy_gegl_locked "$(basename "$archive")" "$sum"; then
    echo "add to scripts/dev/gegl/sources.lock: $(basename "$archive") $sum <its URL in glib-$GLIB_VERSION/subprojects>" >&2
    missing=1
  fi
done
[ $missing = 0 ] || exit 3
meson_build "json-glib-$JSON_GLIB_VERSION" -Dintrospection=disabled -Dtests=false -Dgtk_doc=disabled -Dman=false
meson_build "babl-$BABL_VERSION" -Dauto_features=disabled -Dwith-docs=false -Denable-gir=false -Denable-vapi=false
meson_build "gegl-$GEGL_VERSION" -Dauto_features=disabled -Ddocs=false -Dworkshop=false -Dintrospection=false

STAGE="$WORK/stage"
mkdir -p "$STAGE/lib/gegl-0.4" "$STAGE/lib/babl-0.1" "$STAGE/licenses"
cp "$PREFIX"/lib/gegl-0.4/{gegl-common,gegl-common-cxx,gegl-generated,gegl-common-gpl3,gegl-core}.dylib "$STAGE/lib/gegl-0.4/" 2>/dev/null ||
  cp "$PREFIX"/lib/gegl-0.4/{gegl-common,gegl-common-cxx,gegl-generated,gegl-common-gpl3,gegl-core}.so "$STAGE/lib/gegl-0.4/"
cp "$PREFIX"/lib/babl-0.1/* "$STAGE/lib/babl-0.1/"
rm -f "$STAGE"/lib/babl-0.1/cairo.*
# The libraries the staged ones load from the prefix, followed until nothing new appears;
# each is named @rpath/<file> and finds the others through DYLD_LIBRARY_PATH or its rpath.
queue=("$PREFIX/lib/libgegl-0.4.0.dylib" "$PREFIX/lib/libbabl-0.1.0.dylib" "$STAGE"/lib/gegl-0.4/* "$STAGE"/lib/babl-0.1/*)
while [ ${#queue[@]} -gt 0 ]; do
  item=${queue[0]}
  queue=("${queue[@]:1}")
  for dependency in $(otool -L "$item" | awk 'NR > 1 {print $1}' | grep -E "^($PREFIX|@rpath)/" || true); do
    file=$(basename "$dependency")
    if [ ! -f "$STAGE/lib/$file" ] && [ -f "$PREFIX/lib/$file" ]; then
      cp "$PREFIX/lib/$file" "$STAGE/lib/"
      queue+=("$STAGE/lib/$file")
    fi
  done
done
cp "$PREFIX/lib/libgegl-0.4.0.dylib" "$PREFIX/lib/libbabl-0.1.0.dylib" "$STAGE/lib/" 2>/dev/null || true
for binary in "$STAGE"/lib/*.dylib "$STAGE"/lib/*/*; do
  chmod u+w "$binary"
  case "$binary" in *.dylib) install_name_tool -id "@rpath/$(basename "$binary")" "$binary" 2>/dev/null || true ;; esac
  for dependency in $(otool -L "$binary" | awk 'NR > 1 {print $1}' | grep "^$PREFIX/" || true); do
    install_name_tool -change "$dependency" "@rpath/$(basename "$dependency")" "$binary"
  done
  install_name_tool -add_rpath "@loader_path" "$binary" 2>/dev/null || true
  install_name_tool -add_rpath "@loader_path/.." "$binary" 2>/dev/null || true
  strip -x "$binary" 2>/dev/null || true
  codesign --force -s - "$binary" >/dev/null 2>&1  # install_name_tool invalidates the signature
done
if otool -L "$STAGE"/lib/*.dylib "$STAGE"/lib/*/* | grep -E "/opt/homebrew|/usr/local"; then
  echo "a staged library names a Homebrew path" >&2
  exit 1
fi
for catalog in "$REPO"/translations/patchy_*.ts; do
  language=$(basename "$catalog" .ts)
  language=${language#patchy_}
  if [ -f "$PREFIX/share/locale/$language/LC_MESSAGES/gegl-0.4.mo" ]; then
    mkdir -p "$STAGE/share/locale/$language/LC_MESSAGES"
    cp "$PREFIX/share/locale/$language/LC_MESSAGES/gegl-0.4.mo" "$STAGE/share/locale/$language/LC_MESSAGES/"
  fi
done
cp "gegl-$GEGL_VERSION/COPYING.LESSER" "$STAGE/licenses/gegl-COPYING.LESSER.txt"
cp "gegl-$GEGL_VERSION/COPYING" "$STAGE/licenses/gegl-COPYING.txt"
cp "babl-$BABL_VERSION/COPYING" "$STAGE/licenses/babl-COPYING.txt"
cp "glib-$GLIB_VERSION/COPYING" "$STAGE/licenses/glib-COPYING.txt"
cp "json-glib-$JSON_GLIB_VERSION/COPYING" "$STAGE/licenses/json-glib-COPYING.txt"
cp "gettext-$GETTEXT_VERSION/gettext-runtime/intl/COPYING.LIB" "$STAGE/licenses/libintl-COPYING.LIB.txt" 2>/dev/null ||
  cp "gettext-$GETTEXT_VERSION/COPYING" "$STAGE/licenses/gettext-COPYING.txt"
pcre2_license=$(find "build-glib-$GLIB_VERSION" "glib-$GLIB_VERSION/subprojects" -maxdepth 3 -name LICENCE* 2>/dev/null | head -1)
[ -n "$pcre2_license" ] && cp "$pcre2_license" "$STAGE/licenses/pcre2-LICENCE.txt"
{
  echo "Patchy's GEGL bundle (osx-arm64, macOS $MACOSX_DEPLOYMENT_TARGET and later), built $(date -u +%Y-%m-%d) by scripts/dev/gegl/build-gegl-macos.sh"
  echo "GEGL $GEGL_VERSION https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz sha256 $GEGL_SHA256"
  echo "babl $BABL_VERSION https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz sha256 $BABL_SHA256"
  echo "GLib $GLIB_VERSION https://download.gnome.org/sources/glib/ sha256 $GLIB_SHA256 (PCRE2 from its meson subproject)"
  echo "json-glib $JSON_GLIB_VERSION https://download.gnome.org/sources/json-glib/ sha256 $JSON_GLIB_SHA256"
  echo "gettext $GETTEXT_VERSION https://ftp.gnu.org/gnu/gettext/ sha256 $GETTEXT_SHA256"
  for archive in "glib-$GLIB_VERSION"/subprojects/packagecache/*; do
    [ -f "$archive" ] && echo "GLib subproject $(basename "$archive") sha256 $(shasum -a 256 "$archive" | cut -d' ' -f1)"
  done
  patchy_gegl_sources_note
} >"$STAGE/licenses/SOURCES.txt"

mkdir -p "$OUT"
rm -rf "${OUT:?}/lib" "${OUT:?}/share" "${OUT:?}/licenses"
cp -R "$STAGE"/. "$OUT"/
cd "$REPO"
rm -rf "${WORK:?}"
echo "staged $OUT: $(find "$OUT" -type f | wc -l | tr -d ' ') files, $(du -sh "$OUT" | cut -f1)"
