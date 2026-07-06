#!/usr/bin/env bash
# One-time (idempotent) provisioning of an Ubuntu/Debian machine for Patchy remote builds.
# Run from the Windows box:
#   scp scripts/remote/setup-linux.sh glados@glados.local:
#   ssh glados@glados.local 'bash setup-linux.sh'
# Sudo-free where possible: pip is bootstrapped with get-pip.py --user, ninja is the
# static release binary, and Qt installs via aqtinstall into the work tree's .deps/Qt so
# the linux-* CMake presets resolve it exactly like on Windows. Only the apt package
# step needs sudo; without it the script prints the command to run manually.
set -euo pipefail

TOOLS="$HOME/.patchy-tools"
BARE="$HOME/patchy.git"
SRC="$HOME/patchy/src"
QT_VER="6.8.3"
NINJA_VERSION="1.12.1"

echo "== Patchy linux setup =="

# Headers Qt6Gui's CMake config requires (OpenGL, xkbcommon), the offscreen platform's
# runtime deps (fontconfig/freetype/dbus), the xcb set + xvfb for windowed smoke tests,
# and the metric-compatible test fonts (see tests/test_fonts.hpp).
APT_PKGS=(
  libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0
  libfontconfig1 libfreetype6 libdbus-1-3
  libxcb1 libx11-xcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1
  libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-shm0 libxcb-sync1
  libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 libxcb-glx0 libxcb-util1
  libsm6 libice6 libxext6 libxrender1
  xvfb x11-apps
  fonts-liberation fonts-dejavu-core
)
if sudo -n true 2>/dev/null; then
  sudo apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${APT_PKGS[@]}"
else
  echo "WARNING: passwordless sudo unavailable; install the apt packages manually:"
  echo "  sudo apt-get install -y ${APT_PKGS[*]}"
  # Sudo-free fallback for the BUILD deps: extract the OpenGL dev/runtime packages into a
  # user sysroot that build-and-test.sh adds to CMAKE_PREFIX_PATH / LD_LIBRARY_PATH.
  # (Windowed xcb smoke tests and the extra fonts still want the real apt install.)
  if [ ! -e "$HOME/.patchy-tools/sysroot/usr/include/GL/gl.h" ]; then
    mkdir -p "$HOME/.patchy-tools/sysroot" "$HOME/aptdl"
    (cd "$HOME/aptdl" \
     && apt-get download libgl-dev libglx-dev libopengl-dev libegl-dev libglvnd-dev \
                         mesa-common-dev libgl1 libglx0 libopengl0 libegl1 \
     && for f in *.deb; do dpkg-deb -x "$f" "$HOME/.patchy-tools/sysroot"; done)
  fi
fi

mkdir -p "$TOOLS/bin"

if [ ! -x "$HOME/.local/bin/pip" ]; then
  wget -qO /tmp/get-pip.py https://bootstrap.pypa.io/get-pip.py
  python3 /tmp/get-pip.py --user --break-system-packages -q
  rm -f /tmp/get-pip.py
fi
"$HOME/.local/bin/pip" install --user --break-system-packages -q --upgrade aqtinstall

if [ ! -x "$TOOLS/bin/ninja" ]; then
  wget -q "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip" -O /tmp/ninja-linux.zip
  python3 -c "import zipfile; zipfile.ZipFile('/tmp/ninja-linux.zip').extractall('/tmp/ninja-extract')"
  mv /tmp/ninja-extract/ninja "$TOOLS/bin/ninja"
  chmod +x "$TOOLS/bin/ninja"
  rm -rf /tmp/ninja-linux.zip /tmp/ninja-extract
fi

[ -d "$BARE" ] || git init --bare "$BARE"
mkdir -p "$(dirname "$SRC")"
[ -d "$SRC/.git" ] || git clone "$BARE" "$SRC"

QT_DIR="$SRC/.deps/Qt/$QT_VER/gcc_64"
if [ ! -d "$QT_DIR" ]; then
  echo "== Installing Qt $QT_VER into $SRC/.deps/Qt (downloads ~1 GB) =="
  "$HOME/.local/bin/aqt" install-qt linux desktop "$QT_VER" linux_gcc_64 -m qtimageformats -O "$SRC/.deps/Qt"
fi

echo "== Versions =="
cmake --version | head -1
printf 'ninja %s\n' "$("$TOOLS/bin/ninja" --version)"
g++ --version | head -1
[ -x "$QT_DIR/bin/qmake" ] && echo "Qt: $QT_DIR OK"
echo "== linux setup complete =="
