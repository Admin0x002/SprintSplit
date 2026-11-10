"""Tiny dependency-free PE import table dumper.

Usage:
    python pe_imports.py <path-to-exe-or-dll> [dll-name-filter]

Prints every imported module, and for the modules matching the optional filter
(case-insensitive substring) prints the imported symbols, marking ordinals so
that proxy DLLs know exactly which ordinal/name pairs they must export.
"""

import struct
import sys


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a PE file (no MZ header)")
        pe_off = u32(data, 0x3C)
        if data[pe_off:pe_off + 4] != b"PE\0\0":
            raise ValueError("not a PE file (no PE signature)")

        coff = pe_off + 4
        self.machine = u16(data, coff)
        nsections = u16(data, coff + 2)
        opt_size = u16(data, coff + 16)
        opt = coff + 20
        magic = u16(data, opt)
        self.is_64 = magic == 0x20B

        if self.is_64:
            self.image_base = u64(data, opt + 24)
            ndirs_off = opt + 108
            self.dirs_off = opt + 112
        else:
            self.image_base = u32(data, opt + 28)
            ndirs_off = opt + 92
            self.dirs_off = opt + 96

        ndirs = u32(data, ndirs_off)
        self.ndirs = min(ndirs, 16)

        self.sections = []
        sec = opt + opt_size
        for i in range(nsections):
            base = sec + i * 40
            name = data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize = u32(data, base + 8)
            vaddr = u32(data, base + 12)
            rawsize = u32(data, base + 16)
            rawptr = u32(data, base + 20)
            self.sections.append((name, vaddr, max(vsize, rawsize), rawptr))

    def rva_to_off(self, rva):
        for _name, vaddr, size, rawptr in self.sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        raise ValueError("RVA 0x%X not mapped" % rva)

    def directory(self, index):
        if index >= self.ndirs:
            return 0, 0
        return u32(self.data, self.dirs_off + index * 8), u32(
            self.data, self.dirs_off + index * 8 + 4
        )

    def cstr(self, rva):
        off = self.rva_to_off(rva)
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("ascii", "replace")

    def imports(self):
        rva, size = self.directory(1)
        if not rva:
            return []
        off = self.rva_to_off(rva)
        result = []
        idx = 0
        while True:
            desc = off + idx * 20
            oft, ts, fwd, name_rva, ft = struct.unpack_from("<IIIII", self.data, desc)
            if oft == 0 and name_rva == 0 and ft == 0:
                break
            dll = self.cstr(name_rva)
            thunk_rva = oft or ft
            syms = []
            j = 0
            while True:
                if self.is_64:
                    val = u64(self.data, self.rva_to_off(thunk_rva) + j * 8)
                    if val == 0:
                        break
                    if val & 0x8000000000000000:
                        syms.append(("ord", val & 0xFFFF))
                    else:
                        syms.append(("name", self.cstr((val & 0x7FFFFFFF) + 2)))
                else:
                    val = u32(self.data, self.rva_to_off(thunk_rva) + j * 4)
                    if val == 0:
                        break
                    if val & 0x80000000:
                        syms.append(("ord", val & 0xFFFF))
                    else:
                        syms.append(("name", self.cstr((val & 0x7FFFFFFF) + 2)))
                j += 1
            result.append((dll, syms))
            idx += 1
        return result


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    needle = sys.argv[2].lower() if len(sys.argv) > 2 else None
    with open(path, "rb") as fh:
        pe = PE(fh.read())

    print("file      :", path)
    print("machine   :", "x64" if pe.machine == 0x8664 else hex(pe.machine))
    print("imagebase : 0x%X" % pe.image_base)
    print()
    for dll, syms in pe.imports():
        if needle and needle not in dll.lower():
            continue
        print("%s  (%d symbols)" % (dll, len(syms)))
        for kind, value in syms:
            if kind == "ord":
                print("    #%d (ordinal)" % value)
            else:
                print("    %s" % value)
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
