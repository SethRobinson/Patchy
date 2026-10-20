#!/usr/bin/env bash
# Fetches the Filter Foundry test plug-ins (docs/plugins.md) into the untracked local fixture
# tree, local-test-fixtures/photoshop-plugins/filter-foundry. The four standalone .8bf files
# are GPL binaries the repository does not carry; each download is verified against the
# SHA-256 recorded here and in test-fixtures/photoshop-plugins/README.md, and the author's
# .afs expression files come along as their source. Tests that need the plug-ins skip while
# the folder is missing. The PiPL reader tests run them on every platform; running them
# needs Windows.
#
#   scripts/fetch-test-plugins.sh [destination-dir]
set -euo pipefail

if [[ $# -ge 1 && -n "$1" ]]; then
  DESTINATION_DIR="$1"
else
  REPO="$(cd "$(dirname "$0")/.." && pwd)"
  DESTINATION_DIR="$REPO/local-test-fixtures/photoshop-plugins/filter-foundry"
fi
mkdir -p "$DESTINATION_DIR"

BASE_URL="https://misc.daniel-marschall.de/photoshop_filters/"
FILES=(
  "Greyscale.8bf|ab0533c931ddb49093f09b5f80cc5e02b007c36b311572db19bb780a0a04fd6d"
  "Greyscale64.8bf|301a1c7766667c3bff4d83b123341f58c95acc16ce504b9b8f5e47098149e6e0"
  "White to Transparent.8bf|f5bf415279d3a645bcd32848bf7b07d05e247877e4ad46e6b3d0602b218f6625"
  "White to Transparent64.8bf|263e4b30f2ac1934fbd1c04572bbbd4120b9201a295281d9100b76ae03a8fa56"
  "Greyscale.afs|f08d9829f9bcaef1214390fc6ad20f87b6d0953b6cd3252b5090350474d97b35"
  "White to Transparent.afs|56b6d8c9eb4064d319e656cbd9aef6d11cbfa8e052090ed4a729540dff6c87dc"
)

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# Downloads to a .part sibling, verifies, then renames, so an interrupted run never leaves a
# file a later run would mistake for a verified one.
fetch_verified() {
  local url="$1" destination="$2" expected="$3"
  if [[ -f "$destination" ]]; then
    [[ "$(sha256_of "$destination")" == "$expected" ]] && return 0
    echo "checksum mismatch, downloading again: $destination"
    rm -f "$destination"
  fi
  echo "downloading $url"
  curl -fL --retry 3 -o "$destination.part" "$url"
  local actual
  actual="$(sha256_of "$destination.part")"
  if [[ "$actual" != "$expected" ]]; then
    rm -f "$destination.part"
    echo "SHA-256 mismatch for $url (expected $expected, got $actual)" >&2
    exit 1
  fi
  mv "$destination.part" "$destination"
}

for entry in "${FILES[@]}"; do
  name="${entry%%|*}"
  expected="${entry##*|}"
  fetch_verified "${BASE_URL}${name// /%20}" "$DESTINATION_DIR/$name" "$expected"
done
echo "Filter Foundry test plug-ins ready in $DESTINATION_DIR"
