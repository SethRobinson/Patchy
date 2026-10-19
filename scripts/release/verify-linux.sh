#!/usr/bin/env bash
# Release verification on the linux build host (docs/release-process.md, docs/appimage.md
# "Testing"), after the full native suites:
#   - the MCP client suite natively, then inside the Flatpak sandbox, and
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

echo '== NATIVE MCP SUITE =='
nice -n 10 "$PYTHON" tests/mcp_client_tests.py build/linux-release/patchy-mcp

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
# --reinstall: a rerun finds this same version already installed by the first run.
flatpak install --user -y --reinstall "$FLATPAK_BUNDLE"
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
