#!/usr/bin/env bash
# Reports whether the login keychain password stored in ~/.patchy-release-env
# (PATCHY_KEYCHAIN_PASSWORD) is also the account login password, without printing
# either: the stored value is piped to `sudo -S`, which accepts only the login password.
# Quote-free from Windows, so PowerShell and cmd cannot mangle it:
#   ssh seth@studiomac.local bash /Users/seth/patchy/src/packaging/macos/check-keychain-password.sh
set -u
[ -f "$HOME/.patchy-release-env" ] || { echo "ERROR: ~/.patchy-release-env is missing" >&2; exit 2; }
# shellcheck disable=SC1090
source "$HOME/.patchy-release-env"
if [ -z "${PATCHY_KEYCHAIN_PASSWORD:-}" ]; then
  echo "ERROR: PATCHY_KEYCHAIN_PASSWORD is not set in ~/.patchy-release-env" >&2
  exit 2
fi
if printf '%s\n' "$PATCHY_KEYCHAIN_PASSWORD" | sudo -S -k -p '' true 2>/dev/null; then
  echo "MATCH: the login keychain password is the account login password"
  result=0
else
  echo "DIFFERENT: the stored keychain password is not the account login password (or this account cannot sudo)"
  result=1
fi
sudo -k 2>/dev/null
exit $result
