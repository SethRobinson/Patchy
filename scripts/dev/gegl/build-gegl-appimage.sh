#!/usr/bin/env bash
# Builds Patchy's own GEGL for the Linux AppImage (docs/gegl-filters.md, "Bundled GEGL") and
# stages it for CMake:
#
#   <repo>/.deps/gegl/<gegl version>/linux-x64-appimage/{lib,lib/gegl-0.4,lib/babl-0.1,share/locale,licenses}
#
#   scripts/dev/gegl/build-gegl-appimage.sh <repo>
#
# Runs inside the AppImage's Ubuntu 22.04 container (packaging/linux/appimage/Containerfile;
# build-in-container.sh calls it when the folder is missing), so everything links against
# glibc 2.35 and passes the AppImage's ABI audit. The AppImage cannot count on the host's
# GLib or json-glib, so, as on macOS, everything shipped is built here from the release
# tarballs in sources.lock: GLib (PCRE2 through its own subproject), json-glib, babl and
# GEGL, with GEGL built without the operations common.sh names. libffi is Ubuntu's (MIT);
# zlib and glibc (gettext included) are the host's. The modules staged are the ones the
# other scripts stage, GPL-3 module included. Libraries find each other through RUNPATH
# ($ORIGIN, and $ORIGIN/.. for the module folders), checked with ldd.
set -euo pipefail

REPO=${1:?usage: build-gegl-appimage.sh <repo>}
REPO=$(cd "$REPO" && pwd)
[ -f "$REPO/CMakeLists.txt" ] || { echo "not a Patchy checkout: $REPO" >&2; exit 2; }
grep -q 'VERSION_ID="22.04"' /etc/os-release || { echo "run inside the AppImage's Ubuntu 22.04 container" >&2; exit 2; }
PATCHY_GEGL_SCRIPTS=$(cd "$(dirname "$0")" && pwd)
. "$PATCHY_GEGL_SCRIPTS/common.sh"
export CC=gcc-12 CXX=g++-12

GEGL_VERSION=0.4.72
GEGL_SHA256=ccbb8cdd1db56ecd4ece5dbabae0118ab2c46b5b3439c94f3cec467798ce956d
BABL_VERSION=0.1.128
BABL_SHA256=e676507b421d414d49db8627415dc1eadbd787536baa7d6fe94a1f1971cbe73e
GLIB_VERSION=2.84.4
GLIB_SHA256=8a9ea10943c36fc117e253f80c91e477b673525ae45762942858aef57631bb90
JSON_GLIB_VERSION=1.10.8
JSON_GLIB_SHA256=55c5c141a564245b8f8fbe7698663c87a45a7333c2a2c56f06f811ab73b212dd

WORK=$(mktemp -d /tmp/patchy-gegl-XXXXXX)
PREFIX="$WORK/install"
# ARCH (x86_64 or aarch64) names the container's architecture (packaging/linux/make-appimage.sh).
case "$(uname -m)" in
  x86_64) BUNDLE_PLATFORM=linux-x64 ;;
  aarch64) BUNDLE_PLATFORM=linux-aarch64 ;;
  *) echo "unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac
MULTIARCH_LIB="/usr/lib/$(uname -m)-linux-gnu"
OUT="$REPO/.deps/gegl/$GEGL_VERSION/$BUNDLE_PLATFORM-appimage"
cd "$WORK"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"

fetch() {  # url sha256
  local file
  file=$(basename "$1")
  curl -sSfL --retry 3 -o "$file" "$1"
  echo "$2  $file" | sha256sum -c - >/dev/null
  patchy_gegl_locked "$file" "$2" || { echo "$file is not in sources.lock" >&2; exit 3; }
  tar xf "$file"
}
fetch "https://download.gnome.org/sources/glib/${GLIB_VERSION%.*}/glib-$GLIB_VERSION.tar.xz" "$GLIB_SHA256"
fetch "https://download.gnome.org/sources/json-glib/${JSON_GLIB_VERSION%.*}/json-glib-$JSON_GLIB_VERSION.tar.xz" "$JSON_GLIB_SHA256"
fetch "https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz" "$BABL_SHA256"
fetch "https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz" "$GEGL_SHA256"
patchy_gegl_drop_operations "gegl-$GEGL_VERSION"

meson_build() {  # source dir, extra options...
  local src=$1
  shift
  meson setup "build-$src" "$src" --prefix="$PREFIX" --libdir=lib --buildtype=release "$@" >"$src-setup.log" 2>&1 ||
    { tail -n 40 "$src-setup.log" >&2; exit 1; }
  nice -n 10 ninja -C "build-$src" install >"$src-build.log" 2>&1 || { tail -n 40 "$src-build.log" >&2; exit 1; }
}
meson_build "glib-$GLIB_VERSION" -Dtests=false -Dintrospection=disabled -Dman-pages=disabled \
  -Ddocumentation=false -Dselinux=disabled -Dlibmount=disabled -Dsysprof=disabled -Dnls=disabled \
  --force-fallback-for=pcre2
