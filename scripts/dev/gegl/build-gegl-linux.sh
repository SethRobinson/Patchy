#!/usr/bin/env sh
# Builds Patchy's own GEGL for Linux (docs/gegl-filters.md, "Bundled GEGL") inside the
# Flatpak SDK Patchy's Flatpak uses, so it matches that runtime, and stages it for CMake:
#
#   <repo>/.deps/gegl/<gegl version>/linux-x64/{lib,lib/gegl-0.4,lib/babl-0.1,share/locale,licenses}
#
#   scripts/dev/gegl/build-gegl-linux.sh <repo>
#
# It re-runs itself in `flatpak run org.kde.Sdk//<the manifest's runtime-version>`. babl and
# GEGL come from gimp.org's release tarballs, checked against the pinned SHA-256; GLib and
# json-glib are the runtime's own and are not staged. The modules staged are the ones the
# Windows script stages (build-gegl-msys2.sh), GPL-3 module included, and GEGL is built
# without the operations common.sh names.
set -eu

REPO=${1:?usage: build-gegl-linux.sh <repo>}
REPO=$(cd "$REPO" && pwd)
[ -f "$REPO/CMakeLists.txt" ] || { echo "not a Patchy checkout: $REPO" >&2; exit 2; }
PATCHY_GEGL_SCRIPTS=$(cd "$(dirname "$0")" && pwd)
. "$PATCHY_GEGL_SCRIPTS/common.sh"

GEGL_VERSION=0.4.72
GEGL_SHA256=ccbb8cdd1db56ecd4ece5dbabae0118ab2c46b5b3439c94f3cec467798ce956d
BABL_VERSION=0.1.128
BABL_SHA256=e676507b421d414d49db8627415dc1eadbd787536baa7d6fe94a1f1971cbe73e

if [ ! -f /.flatpak-info ]; then
  RUNTIME=$(sed -n "s/^runtime-version: *'\{0,1\}\([^']*\)'\{0,1\}.*/\1/p" "$REPO/packaging/linux/flatpak/com.rtsoft.patchy.yml")
  exec flatpak run --share=network --filesystem="$REPO" --command=sh "org.kde.Sdk//$RUNTIME" "$REPO/scripts/dev/gegl/build-gegl-linux.sh" "$REPO"
fi

case "$(uname -m)" in
  x86_64) PLATFORM=linux-x64 ;;
  aarch64) PLATFORM=linux-aarch64 ;;
  *) echo "unsupported machine $(uname -m)" >&2; exit 2 ;;
esac
WORK=$(mktemp -d /tmp/patchy-gegl-XXXXXX)
PREFIX="$WORK/install"
OUT="$REPO/.deps/gegl/$GEGL_VERSION/$PLATFORM"
cd "$WORK"

fetch() {  # url sha256
  file=$(basename "$1")
  curl -sSfL -o "$file" "$1"
  echo "$2  $file" | sha256sum -c - >/dev/null
  patchy_gegl_locked "$file" "$2" || { echo "$file is not in sources.lock" >&2; exit 3; }
  tar xf "$file"
}
fetch "https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz" "$BABL_SHA256"
fetch "https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz" "$GEGL_SHA256"
patchy_gegl_drop_operations "gegl-$GEGL_VERSION"

build() {  # source dir, extra options...
  src=$1
  shift
  meson setup "build-$src" "$src" --prefix="$PREFIX" --libdir=lib --buildtype=release -Dauto_features=disabled "$@" >"$src-setup.log"
  ninja -C "build-$src" install >"$src-build.log"
}
build "babl-$BABL_VERSION" -Dwith-docs=false -Denable-gir=false -Denable-vapi=false
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" build "gegl-$GEGL_VERSION" -Ddocs=false -Dworkshop=false -Dintrospection=false

STAGE="$WORK/stage"
mkdir -p "$STAGE/lib/gegl-0.4" "$STAGE/lib/babl-0.1" "$STAGE/licenses"
cp -L "$PREFIX/lib/libgegl-0.4.so.0" "$PREFIX/lib/libbabl-0.1.so.0" "$STAGE/lib/"
for module in gegl-common gegl-common-cxx gegl-generated gegl-common-gpl3 gegl-core; do
  for file in "$PREFIX/lib/gegl-0.4/$module".so "$PREFIX/lib/gegl-0.4/$module"-x86_64-v*.so; do
    [ -f "$file" ] && cp "$file" "$STAGE/lib/gegl-0.4/"
  done
done
cp "$PREFIX"/lib/babl-0.1/*.so "$STAGE/lib/babl-0.1/"
rm -f "$STAGE/lib/babl-0.1/cairo.so"
patchy_gegl_catalogs "$REPO"
strip --strip-unneeded "$STAGE"/lib/*.so.0 "$STAGE"/lib/*/*.so
cp "gegl-$GEGL_VERSION/COPYING.LESSER" "$STAGE/licenses/gegl-COPYING.LESSER.txt"
cp "gegl-$GEGL_VERSION/COPYING" "$STAGE/licenses/gegl-COPYING.txt"
cp "babl-$BABL_VERSION/COPYING" "$STAGE/licenses/babl-COPYING.txt"
{
  echo "Patchy's GEGL bundle ($PLATFORM), built $(date -u +%Y-%m-%d) by scripts/dev/gegl/build-gegl-linux.sh in $(sed -n 's/^runtime=//p' /.flatpak-info)"
  echo "GEGL $GEGL_VERSION https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz sha256 $GEGL_SHA256"
  echo "babl $BABL_VERSION https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz sha256 $BABL_SHA256"
  echo "GLib and json-glib: the runtime's own (not in this folder)."
  patchy_gegl_sources_note
} >"$STAGE/licenses/SOURCES.txt"

mkdir -p "$OUT"
rm -rf "${OUT:?}/lib" "${OUT:?}/share" "${OUT:?}/licenses"
cp -r "$STAGE"/. "$OUT"/
cd "$REPO"
rm -rf "${WORK:?}"
echo "staged $OUT: $(find "$OUT" -type f | wc -l) files, $(du -sh "$OUT" | cut -f1)"
