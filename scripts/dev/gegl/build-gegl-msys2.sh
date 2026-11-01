#!/usr/bin/env bash
# Builds Patchy's own GEGL for Windows (docs/gegl-filters.md, "Bundled GEGL") in an MSYS2
# CLANG64 (x64) or CLANGARM64 (ARM64) shell and stages it for CMake:
#
#   <repo>/.deps/gegl/<gegl version>/<win-x64|win-arm64>/{bin,lib/gegl-0.4,lib/babl-0.1,share/locale,licenses}
#
#   MSYSTEM=CLANG64 bash -l scripts/dev/gegl/build-gegl-msys2.sh <repo> [--gpl3]
#
# babl and GEGL come from gimp.org's release tarballs, checked against the pinned SHA-256
# below; every optional GEGL dependency is off (no PDF, raw, SVG, text, video, TIFF...).
# The rest comes from MSYS2's packages (GLib, json-glib and their libraries, libjpeg-turbo,
# libpng, zlib). Only the operation modules Patchy offers are staged: never the file
# loaders and savers, transforms, seamless clone or vector fill, and the GPL-3 module only
# with --gpl3 (docs/legal-licensing.md). Needs: pacman packages toolchain meson ninja
# pkgconf glib2 json-glib gettext-tools python libjpeg-turbo libpng zlib.
set -euo pipefail

REPO=${1:?usage: build-gegl-msys2.sh <repo> [--gpl3]}
GPL3=${2:-}
[ -f "$REPO/CMakeLists.txt" ] || { echo "not a Patchy checkout: $REPO" >&2; exit 2; }

GEGL_VERSION=0.4.72
GEGL_SHA256=ccbb8cdd1db56ecd4ece5dbabae0118ab2c46b5b3439c94f3cec467798ce956d
BABL_VERSION=0.1.128
BABL_SHA256=e676507b421d414d49db8627415dc1eadbd787536baa7d6fe94a1f1971cbe73e

case "${MSYSTEM:-}" in
  CLANG64) PLATFORM=win-x64; PACKAGE_PREFIX=mingw-w64-clang-x86_64 ;;
  CLANGARM64) PLATFORM=win-arm64; PACKAGE_PREFIX=mingw-w64-clang-aarch64 ;;
  *) echo "run in an MSYS2 CLANG64 or CLANGARM64 shell" >&2; exit 2 ;;
esac

WORK=$(mktemp -d /tmp/patchy-gegl-XXXXXX)
PREFIX="$WORK/install"
OUT="$REPO/.deps/gegl/$GEGL_VERSION/$PLATFORM"
cd "$WORK"

fetch() {  # url sha256
  local file
  file=$(basename "$1")
  curl -sSfL -o "$file" "$1"
  echo "$2  $file" | sha256sum -c - >/dev/null
  tar xf "$file" 2>/dev/null || true  # GEGL's tarball holds Lua symlinks Windows cannot make; unused
}
fetch "https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz" "$BABL_SHA256"
fetch "https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz" "$GEGL_SHA256"

# A developer self-check of the export lists (check-def-files) calls a Windows tool that
# fails under MSYS2: build everything else (-k 0) and install without rebuilding.
build() {  # source dir, extra options...
  local src=$1
  shift
  meson setup "build-$src" "$src" --prefix="$PREFIX" --buildtype=release -Dauto_features=disabled "$@" >"$src-setup.log"
  ninja -C "build-$src" -k 0 >"$src-build.log" || true
  meson install -C "build-$src" --no-rebuild >>"$src-build.log"
}
build "babl-$BABL_VERSION" -Dwith-docs=false -Denable-gir=false -Denable-vapi=false -Drelocatable-bundle=yes
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" build "gegl-$GEGL_VERSION" -Ddocs=false -Dworkshop=false \
  -Dintrospection=false -Drelocatable-bundle=yes

STAGE="$WORK/stage"
mkdir -p "$STAGE/bin" "$STAGE/lib/gegl-0.4" "$STAGE/lib/babl-0.1" "$STAGE/licenses"
cp "$PREFIX/bin/libgegl-0.4-0.dll" "$PREFIX/bin/libbabl-0.1-0.dll" "$STAGE/bin/"
SYSBIN="/${MSYSTEM,,}/bin"
modules=(gegl-common gegl-common-cxx gegl-generated)
[ "$GPL3" = --gpl3 ] && modules+=(gegl-common-gpl3)
for module in "${modules[@]}"; do
  for variant in "" -x86_64-v2 -x86_64-v3; do
    [ -f "$PREFIX/lib/gegl-0.4/$module$variant.dll" ] && cp "$PREFIX/lib/gegl-0.4/$module$variant.dll" "$STAGE/lib/gegl-0.4/"
  done
