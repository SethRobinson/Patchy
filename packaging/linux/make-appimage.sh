#!/usr/bin/env bash
# Builds the Linux AppImage in build/package (docs/appimage.md):
#   Patchy-<version>-x86_64.AppImage          published as PatchyLinux.AppImage
#   Patchy-<version>-x86_64.AppImage.zsync    published as PatchyLinux.AppImage.zsync
# ARCH=aarch64 builds the arm64 AppImage in an arm64 container instead (natively on an arm64
# host, through qemu-user-static elsewhere):
#   Patchy-<version>-aarch64.AppImage         published as PatchyLinux-aarch64.AppImage
# Everything compiles inside an Ubuntu 22.04 container (appimage/Containerfile), so the
# AppImage runs on 22.04 and every newer distro; the container's build half is
# appimage/build-in-container.sh. Afterwards the AppImage is smoke-checked here on the host
# through a real FUSE mount.
#   make-appimage.sh            build the AppImage
#   make-appimage.sh --tests [filter]
#                               build the tests on the AppImage toolchain (gcc-12, Qt 6.8.3)
#                               and run both suites offscreen; no AppImage is written
# Prerequisite (one-time, needs sudo): sudo apt-get install -y podman uidmap
# The image is built on first use and rebuilt whenever the Containerfile or
# scripts/setup-ai-deps.sh changes (its tag is their hash).
set -euo pipefail
cd "$(dirname "$0")"

# Absolute and checked: the rm lines below build on it (AGENTS.md, destructive deletes).
ROOT=$(cd ../.. && pwd)
[ -n "$ROOT" ] && [ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: could not find the repository root" >&2; exit 1; }
command -v podman >/dev/null || { echo "ERROR: podman is not installed (sudo apt-get install -y podman uidmap)" >&2; exit 1; }

MODE=package
FILTER=""
if [ "${1:-}" = "--tests" ]; then
  MODE=tests
  FILTER="${2:-}"
fi

VERSION=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9.]+).*$/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "ERROR: could not read the project version from CMakeLists.txt" >&2; exit 1; }

# Every core but four (at least one), so the build host stays usable.
JOBS=$(( $(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 5) - 4 ))
[ "$JOBS" -ge 1 ] || JOBS=1

ARCH="${ARCH:-x86_64}"
case "$ARCH" in
  x86_64) PODMAN_ARCH=amd64; IMAGE_SUFFIX="" ;;
  aarch64) PODMAN_ARCH=arm64; IMAGE_SUFFIX="-aarch64" ;;
  *) echo "ERROR: ARCH must be x86_64 or aarch64, not $ARCH" >&2; exit 1 ;;
esac

IMAGE_HASH=$(cat appimage/Containerfile "$ROOT/scripts/setup-ai-deps.sh" | sha256sum | cut -c1-16)
IMAGE="localhost/patchy-appimage-build$IMAGE_SUFFIX:$IMAGE_HASH"
if ! podman image exists "$IMAGE"; then
  echo "== building the container image $IMAGE =="
  CONTEXT=$(mktemp -d "${TMPDIR:-/tmp}/patchy-appimage-context.XXXXXX")
  trap 'case "$CONTEXT" in */patchy-appimage-context.?*) rm -rf "$CONTEXT" ;; esac' EXIT
  cp appimage/Containerfile "$ROOT/scripts/setup-ai-deps.sh" "$CONTEXT/"
  nice -n 10 podman build --arch "$PODMAN_ARCH" --build-arg ARCH="$ARCH" --tag "$IMAGE" \
    --file "$CONTEXT/Containerfile" "$CONTEXT"
fi

if [ "$MODE" = "package" ]; then
  mkdir -p "$ROOT/build/package"
  # Delete every previous AppImage of this architecture up front (not just this version's):
  # if the build fails,
  # nothing stale remains for the release scripts to pick up by accident.
  rm -f "${ROOT:?}/build/package"/Patchy-*-"$ARCH".AppImage "${ROOT:?}/build/package"/Patchy-*-"$ARCH".AppImage.zsync
