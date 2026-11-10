"""Dependency-free PE export table dumper (ordinal + name + RVA)."""

import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from pe_imports import PE  # noqa: E402


def dump(path):
    with open(path, "rb") as fh:
        pe = PE(fh.read())

    rva, size = pe.directory(0)
    if not rva:
        print("no export directory")
        return
    off = pe.rva_to_off(rva)
    (_flags, _ts, _major, _minor, name_rva, base, nfuncs, nnames,
     funcs_rva, names_rva, ords_rva) = struct.unpack_from("<IIHHIIIIIII", pe.data, off)

    print("dll        :", pe.cstr(name_rva))
    print("ordinal base:", base, " functions:", nfuncs, " names:", nnames)

    names = {}
    for i in range(nnames):
        nr = pe.rva_to_off(names_rva) + i * 4
        name = pe.cstr(struct.unpack_from("<I", pe.data, nr)[0])
        ordr = struct.unpack_from("<H", pe.data, pe.rva_to_off(ords_rva) + i * 2)[0]
        names[ordr] = name

    for i in range(nfuncs):
        frva = struct.unpack_from("<I", pe.data, pe.rva_to_off(funcs_rva) + i * 4)[0]
        if frva == 0:
            continue
        ordinal = base + i
        print("  #%-4d %-38s RVA=0x%X" % (ordinal, names.get(i, ""), frva))


if __name__ == "__main__":
    dump(sys.argv[1] if len(sys.argv) > 1 else r"C:\Windows\System32\XInput1_4.dll")
