#!/usr/bin/env bash
# Runs on the mac build host and drives its aarch64 Linux VM (Lima), for
# scripts/remote/release-linux.ps1 (packaging/linux/README.md, "aarch64"). The release script
# copies this file out of the snapshot it pushed, so the host always runs the committed
# version that matches the snapshot.
#
#   linux-arm64-vm.sh build  <limactl> <vm> <snapshot sha> <version> <out dir>
#       Checks the snapshot out in the VM's own work tree (~/patchy/src) from the host's bare
#       repo (~/patchy.git, mounted read-only in the VM at the same path), runs
#       packaging/linux/make-flatpak.sh there without a signing key, and copies the unsigned
#       bundle Patchy-<version>-aarch64.flatpak to <out dir> on the host. The signed
#       repository and the released aarch64 bundle are made on the linux build host, which
#       imports this one.
#   linux-arm64-vm.sh verify <limactl> <vm> <bundle on the host>
#       Installs a released (signed) aarch64 bundle for the VM's user and runs the headless
#       GEGL smoke script and patchy-mcp --check in its sandbox.
#
# Never touches the host's own ~/patchy/src or its build lock: the VM has its own disk.
set -euo pipefail

MODE=${1:?mode: build or verify}
LIMACTL=${2:?limactl path}
LIMACTL=${LIMACTL/#\~/$HOME}
VM=${3:?VM name}
[ -x "$LIMACTL" ] || { echo "ERROR: $LIMACTL is not an executable limactl (packaging/linux/README.md, aarch64 build host)" >&2; exit 1; }

status=$("$LIMACTL" list --format '{{.Status}}' "$VM" 2>/dev/null || true)
case "$status" in
  Running) ;;
  Stopped) echo "== starting VM $VM =="; "$LIMACTL" start --tty=false "$VM" < /dev/null ;;
  *) echo "ERROR: Lima VM '$VM' not found (status '$status'); set it up as packaging/linux/README.md says" >&2; exit 1 ;;
esac
GUEST_HOME=$("$LIMACTL" shell --workdir / "$VM" -- sh -c 'echo $HOME' < /dev/null)
[ -n "$GUEST_HOME" ] || { echo "ERROR: could not read the VM user's home" >&2; exit 1; }

case "$MODE" in
build)
  SHA=${4:?snapshot sha}
  VERSION=${5:?version}
  OUT=${6:?out dir}
  OUT=${OUT/#\~/$HOME}
  # Absolute and checked before the delete below (AGENTS.md, destructive deletes).
  case "$OUT" in /*) ;; *) echo "ERROR: out dir must be absolute: $OUT" >&2; exit 1 ;; esac
  mkdir -p "$OUT"
  rm -f "${OUT:?}"/Patchy-*-aarch64.flatpak
  echo "== aarch64 Flatpak build of ${SHA:0:12} in VM $VM =="
  "$LIMACTL" shell --workdir / "$VM" -- bash -s -- "$HOME/patchy.git" "$SHA" <<'EOF'
set -euo pipefail
BARE=$1
SHA=$2
git config --global --get-all safe.directory 2>/dev/null | grep -qx '\*' || git config --global --add safe.directory '*'
[ -d "$HOME/patchy/src/.git" ] || { mkdir -p "$HOME/patchy"; git init -q "$HOME/patchy/src"; }
cd "$HOME/patchy/src"
git fetch -q "$BARE" refs/snapshots/linux-arm64
[ "$(git rev-parse FETCH_HEAD)" = "$SHA" ] || { echo "ERROR: refs/snapshots/linux-arm64 is not $SHA (another push raced this one?)" >&2; exit 1; }
git checkout -q --force --detach FETCH_HEAD
# The VM is a dedicated build machine: use every core.
PATCHY_FLATPAK_JOBS=$(nproc) bash packaging/linux/make-flatpak.sh < /dev/null
EOF
  "$LIMACTL" copy "$VM:$GUEST_HOME/patchy/src/build/package/Patchy-$VERSION-aarch64.flatpak" "$OUT/" < /dev/null
  [ -s "$OUT/Patchy-$VERSION-aarch64.flatpak" ] || { echo "ERROR: the aarch64 bundle did not arrive in $OUT" >&2; exit 1; }
  echo "aarch64 bundle: $OUT/Patchy-$VERSION-aarch64.flatpak"
  ;;
verify)
  BUNDLE=${4:?bundle path}
  BUNDLE=${BUNDLE/#\~/$HOME}
  [ -s "$BUNDLE" ] || { echo "ERROR: no bundle at $BUNDLE" >&2; exit 1; }
  NAME=$(basename "$BUNDLE")
  "$LIMACTL" copy "$BUNDLE" "$VM:$GUEST_HOME/$NAME" < /dev/null
  echo "== installing $NAME in VM $VM and running its checks =="
  "$LIMACTL" shell --workdir / "$VM" -- bash -s -- "$NAME" <<'EOF'
set -euo pipefail
NAME=$1
cd "$HOME"
flatpak install --user -y --noninteractive --reinstall --bundle "$HOME/$NAME" < /dev/null
flatpak info --user com.rtsoft.patchy | grep -E '^ *(Arch|Version|Origin):'
# The bundle must carry the repository as its origin (packaging/linux/README.md), or its
# installs never update.
origin=$(flatpak info --user --show-origin com.rtsoft.patchy)
flatpak remotes --user --columns=name,url | grep -E "^$origin[[:space:]]+https://" \
  || { echo "ERROR: the installed bundle's origin '$origin' has no repository URL" >&2; exit 1; }
SMOKE=$(mktemp -d "$HOME/.patchy-arm64-verify.XXXXXX")
trap 'case "$SMOKE" in */.patchy-arm64-verify.?*) rm -rf "$SMOKE" ;; esac' EXIT
mkdir -p "$SMOKE/settings"
cp "$HOME/patchy/src/packaging/gegl-smoke.js" "$SMOKE/smoke.js"
timeout 300 flatpak run --user --env=PATCHY_SETTINGS_DIR="$SMOKE/settings" --env=PATCHY_NO_SOUND=1 \
  com.rtsoft.patchy --headless --run-script "$SMOKE/smoke.js" --script-output "$SMOKE/out.txt" \
  > "$SMOKE/console.txt" 2>&1 < /dev/null || { cat "$SMOKE/console.txt" >&2; exit 1; }
[ "$(tail -n 1 "$SMOKE/out.txt")" = "[done]" ] || { cat "$SMOKE/out.txt" >&2; exit 1; }
cat "$SMOKE/out.txt"
timeout 120 flatpak run --user --command=patchy-mcp com.rtsoft.patchy --check < /dev/null
echo "aarch64 bundle checks passed"
EOF
  ;;
*)
  echo "ERROR: unknown mode '$MODE' (build or verify)" >&2
  exit 1
  ;;
esac
