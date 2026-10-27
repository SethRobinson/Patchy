#!/usr/bin/env bash
# Release verification on the linux build host (docs/release-process.md, docs/appimage.md
# "Testing"), after the full native suites:
#   - the MCP client suite on the suites' linux-flatpak-qt build (in its SDK), then inside
#     the installed Flatpak, and
#     tests/flatpak_mcp_tests.py (attachment between separate app and connector sandboxes);
#   - the AppImage: clean-distro containers (test-appimage.sh), the MCP suite through a
#     patchy-mcp symlink (AppRun's symlink dispatch), attachment across two FUSE launches,
#     and both full suites on the AppImage toolchain (make-appimage.sh --tests, which writes
#     no AppImage).
# The Flatpak bundle is installed for this user (`flatpak install --user`), replacing the
# Patchy Flatpak installed there. scripts/release/verify-remote.ps1 -Target linux pipes this
# script over ssh; by hand on the host:
#   bash scripts/release/verify-linux.sh <version>
set -euo pipefail
VERSION="${1:?usage: verify-linux.sh <version>}"
cd "$HOME/patchy/src"
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
unset PATCHY_UI_TEST_FILTER
unset PATCHY_HEADLESS
if [ -d "$HOME/.patchy-tools/sysroot/usr" ]; then
  export LD_LIBRARY_PATH="$HOME/.patchy-tools/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
PYTHON=.deps/mcp-client/bin/python
FLATPAK_BUNDLE="build/package/Patchy-$VERSION.flatpak"
APPIMAGE_FILE="$PWD/build/package/Patchy-$VERSION-x86_64.AppImage"
[ -f "$FLATPAK_BUNDLE" ] || { echo "missing $FLATPAK_BUNDLE (run the linux builder first)" >&2; exit 1; }
[ -x "$APPIMAGE_FILE" ] || { echo "missing $APPIMAGE_FILE (run the linux builder first)" >&2; exit 1; }

echo '== NATIVE MCP SUITE (linux-flatpak-qt, inside its SDK) =='
# The build the full suites just made, run in the SDK it was built in. Not linux-release:
# nothing rebuilds that preset any more, so this suite checked a weeks-old binary until the
# 1.20 settings folder change broke its recent-history test.
MANIFEST=packaging/linux/flatpak/com.rtsoft.patchy.yml
SDK=$(sed -nE 's/^sdk:[[:space:]]*([A-Za-z0-9._-]+).*$/\1/p' "$MANIFEST" | head -1)
SDK_VERSION=$(sed -nE "s/^runtime-version:[[:space:]]*'?([0-9.]+)'?.*$/\1/p" "$MANIFEST" | head -1)
[ -n "$SDK" ] && [ -n "$SDK_VERSION" ] || { echo "could not read the SDK from $MANIFEST" >&2; exit 1; }
[ -x build/linux-flatpak-qt/patchy ] || { echo 'missing build/linux-flatpak-qt/patchy (run the full suites first)' >&2; exit 1; }
nice -n 10 flatpak run --user --filesystem=home --command=bash "$SDK//$SDK_VERSION" -c \
  'cd "$HOME/patchy/src" && .deps/mcp-client-flatpak/bin/python tests/mcp_client_tests.py build/linux-flatpak-qt/patchy-mcp'

if flatpak ps --columns=application | grep -qx com.rtsoft.patchy; then
  echo 'A Patchy Flatpak is running: close it before the installation and attachment tests.' >&2
  exit 1
fi
# flatpak_mcp_tests.py refuses an attachment endpoint it did not create. A leftover from an
# earlier run is reported, never deleted here: it may belong to a Patchy someone is using.
endpoint_dir="$XDG_RUNTIME_DIR/app/com.rtsoft.patchy"
if compgen -G "$endpoint_dir/PatchyMcp-*" >/dev/null; then
  echo "Existing attachment endpoint(s) in $endpoint_dir:" >&2
  ls -la "$endpoint_dir" >&2
  echo 'Check that nothing listens on them (ss -xlp | grep PatchyMcp) and remove them by hand.' >&2
  exit 1
fi
# A rerun finds this same version already installed by the first run, and Flatpak 1.14
# refuses a bundle at the installed version even with --reinstall, so uninstall first
# (the app's data stays; only the installation is replaced).
if flatpak info --user com.rtsoft.patchy >/dev/null 2>&1; then
  flatpak uninstall --user -y --noninteractive com.rtsoft.patchy
fi
flatpak install --user -y "$FLATPAK_BUNDLE"
echo '== FLATPAK MCP SUITE =='
nice -n 10 flatpak run --user --devel --command=bash com.rtsoft.patchy -c 'cd "$HOME/patchy/src" && .deps/mcp-client-flatpak/bin/python tests/mcp_client_tests.py /app/bin/patchy-mcp'
echo '== SEPARATE SANDBOX ATTACHMENT =='
nice -n 10 "$PYTHON" tests/flatpak_mcp_tests.py

echo '== APPIMAGE CLEAN-DISTRO CONTAINERS =='
nice -n 10 bash packaging/linux/appimage/test-appimage.sh "$APPIMAGE_FILE"
echo '== APPIMAGE MCP SUITE (patchy-mcp symlink) =='
# The suite finds the app beside the connector (matching_app), so a wrapper named patchy
# runs the same AppImage.
mkdir -p build/appimage-mcp
ln -sf "$APPIMAGE_FILE" build/appimage-mcp/patchy-mcp
printf '#!/bin/sh\nexec "%s" "$@"\n' "$APPIMAGE_FILE" > build/appimage-mcp/patchy
chmod +x build/appimage-mcp/patchy
nice -n 10 "$PYTHON" tests/mcp_client_tests.py build/appimage-mcp/patchy-mcp
echo '== APPIMAGE ATTACH ACROSS LAUNCHES =='
nice -n 10 "$PYTHON" tests/appimage_mcp_tests.py "$APPIMAGE_FILE"
echo '== APPIMAGE TOOLCHAIN FULL SUITES =='
nice -n 10 bash packaging/linux/make-appimage.sh --tests
echo '== ALL LINUX RELEASE CHECKS PASSED =='
# end of script (a comment absorbs a carriage return a Windows pipe may append)
