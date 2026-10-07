#!/usr/bin/env bash
# Builds the Linux Flatpak artifacts in build/package:
#   Patchy-<version>.flatpak               the single-file bundle
#   Patchy-<version>-flatpak-repo.tar      the signed repository users update from
# Prerequisites (one-time):
#   sudo apt-get install -y flatpak flatpak-builder ostree
#   flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
#   flatpak install --user -y flathub org.kde.Platform//6.11 org.kde.Sdk//6.11
# The repository needs a GPG signing key (one-time setup in README.md here), named by
# PATCHY_FLATPAK_GPG_KEY (PATCHY_FLATPAK_GPG_HOMEDIR for a keyring outside ~/.gnupg).
# Without a key only an update-less bundle is built; PATCHY_REQUIRE_FLATPAK_REPO=1, which
# scripts/remote/release-linux.ps1 sets, turns that into an error so a release can never
# ship a bundle whose installs do not update.
# PATCHY_FLATPAK_REPO_URL overrides the published repository URL (local testing).
set -euo pipefail
cd "$(dirname "$0")"

APP_ID=com.rtsoft.patchy
# Absolute and checked: the rm -rf lines below build on it (AGENTS.md, destructive deletes).
ROOT=$(cd ../.. && pwd)
[ -n "$ROOT" ] && [ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: could not find the repository root" >&2; exit 1; }
BUILD_DIR="$ROOT/build/flatpak"
REPO_DIR="$ROOT/build/flatpak-repo"
PACKAGE_DIR="$ROOT/build/package"
PUBLISH_DIR="$ROOT/build/flatpak-publish"
REPO_URL="${PATCHY_FLATPAK_REPO_URL:-https://rtsoft.com/flatpak/repo/}"
FLATHUB_REPO=https://dl.flathub.org/repo/flathub.flatpakrepo
GPG_KEY="${PATCHY_FLATPAK_GPG_KEY:-}"
GPG_HOMEDIR_ARGS=()
GPG_CLI_ARGS=()
if [ -n "${PATCHY_FLATPAK_GPG_HOMEDIR:-}" ]; then
  GPG_HOMEDIR_ARGS=(--gpg-homedir="$PATCHY_FLATPAK_GPG_HOMEDIR")
  GPG_CLI_ARGS=(--homedir "$PATCHY_FLATPAK_GPG_HOMEDIR")
fi
if [ -z "$GPG_KEY" ] && [ "${PATCHY_REQUIRE_FLATPAK_REPO:-0}" = "1" ]; then
  echo "ERROR: PATCHY_REQUIRE_FLATPAK_REPO=1 but PATCHY_FLATPAK_GPG_KEY is not set." >&2
  echo "Create the signing key and ~/.patchy-release-env (packaging/linux/README.md)." >&2
  exit 1
fi

VERSION=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9.]+).*$/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "ERROR: could not read the project version from CMakeLists.txt"; exit 1; }

mkdir -p "$PACKAGE_DIR"
# Every core but four (at least one), so the remote machine stays usable.
JOBS=$(( $(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 5) - 4 ))
[ "$JOBS" -ge 1 ] || JOBS=1
# Delete ALL previous bundles up front (not just this version's): if the build fails,
# nothing stale remains for the newest-file upload script to pick up by accident.
rm -f "${ROOT:?}/build/package"/Patchy-*.flatpak "${ROOT:?}/build/package"/Patchy-*-flatpak-repo.tar
nice -n 10 flatpak-builder --jobs="$JOBS" --force-clean --repo="$REPO_DIR" "$BUILD_DIR" "flatpak/$APP_ID.yml"

# Proves the sandboxed app runs with no display before any bundle exists. The Qt
# offscreen platform comes from the org.kde.Platform runtime, not from this repo, so
# this is the one check that the runtime still ships it and that --headless works
# inside the sandbox. flatpak-builder --run uses the build directory the bundle is
# exported from, so nothing is installed on the build machine. --headless never
# forwards to a running Patchy; PATCHY_SETTINGS_DIR keeps the run out of the real
# settings; the temp directory lives under $HOME because that is what the sandbox
# can see. The same check runs in the Windows and macOS packagers.
echo "== headless smoke check (the sandboxed app must run with no display) =="
SMOKE=$(mktemp -d "$HOME/.patchy-flatpak-smoke.XXXXXX")
trap 'case "$SMOKE" in */.patchy-flatpak-smoke.?*) rm -rf "$SMOKE" ;; esac' EXIT
mkdir -p "$SMOKE/settings"
echo 'console.log("headless smoke")' > "$SMOKE/smoke.js"
smoke_status=0
timeout 180 flatpak-builder --run "$BUILD_DIR" "flatpak/$APP_ID.yml" \
  env PATCHY_SETTINGS_DIR="$SMOKE/settings" PATCHY_NO_SOUND=1 \
  patchy --headless --run-script "$SMOKE/smoke.js" --script-output "$SMOKE/smoke-output.txt" \
  > "$SMOKE/smoke-console.txt" 2>&1 || smoke_status=$?
