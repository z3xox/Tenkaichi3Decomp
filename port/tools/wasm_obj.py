#!/usr/bin/env python3
"""Reads the function signatures of WebAssembly object files (relocatable, with a "linking" section).

functions(path) -> (defined, imported): {name: (params, results)} each, types as "i32" / "i64" / "f32" / "f64".
`imported` are the functions the object calls or takes the address of without defining them, with the type the
object declared them with. Used by wasm_sig.py to find and repair calls whose declared type differs from the
definition (wasm-ld turns such a direct call into a trap). Only what that needs is parsed.

Usage: python wasm_obj.py <object.o>   (prints both tables)"""
import sys

VT = {0x7F: "i32", 0x7E: "i64", 0x7D: "f32", 0x7C: "f64", 0x7B: "v128", 0x70: "funcref", 0x6F: "externref"}
SYM_UNDEFINED, SYM_EXPLICIT_NAME = 0x10, 0x40

class R:
    def __init__(self, b, p=0):
        self.b, self.p = b, p
    def u8(self):
        self.p += 1
        return self.b[self.p - 1]
    def uleb(self):
        v = s = 0
        while True:
            x = self.u8()
            v |= (x & 0x7F) << s
            s += 7
            if not x & 0x80:
                return v
    def name(self):
        n = self.uleb()
        self.p += n
        return self.b[self.p - n:self.p].decode("utf-8", "replace")
    def limits(self):
        f = self.u8()
        self.uleb()
        if f & 1:
            self.uleb()

def functions(path):
    b = open(path, "rb").read()
    if b[:4] != b"\0asm":
        raise ValueError(f"{path}: not a WebAssembly file")
    r = R(b, 8)
    types, imports, funcs, syms = [], [], [], None
    while r.p < len(b):
        sid, size = r.u8(), None
        size = r.uleb()
        end = r.p + size
        if sid == 1:
            for _ in range(r.uleb()):
                if r.u8() != 0x60:
                    raise ValueError("unknown type form")
                params = tuple(VT.get(r.u8(), "?") for _ in range(r.uleb()))
                results = tuple(VT.get(r.u8(), "?") for _ in range(r.uleb()))
                types.append((params, results))
        elif sid == 2:
            for _ in range(r.uleb()):
                mod, field, kind = r.name(), r.name(), r.u8()
                if kind == 0:
                    imports.append((mod, field, r.uleb()))
                elif kind == 1:
                    r.u8()
                    r.limits()
                elif kind == 2:
                    r.limits()
                elif kind == 3:
                    r.u8()
                    r.u8()
                elif kind == 4:
                    r.u8()
                    r.uleb()
        elif sid == 3:
            funcs = [r.uleb() for _ in range(r.uleb())]
        elif sid == 0:
            name = r.name()
            if name == "linking":
                r.uleb()  # version
                while r.p < end:
                    kind, ssize = r.u8(), r.uleb()
                    send = r.p + ssize
                    if kind == 8:  # symbol table
                        syms = []
                        for _ in range(r.uleb()):
                            sk, flags = r.u8(), r.uleb()
                            if sk in (0, 2, 4, 5):  # function, global, tag, table
                                idx = r.uleb()
                                nm = r.name() if (not flags & SYM_UNDEFINED) or (flags & SYM_EXPLICIT_NAME) else None
                                syms.append((sk, flags, idx, nm))
                            elif sk == 1:  # data
                                nm = r.name()
                                if not flags & SYM_UNDEFINED:
                                    r.uleb(); r.uleb(); r.uleb()
                            elif sk == 3:  # section
                                r.uleb()
                    r.p = send
        r.p = end
    defined, imported = {}, {}
    for sk, flags, idx, nm in syms or []:
        if sk != 0:
            continue
        if idx < len(imports):
            nm = nm or imports[idx][1]
            imported[nm] = types[imports[idx][2]]
        elif not flags & SYM_UNDEFINED:
            defined[nm] = types[funcs[idx - len(imports)]]
    return defined, imported

if __name__ == "__main__":
    d, i = functions(sys.argv[1])
    for k, v in sorted(d.items()):
        print("def", k, v)
    for k, v in sorted(i.items()):
        print("imp", k, v)
