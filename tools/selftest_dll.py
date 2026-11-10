"""Load a built xinput1_4.dll proxy in a throwaway process and verify it.

Usage:
    python tools/selftest_dll.py build/xinput1_4.dll

Checks, without starting the game:
  * the DLL loads at all (no missing imports / bad image),
  * ELDEN RING's imports resolve: ordinals 2 and 3,
  * the forwarded XInputGetState call works (returns 0 or 1167),
  * a log file was produced next to the DLL.
"""

import ctypes
import os
import sys

ERROR_DEVICE_NOT_CONNECTED = 1167

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.LoadLibraryW.restype = ctypes.c_void_p
kernel32.LoadLibraryW.argtypes = [ctypes.c_wchar_p]
kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
kernel32.GetProcAddress.restype = ctypes.c_void_p
kernel32.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_void_p]


def main() -> int:
    path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/xinput1_4.dll")
    if not os.path.exists(path):
        print("missing file:", path)
        return 2

    print("file :", path)
    handle = kernel32.LoadLibraryW(path)
    if not handle:
        print("LoadLibraryW failed, error", ctypes.get_last_error())
        return 1
    print("load : ok")

    expected = {
        2: "XInputGetState",
        3: "XInputSetState",
        4: "XInputGetCapabilities",
        5: "XInputEnable",
        7: "XInputGetBatteryInformation",
        8: "XInputGetKeystroke",
        10: "XInputGetAudioDeviceIds",
        100: "XInputGetStateEx",
    }
    ok = True
    for ordinal, name in expected.items():
        address = kernel32.GetProcAddress(handle, ctypes.c_void_p(ordinal))
        status = "ok" if address else "MISSING"
        if not address:
            ok = False
        print("  #%-4d %-28s %s" % (ordinal, name, status))

    if ok:
        get_state = ctypes.WINFUNCTYPE(
            ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p
        )(kernel32.GetProcAddress(handle, ctypes.c_void_p(2)))
        state = ctypes.create_string_buffer(16)
        result = get_state(0, ctypes.cast(state, ctypes.c_void_p))
        if result == 0:
            print("call : XInputGetState(0) = 0 (controller connected)")
        elif result == ERROR_DEVICE_NOT_CONNECTED:
            print("call : XInputGetState(0) = 1167 (no controller, forwarding works)")
        else:
            print("call : XInputGetState(0) =", result, "(unexpected)")
            ok = False

    log_path = os.path.join(os.path.dirname(path), "SprintSplit", "SprintSplit.log")
    if os.path.exists(log_path):
        print("log  :", log_path)
        with open(log_path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh.readlines()[-6:]:
                print("      ", line.rstrip())
    else:
        print("log  : not created (check writability of the folder)")

    kernel32.FreeLibrary(handle)
    print("RESULT:", "OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
