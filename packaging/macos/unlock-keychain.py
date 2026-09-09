#!/usr/bin/env python3
"""Unlock an existing signing keychain without UI, retries, or argv secrets."""

import ctypes
import os
from pathlib import Path
import subprocess
import sys


def unlock():
    password = os.environ.pop("PATCHY_KEYCHAIN_PASSWORD", "")
    path = Path("~/Library/Keychains/login.keychain-db").expanduser()
    if not password or not path.is_file():
        raise RuntimeError("The signing keychain and PATCHY_KEYCHAIN_PASSWORD must be configured.")
    security = ctypes.CDLL("/System/Library/Frameworks/Security.framework/Security")
    security.SecKeychainSetUserInteractionAllowed.argtypes = [ctypes.c_ubyte]
    security.SecKeychainOpen.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    security.SecKeychainUnlock.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                         ctypes.c_void_p, ctypes.c_ubyte]
    security.SecKeychainGetStatus.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]

    def check(status, operation):
        if status:
            raise RuntimeError(f"{operation} failed (OSStatus {status}); no password dialog was allowed.")

    check(security.SecKeychainSetUserInteractionAllowed(0), "Disabling keychain UI")
    keychain = ctypes.c_void_p()
    check(security.SecKeychainOpen(os.fsencode(path), ctypes.byref(keychain)), "Opening keychain")
    try:
        encoded = password.encode("utf-8")
        check(security.SecKeychainUnlock(keychain, len(encoded), encoded, 1), "Unlocking keychain")
        status = ctypes.c_uint32()
        check(security.SecKeychainGetStatus(keychain, ctypes.byref(status)), "Checking unlocked status")
        if not status.value & 1:
            raise RuntimeError("The signing keychain is still locked.")
    finally:
        core = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
        core.CFRelease.argtypes = [ctypes.c_void_p]
        core.CFRelease(keychain)


if __name__ == "__main__":
    try:
        if sys.argv[1:] == ["--worker"]:
            unlock()
        else:
            result = subprocess.run([sys.executable, __file__, "--worker"],
                                    stdin=subprocess.DEVNULL, timeout=15)
            sys.exit(result.returncode)
    except subprocess.TimeoutExpired:
        print("ERROR: Noninteractive keychain unlock timed out after 15 seconds; stopped without retry.", file=sys.stderr)
        sys.exit(1)
    except (OSError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