done
cp "$PREFIX/lib/gegl-0.4/gegl-core.dll" "$STAGE/lib/gegl-0.4/"
cp "$PREFIX"/lib/babl-0.1/*.dll "$STAGE/lib/babl-0.1/"
# GEGL's own translations (its labels follow Patchy's language), for Patchy's languages only.
for catalog in "$REPO"/translations/patchy_*.ts; do
  language=$(basename "$catalog" .ts)
  language=${language#patchy_}
  if [ -f "$PREFIX/share/locale/$language/LC_MESSAGES/gegl-0.4.mo" ]; then
    mkdir -p "$STAGE/share/locale/$language/LC_MESSAGES"
    cp "$PREFIX/share/locale/$language/LC_MESSAGES/gegl-0.4.mo" "$STAGE/share/locale/$language/LC_MESSAGES/"
  fi
done
rm -f "$STAGE/lib/babl-0.1/cairo.dll"  # cairo pixel formats: Patchy hands GEGL no cairo surfaces
# The MSYS2 libraries the staged ones load, found by following their import tables until
# nothing new appears (GLib and its libraries, json-glib, zlib, the C++ runtime where the
# toolchain links it as a DLL). Read statically with objdump: ldd loads each DLL and hangs
# under MSYS2 on ARM64.
while true; do
  added=0
  for name in $(objdump -p "$STAGE"/bin/*.dll "$STAGE"/lib/*/*.dll 2>/dev/null | awk '/DLL Name:/ {print $3}' | sort -u); do
    if [ -f "$SYSBIN/$name" ] && [ ! -f "$STAGE/bin/$name" ]; then
      cp "$SYSBIN/$name" "$STAGE/bin/"
      added=1
    fi
  done
  [ $added = 0 ] && break
done
strip --strip-unneeded "$STAGE"/bin/*.dll "$STAGE"/lib/*/*.dll

# Licenses, and what to ask for the sources (docs/gegl-filters.md, "Bundled GEGL").
cp "gegl-$GEGL_VERSION/COPYING.LESSER" "$STAGE/licenses/gegl-COPYING.LESSER.txt"
cp "gegl-$GEGL_VERSION/COPYING" "$STAGE/licenses/gegl-COPYING.txt"
cp "babl-$BABL_VERSION/COPYING" "$STAGE/licenses/babl-COPYING.txt"
for package in glib2 json-glib libffi pcre2 libiconv gettext-runtime libjpeg-turbo libpng zlib; do
  for file in "/${MSYSTEM,,}/share/licenses/$package"/*; do
    [ -f "$file" ] && cp "$file" "$STAGE/licenses/$package-$(basename "$file").txt"
  done
done
{
  echo "Patchy's GEGL bundle ($PLATFORM), built $(date -u +%Y-%m-%d) by scripts/dev/gegl/build-gegl-msys2.sh"
  echo "GEGL $GEGL_VERSION https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz sha256 $GEGL_SHA256"
  echo "babl $BABL_VERSION https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz sha256 $BABL_SHA256"
  echo "GPL-3 operations: $([ "$GPL3" = --gpl3 ] && echo included || echo not included)"
  echo "MSYS2 packages (sources: https://github.com/msys2/MINGW-packages and the upstream projects):"
  for package in glib2 json-glib libffi pcre2 libiconv gettext-runtime libjpeg-turbo libpng zlib; do
    pacman -Q "$PACKAGE_PREFIX-$package"
  done
} >"$STAGE/licenses/SOURCES.txt"

# Refill the folder in place (a shell or explorer may hold it open).
mkdir -p "$OUT"
rm -rf "${OUT:?}/bin" "${OUT:?}/lib" "${OUT:?}/share" "${OUT:?}/licenses"
cp -r "$STAGE"/. "$OUT"/
cd "$REPO"
rm -rf "${WORK:?}"
echo "staged $OUT: $(find "$OUT" -type f | wc -l) files, $(du -sh "$OUT" | cut -f1)"