fi

# --userns=keep-id: files the build writes belong to this user, as in a host build.
# The source is mounted read-write because the test suites write into it (test-artifacts).
# The test suites also see this host's fonts, read-only, where the container's fontconfig
# looks (/usr/local/share/fonts): several text tests depend on the installed font set, and
# the Flatpak SDK runs see the host's fonts the same way (docs/linux-testing.md).
FONT_ARGS=()
if [ "$MODE" = "tests" ] && [ -d /usr/share/fonts ]; then
  FONT_ARGS=(--volume /usr/share/fonts:/usr/local/share/fonts:ro)
fi
echo "== container build ($MODE) =="
podman run --rm --arch "$PODMAN_ARCH" --userns=keep-id --security-opt label=disable \
  --volume "$ROOT:/src" "${FONT_ARGS[@]}" --env JOBS="$JOBS" --workdir /src \
  "$IMAGE" nice -n 10 bash /src/packaging/linux/appimage/build-in-container.sh "$MODE" \
  "$( [ "$MODE" = package ] && echo "$VERSION" || echo "$FILTER" )"

[ "$MODE" = "package" ] || exit 0

APPIMAGE="$ROOT/build/package/Patchy-$VERSION-$ARCH.AppImage"
[ -s "$APPIMAGE" ] || { echo "ERROR: the container did not write $APPIMAGE" >&2; exit 1; }
chmod +x "$APPIMAGE"

# The container validated the desktop file with 22.04's desktop-file-validate (what the
# AppImage catalog runs, GitHub issue 208); check it and the metainfo again with this
# host's newer tools when they are installed.
APPDIR="$ROOT/build/appimage-appdir"
if command -v desktop-file-validate >/dev/null; then
  desktop-file-validate "$APPDIR/com.rtsoft.patchy.desktop" \
    || { echo "ERROR: the AppImage's desktop file fails desktop-file-validate" >&2; exit 1; }
fi
if command -v appstreamcli >/dev/null; then
  appstreamcli validate --no-net "$APPDIR/usr/share/metainfo/com.rtsoft.patchy.metainfo.xml" \
    || { echo "ERROR: the AppImage's metainfo fails appstreamcli validate" >&2; exit 1; }
fi

# The same checks as inside the container, now through the AppImage runtime's own FUSE
# mount on this (newer) distro: what a user's double-click does. A foreign-architecture
# AppImage cannot mount on this host; the container's extracted run already checked it.
if [ "$(uname -m)" != "$ARCH" ]; then
  echo "Host smoke check skipped: $ARCH AppImage on a $(uname -m) host."
  echo "AppImage written: $APPIMAGE"
  exit 0
fi
echo "== host smoke check (FUSE mount) =="
SMOKE=$(mktemp -d "${TMPDIR:-/tmp}/patchy-appimage-smoke.XXXXXX")
mkdir -p "$SMOKE/settings"
echo 'console.log("headless smoke")' > "$SMOKE/smoke.js"
smoke_status=0
timeout 180 env -u APPIMAGE_EXTRACT_AND_RUN PATCHY_SETTINGS_DIR="$SMOKE/settings" PATCHY_NO_SOUND=1 \
  "$APPIMAGE" --headless --run-script "$SMOKE/smoke.js" --script-output "$SMOKE/out.txt" \
  > "$SMOKE/console.txt" 2>&1 || smoke_status=$?
if [ "$smoke_status" != "0" ] || [ "$(tail -n 1 "$SMOKE/out.txt" 2>/dev/null)" != "[done]" ]; then
  echo "ERROR: host smoke check failed (exit $smoke_status):" >&2
  cat "$SMOKE/console.txt" "$SMOKE/out.txt" >&2 2>/dev/null || true
  exit 1
fi
timeout 180 "$APPIMAGE" patchy-mcp --check
case "$SMOKE" in */patchy-appimage-smoke.?*) rm -rf "$SMOKE" ;; esac
echo "Host smoke check passed."
echo "AppImage written: $APPIMAGE"
