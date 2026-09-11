#!/usr/bin/env bash
# One-shot recovery for the desktop keychain password dialogs ("com.apple.iCloudHelper
# wants to use the login keychain") that follow a securityd restart on the build mac.
#
# An ssh session cannot see or change the desktop session's keychain state, so this
# bootstraps a helper INTO the desktop (gui/<uid>) launchd domain. The helper reads the
# login keychain status there and, only if it is locked, unlocks it once with
# PATCHY_KEYCHAIN_PASSWORD from ~/.patchy-release-env with Security.framework UI
# disabled. No retries, no password or settings changes, and the helper is removed
# afterwards. iCloudHelper's next retry then succeeds silently. Run from Windows:
#
#   ssh seth@studiomac.local 'bash ~/patchy/src/packaging/macos/desktop-keychain-unlock.sh'
#
# Prints one JSON line; status_before/status_after are SecKeychainGetStatus bits
# (1 unlocked, 2 readable, 4 writable). Exit 1 when the keychain is still locked.
# See README.md, "Diagnosing a keychain password dialog on the desktop".
set -euo pipefail

[ -f "$HOME/.patchy-release-env" ] || { echo "ERROR: ~/.patchy-release-env is missing" >&2; exit 1; }

LABEL=com.rtsoft.patchy.desktop-keychain-unlock
WORK=$(mktemp -d /tmp/patchy-desktop-kc.XXXXXX)
HELPER="$WORK/helper.py"
PLIST="$WORK/$LABEL.plist"
OUT="$WORK/result.json"
ERR="$WORK/helper.err"
GUI_DOMAIN="gui/$(id -u)"
cleanup() {
  launchctl bootout "$GUI_DOMAIN/$LABEL" 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

cat > "$HELPER" <<'PY'
import ctypes, json, os
from pathlib import Path

out = {}
password = os.environ.pop("PATCHY_KEYCHAIN_PASSWORD", "")
sec = ctypes.CDLL("/System/Library/Frameworks/Security.framework/Security")
sec.SecKeychainSetUserInteractionAllowed.argtypes = [ctypes.c_ubyte]
sec.SecKeychainOpen.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
sec.SecKeychainUnlock.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_ubyte]
sec.SecKeychainGetStatus.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
out["ui_disabled"] = sec.SecKeychainSetUserInteractionAllowed(0)
keychain = ctypes.c_void_p()
path = Path("~/Library/Keychains/login.keychain-db").expanduser()
out["open"] = sec.SecKeychainOpen(os.fsencode(path), ctypes.byref(keychain))
status = ctypes.c_uint32()
out["status_rc"] = sec.SecKeychainGetStatus(keychain, ctypes.byref(status))
out["status_before"] = status.value
if not (status.value & 1):
    if password:
        encoded = password.encode("utf-8")
        out["unlock_rc"] = sec.SecKeychainUnlock(keychain, len(encoded), encoded, 1)
        sec.SecKeychainGetStatus(keychain, ctypes.byref(status))
    else:
        out["unlock_rc"] = "skipped: no PATCHY_KEYCHAIN_PASSWORD"
out["status_after"] = status.value
print(json.dumps(out))
PY

cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$LABEL</string>
<key>ProgramArguments</key><array>
<string>/bin/zsh</string><string>-c</string>
<string>source ~/.patchy-release-env 2>/dev/null; exec /usr/bin/python3 $HELPER</string>
</array>
<key>RunAtLoad</key><true/>
<key>StandardOutPath</key><string>$OUT</string>
<key>StandardErrorPath</key><string>$ERR</string>
</dict></plist>
PL

launchctl bootout "$GUI_DOMAIN/$LABEL" 2>/dev/null || true
launchctl bootstrap "$GUI_DOMAIN" "$PLIST"
for _ in $(seq 1 15); do
  [ -s "$OUT" ] && break
  sleep 1
done
if [ ! -s "$OUT" ]; then
  echo "ERROR: the desktop-session helper produced no result within 15 seconds" >&2
  [ -s "$ERR" ] && cat "$ERR" >&2
  exit 1
fi
cat "$OUT"
[ -s "$ERR" ] && cat "$ERR" >&2
grep -q '"status_after": [1357]' "$OUT" || { echo "ERROR: the desktop login keychain is still locked" >&2; exit 1; }