# What meson downloaded for GLib's subprojects (PCRE2 and its build files) ships too.
missing=0
for archive in "glib-$GLIB_VERSION"/subprojects/packagecache/*; do
  [ -f "$archive" ] || continue
  sum=$(sha256sum "$archive" | cut -d' ' -f1)
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
for module in gegl-common gegl-common-cxx gegl-generated gegl-common-gpl3 gegl-core; do
  for file in "$PREFIX/lib/gegl-0.4/$module".so "$PREFIX/lib/gegl-0.4/$module"-x86_64-v*.so; do
    [ -f "$file" ] && cp "$file" "$STAGE/lib/gegl-0.4/"
  done
done
cp "$PREFIX"/lib/babl-0.1/*.so "$STAGE/lib/babl-0.1/"
rm -f "$STAGE/lib/babl-0.1/cairo.so"
# The libraries the staged ones load from the prefix, plus Ubuntu's libffi (GLib's one
# outside dependency the host may lack), followed until nothing new appears.
cp -L "$PREFIX/lib/libgegl-0.4.so.0" "$PREFIX/lib/libbabl-0.1.so.0" "$STAGE/lib/"
while true; do
  added=0
  for binary in "$STAGE"/lib/*.so* "$STAGE"/lib/*/*.so; do
    for name in $(objdump -p "$binary" | awk '/NEEDED/ {print $2}'); do
      [ -f "$STAGE/lib/$name" ] && continue
      if [ -f "$PREFIX/lib/$name" ]; then
        cp -L "$PREFIX/lib/$name" "$STAGE/lib/"
        added=1
      elif [ "${name%%.so*}" = libffi ]; then
        cp -L "$MULTIARCH_LIB/$name" "$STAGE/lib/"
        added=1
      fi
    done
  done
  [ $added = 0 ] && break
done
for binary in "$STAGE"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$binary"; done
for binary in "$STAGE"/lib/*/*.so; do patchelf --set-rpath '$ORIGIN/..' "$binary"; done
strip --strip-unneeded "$STAGE"/lib/*.so* "$STAGE"/lib/*/*.so
# Everything resolves inside the bundle or from glibc, libstdc++ and zlib.
if ldd "$STAGE"/lib/*.so* "$STAGE"/lib/*/*.so | grep 'not found'; then
  echo "a staged library has an unresolved dependency" >&2
  exit 1
fi
if ldd "$STAGE"/lib/*.so* "$STAGE"/lib/*/*.so | awk '/=>/ {print $3}' | sort -u |
    grep -v -E "^$STAGE/|$MULTIARCH_LIB/(libc|libm|libdl|libpthread|librt|libstdc\+\+|libgcc_s|libz)\.so"; then
  echo "a staged library loads a system library the AppImage cannot count on (above)" >&2
  exit 1
fi
patchy_gegl_catalogs "$REPO"
cp "gegl-$GEGL_VERSION/COPYING.LESSER" "$STAGE/licenses/gegl-COPYING.LESSER.txt"
cp "gegl-$GEGL_VERSION/COPYING" "$STAGE/licenses/gegl-COPYING.txt"
cp "babl-$BABL_VERSION/COPYING" "$STAGE/licenses/babl-COPYING.txt"
cp "glib-$GLIB_VERSION/COPYING" "$STAGE/licenses/glib-COPYING.txt"
cp "json-glib-$JSON_GLIB_VERSION/COPYING" "$STAGE/licenses/json-glib-COPYING.txt"
cp /usr/share/doc/libffi8/copyright "$STAGE/licenses/libffi-copyright.txt"
pcre2_license=$(find "build-glib-$GLIB_VERSION" "glib-$GLIB_VERSION/subprojects" -maxdepth 3 -name 'LICENCE*' 2>/dev/null | head -1)
patchy_gegl_license "${pcre2_license:-the PCRE2 LICENCE of the GLib subproject}" pcre2-LICENCE.txt
{
  echo "Patchy's GEGL bundle ($BUNDLE_PLATFORM-appimage, Ubuntu 22.04 and later), built $(date -u +%Y-%m-%d) by scripts/dev/gegl/build-gegl-appimage.sh"
  echo "GEGL $GEGL_VERSION https://download.gimp.org/pub/gegl/0.4/gegl-$GEGL_VERSION.tar.xz sha256 $GEGL_SHA256"
  echo "babl $BABL_VERSION https://download.gimp.org/pub/babl/0.1/babl-$BABL_VERSION.tar.xz sha256 $BABL_SHA256"
  echo "GLib $GLIB_VERSION https://download.gnome.org/sources/glib/ sha256 $GLIB_SHA256 (PCRE2 from its meson subproject)"
  echo "json-glib $JSON_GLIB_VERSION https://download.gnome.org/sources/json-glib/ sha256 $JSON_GLIB_SHA256"
  for archive in "glib-$GLIB_VERSION"/subprojects/packagecache/*; do
    [ -f "$archive" ] && echo "GLib subproject $(basename "$archive") sha256 $(sha256sum "$archive" | cut -d' ' -f1)"
  done
  echo "libffi: Ubuntu 22.04's libffi8 $(dpkg-query -W -f='${Version}' libffi8) (MIT)"
  patchy_gegl_sources_note
} >"$STAGE/licenses/SOURCES.txt"

mkdir -p "$OUT"
rm -rf "${OUT:?}/lib" "${OUT:?}/share" "${OUT:?}/licenses"
cp -a "$STAGE"/. "$OUT"/
cd "$REPO"
rm -rf "${WORK:?}"
echo "staged $OUT: $(find "$OUT" -type f | wc -l) files, $(du -sh "$OUT" | cut -f1)"
