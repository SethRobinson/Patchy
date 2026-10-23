#!/usr/bin/env bash
# Release verification on the mac build host (docs/release-process.md): the MCP client
# suite against the built connector and against the connector inside the release dmg.
# scripts/release/verify-remote.ps1 -Target mac pipes this script over ssh after the full
# native suites; it can also run on the host by hand:
#   bash scripts/release/verify-mac.sh <version>
set -euo pipefail
VERSION="${1:?usage: verify-mac.sh <version>}"
cd "$HOME/patchy/src"
export QT_QPA_PLATFORM=offscreen
unset PATCHY_UI_TEST_FILTER
PYTHON=.deps/mcp-client-py312/bin/python
DMG="build/package/Patchy-$VERSION.dmg"
[ -f "$DMG" ] || { echo "missing $DMG (run the mac builder first)" >&2; exit 1; }

echo '== NATIVE MCP SUITE =='
nice -n 10 "$PYTHON" tests/mcp_client_tests.py build/mac-release/Patchy.app/Contents/MacOS/patchy-mcp

mount_dir="$PWD/build/verify-dmg-check"
mkdir -p "$mount_dir"
hdiutil attach -readonly -nobrowse -mountpoint "$mount_dir" "$DMG"
trap 'diskutil eject "$mount_dir"' EXIT
echo '== DMG MCP SUITE =='
nice -n 10 "$PYTHON" tests/mcp_client_tests.py "$mount_dir/Patchy.app/Contents/MacOS/patchy-mcp"
echo '== ALL MAC RELEASE CHECKS PASSED =='
# end of script (a comment absorbs a carriage return a Windows pipe may append)
