"""Check that a built SprintSplit DLL exports the ordinals ELDEN RING imports.

Usage:
    python tools/check_exports.py build/xinput1_4.dll

Exits non-zero when a required ordinal is missing, which is exactly the failure
mode that makes the game die at startup with "entry point not found".
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_imports import PE  # noqa: E402

# Ordinals ELDEN RING actually imports, plus the rest of the system table.
REQUIRED = {
    2: "XInputGetState",
    3: "XInputSetState",
    4: "XInputGetCapabilities",
    5: "XInputEnable",
    7: "XInputGetBatteryInformation",
    8: "XInputGetKeystroke",
    10: "XInputGetAudioDeviceIds",
    100: "XInputGetStateEx",
}
OPTIONAL = {101, 102, 103}


def read_exports(path):
    with open(path, "rb") as fh:
        pe = PE(fh.read())

    rva, _size = pe.directory(0)
    if not rva:
        return {}

    off = pe.rva_to_off(rva)
    (_flags, _ts, _major, _minor, _name_rva, base, nfuncs, nnames,
     funcs_rva, names_rva, ords_rva) = struct.unpack_from("<IIHHIIIIIII", pe.data, off)

    names = {}
    for i in range(nnames):
        name = pe.cstr(struct.unpack_from("<I", pe.data, pe.rva_to_off(names_rva) + i * 4)[0])
        ordr = struct.unpack_from("<H", pe.data, pe.rva_to_off(ords_rva) + i * 2)[0]
        names[base + ordr] = name

    result = {}
    for i in range(nfuncs):
        frva = struct.unpack_from("<I", pe.data, pe.rva_to_off(funcs_rva) + i * 4)[0]
        if frva:
            result[base + i] = names.get(base + i, "")
    return result


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build/xinput1_4.dll"
    if not os.path.exists(path):
        print("missing file:", path)
        return 2

    exports = read_exports(path)
    print("file      :", path)
    print("exports   :", len(exports))
    for ordinal in sorted(exports):
        print("  #%-4d %s" % (ordinal, exports[ordinal]))

    missing = [o for o in REQUIRED if o not in exports]
    print()
    if missing:
        for ordinal in missing:
            print("MISSING   : #%d %s" % (ordinal, REQUIRED[ordinal]))
        print("RESULT    : FAIL - the game will not start with this DLL")
        return 1

    wrong = [
        o for o in REQUIRED
        if exports.get(o) and exports[o].lower() != REQUIRED[o].lower()
    ]
    if wrong:
        for ordinal in wrong:
            print("NAME      : #%d is %r, expected %r" % (ordinal, exports[ordinal], REQUIRED[ordinal]))
        print("RESULT    : FAIL - ordinal/name mismatch")
        return 1

    missing_optional = [o for o in sorted(OPTIONAL) if o not in exports]
    if missing_optional:
        print("note      : optional ordinals not exported:",
              ", ".join("#%d" % o for o in missing_optional))

    print("RESULT    : OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
