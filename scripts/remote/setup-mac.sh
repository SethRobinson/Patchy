#!/usr/bin/env bash
# One-time (idempotent) provisioning of a macOS machine for Patchy remote builds.
# Run from the Windows box:
#   scp scripts/remote/setup-mac.sh seth@studiomac.local:
#   ssh seth@studiomac.local 'bash setup-mac.sh'
# Installs pip tools (cmake/ninja/aqtinstall) into a venv, creates the bare repo +
# work tree used by scripts/remote/remote-build.ps1, and installs Qt into the work
# tree's .deps/Qt so the mac-* CMake presets resolve it exactly like on Windows.
set -euo pipefail

TOOLS="$HOME/.patchy-tools"
BARE="$HOME/patchy.git"
SRC="$HOME/patchy/src"
QT_VER="6.8.3"

echo "== Patchy mac setup =="
xcode-select -p >/dev/null || { echo "ERROR: Xcode command line tools not configured"; exit 1; }

if [ ! -d "$TOOLS" ]; then
  python3 -m venv "$TOOLS"
fi
"$TOOLS/bin/pip" install --quiet --upgrade pip
"$TOOLS/bin/pip" install --quiet --upgrade aqtinstall cmake ninja

[ -d "$BARE" ] || git init --bare "$BARE"
mkdir -p "$(dirname "$SRC")"
[ -d "$SRC/.git" ] || git clone "$BARE" "$SRC"

QT_DIR="$SRC/.deps/Qt/$QT_VER/macos"
if [ ! -d "$QT_DIR" ]; then
  echo "== Installing Qt $QT_VER into $SRC/.deps/Qt (downloads ~1 GB) =="
  "$TOOLS/bin/aqt" install-qt mac desktop "$QT_VER" -m qtimageformats -O "$SRC/.deps/Qt"
fi

echo "== Versions =="
"$TOOLS/bin/cmake" --version | head -1
printf 'ninja %s\n' "$("$TOOLS/bin/ninja" --version)"
[ -x "$QT_DIR/bin/qmake" ] && echo "Qt: $QT_DIR OK"
echo "== mac setup complete =="