if [ "$smoke_status" != "0" ]; then
  echo "ERROR: headless smoke check failed (exit $smoke_status). Console output:" >&2
  cat "$SMOKE/smoke-console.txt" >&2
  exit 1
fi
if [ "$(tail -n 1 "$SMOKE/smoke-output.txt" 2>/dev/null)" != "[done]" ]; then
  echo "ERROR: headless smoke check output did not end with [done]:" >&2
  cat "$SMOKE/smoke-output.txt" >&2 2>/dev/null || true
  exit 1
fi
echo "Headless smoke check passed."
timeout 180 flatpak-builder --run "$BUILD_DIR" "flatpak/$APP_ID.yml" patchy-mcp --check

# --runtime-repo records where org.kde.Platform lives in the bundle metadata. A
# single-file bundle has no origin remote, so without it `flatpak install` on a machine
# with no Flathub remote fails with "requires the runtime ... which was not found"
# (GitHub issue 14, CachyOS with no preconfigured remotes).
if [ -z "$GPG_KEY" ]; then
  echo "NOTE: PATCHY_FLATPAK_GPG_KEY is not set: no repository is built, and installs of" >&2
  echo "this bundle will not update through flatpak. Not a release artifact." >&2
  flatpak build-bundle --runtime-repo="$FLATHUB_REPO" \
    "$REPO_DIR" "$PACKAGE_DIR/Patchy-$VERSION.flatpak" "$APP_ID"
  echo "Bundle written: $PACKAGE_DIR/Patchy-$VERSION.flatpak"
  exit 0
fi

# The repository users update from (GitHub issue 28). $REPO_DIR collects every local
# build, so the published repository is made fresh from the one commit just built: a
# signed copy of the app ref only (no Debug ref), a from-scratch static delta so a first
# install is a few large downloads instead of thousands of small ones, and the two
# files users point flatpak at. The bundle is exported from this same signed commit
# with --repo-url, so a bundle install gets the repository as its origin and updates
# from it like a repository install does.
ARCH=$(flatpak --default-arch)
APP_REF="app/$APP_ID/$ARCH/master"
rm -rf "${ROOT:?}/build/flatpak-publish"
mkdir -p "$PUBLISH_DIR"
ostree init --repo="$PUBLISH_DIR/repo" --mode=archive-z2
flatpak build-commit-from --src-repo="$REPO_DIR" --gpg-sign="$GPG_KEY" "${GPG_HOMEDIR_ARGS[@]}" \
  --no-update-summary "$PUBLISH_DIR/repo" "$APP_REF"
flatpak build-update-repo --title="Patchy" --homepage=https://github.com/SethRobinson/Patchy \
  --default-branch=master --gpg-sign="$GPG_KEY" "${GPG_HOMEDIR_ARGS[@]}" \
  --generate-static-deltas "$PUBLISH_DIR/repo"

gpg "${GPG_CLI_ARGS[@]}" --export "$GPG_KEY" > "$PUBLISH_DIR/patchy.gpg"
[ -s "$PUBLISH_DIR/patchy.gpg" ] || { echo "ERROR: could not export the public key $GPG_KEY" >&2; exit 1; }
GPG_KEY_BASE64=$(base64 -w0 "$PUBLISH_DIR/patchy.gpg")
cat > "$PUBLISH_DIR/patchy.flatpakrepo" <<EOF
[Flatpak Repo]
Title=Patchy
Comment=Patchy image editor releases
Homepage=https://github.com/SethRobinson/Patchy
Url=$REPO_URL
DefaultBranch=master
GPGKey=$GPG_KEY_BASE64
EOF
cat > "$PUBLISH_DIR/$APP_ID.flatpakref" <<EOF
[Flatpak Ref]
Name=$APP_ID
Title=Patchy
Branch=master
IsRuntime=false
Url=$REPO_URL
SuggestRemoteName=patchy
Homepage=https://github.com/SethRobinson/Patchy
RuntimeRepo=$FLATHUB_REPO
GPGKey=$GPG_KEY_BASE64
EOF

flatpak build-bundle --runtime-repo="$FLATHUB_REPO" --repo-url="$REPO_URL" \
  --gpg-keys="$PUBLISH_DIR/patchy.gpg" \
  "$PUBLISH_DIR/repo" "$PACKAGE_DIR/Patchy-$VERSION.flatpak" "$APP_ID"
echo "Bundle written: $PACKAGE_DIR/Patchy-$VERSION.flatpak"

# One tar for the upload script: the repo directory plus the two descriptor files.
tar -C "$PUBLISH_DIR" -cf "$PACKAGE_DIR/Patchy-$VERSION-flatpak-repo.tar" \
  repo patchy.flatpakrepo "$APP_ID.flatpakref"
echo "Repository written: $PACKAGE_DIR/Patchy-$VERSION-flatpak-repo.tar"
