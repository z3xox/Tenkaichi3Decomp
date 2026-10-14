#!/usr/bin/env python3
"""Software float for the WebAssembly build: the PS2's single-precision arithmetic on a target without -msoft-float.

The native builds compile the game with -msoft-float, so the code generator turns every single-precision operation
into a call of port/src/softfloat_ps2.c (__addsf3, __ltsf2, __fixsfsi, ...), with floats as 32-bit patterns.
clang's wasm32 target has no such mode. This does the same lowering on LLVM IR instead, BEFORE any optimisation
(the IR comes from clang -O0 -Xclang -disable-O0-optnone and is optimised after this):

    %r = fadd float %a, %b    ->  %sf.r.a = bitcast float %a to i32
                                  %sf.r.b = bitcast float %b to i32
                                  %sf.r.c = call i32 @__addsf3(i32 %sf.r.a, i32 %sf.r.b)
                                  %r = bitcast i32 %sf.r.c to float

fadd / fsub / fmul / fdiv, fcmp (every predicate: through the three-way comparison the routines return; the PS2 has
no unordered values, so `ord` is true and `uno` false, as with the native routines), fptosi / fptoui to i32 or
narrower, sitofp / uitofp from i32 or narrower. WebAssembly's bitcasts, loads, stores, locals and select keep every
bit pattern (only its arithmetic may change NaNs), so a pattern travels unchanged between the routines.
Left as they are, as in the native build: double arithmetic and float <-> double conversions (port/src/plat_libm.c
does them with the host's IEEE arithmetic there too), fneg, llvm.fabs / llvm.copysign (sign-bit operations).
Anything else on float values (frem, 64-bit conversions, vectors of float, other float intrinsics) stops the build
with its line, rather than being compiled with the host's arithmetic.

Usage: wasm_sf.py in.ll out.ll   (prints the number of rewritten instructions)"""
import re, sys

BIN = {"fadd": "__addsf3", "fsub": "__subsf3", "fmul": "__mulsf3", "fdiv": "__divsf3"}
# fcmp predicate -> icmp of the three-way result (-1, 0, 1) against 0; None: constant
CMP = {"oeq": "eq", "ueq": "eq", "one": "ne", "une": "ne", "olt": "slt", "ult": "slt", "ole": "sle", "ule": "sle",
       "ogt": "sgt", "ugt": "sgt", "oge": "sge", "uge": "sge", "ord": True, "uno": False, "true": True, "false": False}
FMF = r"(?:(?:fast|nnan|ninf|nsz|arcp|contract|afn|reassoc)\s+)*"
# an operand: a value name or any constant spelling (1.0e+00, 0x3FF0000000000000, f0x3F800000 in newer LLVM, ...)
VAL = r"(%[-\w.$]+|%\"[^\"]*\"|[^\s,%][^\s,]*)"
# after the rewrite no float instruction may be left: one that is was not understood by the rules
RE_LEFT = re.compile(r"= (fadd|fsub|fmul|fdiv|frem|fcmp)\b[^=]*\bfloat\b|= (fptosi|fptoui) [^=]*\bfloat\b|= (sitofp|uitofp) .* to float\b")
RE_BIN = re.compile(rf"^(\s*)(%[-\w.$]+) = (fadd|fsub|fmul|fdiv) {FMF}float {VAL}, {VAL}(.*)$")
RE_CMP = re.compile(rf"^(\s*)(%[-\w.$]+) = fcmp {FMF}(\w+) float {VAL}, {VAL}(.*)$")
RE_F2I = re.compile(rf"^(\s*)(%[-\w.$]+) = (fptosi|fptoui) float {VAL} to i(\d+)(.*)$")
RE_I2F = re.compile(rf"^(\s*)(%[-\w.$]+) = (sitofp|uitofp) i(\d+) {VAL} to float(.*)$")
# float operations this does not lower: refused (a line that matches none of the rules above)
RE_BAD = re.compile(r"= (frem\b.*\bfloat\b|f\w+ <\d+ x float>|fptos?u?i float .* to i64|[su]itofp i64 .* to float)|"
                    r"call [^@]*@llvm\.(?!fabs\.f32|copysign\.f32|dbg\.|lifetime\.|memcpy|memset|memmove|stack|va_)\w+\.f32\b")
ATTR = "nounwind willreturn memory(none)"
DECL = {"__addsf3": "i32 @__addsf3(i32, i32)", "__subsf3": "i32 @__subsf3(i32, i32)", "__mulsf3": "i32 @__mulsf3(i32, i32)",
        "__divsf3": "i32 @__divsf3(i32, i32)", "__ltsf2": "i32 @__ltsf2(i32, i32)", "__fixsfsi": "i32 @__fixsfsi(i32)",
        "__fixunssfsi": "i32 @__fixunssfsi(i32)", "__floatsisf": "i32 @__floatsisf(i32)",
        "__floatunsisf": "i32 @__floatunsisf(i32)"}

def tmp(dst, s):
    """A named temporary next to destination `dst` (named, so the numbering of unnamed values is unchanged)."""
    base = dst[1:].strip('"').replace('"', "")
    return f'%"sf.{base}.{s}"'

def fix(text):
    out, used, n, bad = [], set(), 0, []
    for no, line in enumerate(text.split("\n"), 1):
        m = RE_BIN.match(line)
        if m:
            ind, dst, op, a, b, rest = m.groups()
            fn = BIN[op]
            ta, tb, tc = tmp(dst, "a"), tmp(dst, "b"), tmp(dst, "c")
            out += [f"{ind}{ta} = bitcast float {a} to i32", f"{ind}{tb} = bitcast float {b} to i32",
                    f"{ind}{tc} = call i32 @{fn}(i32 {ta}, i32 {tb})", f"{ind}{dst} = bitcast i32 {tc} to float{rest}"]
            used.add(fn)
            n += 1
            continue
        m = RE_CMP.match(line)
        if m:
            ind, dst, pred, a, b, rest = m.groups()
            if pred not in CMP:
                bad.append((no, line.strip()))
                out.append(line)
                continue
            c = CMP[pred]
            if c is True or c is False:
                out.append(f"{ind}{dst} = icmp {'eq' if c else 'ne'} i32 0, 0{rest}")
            else:
                ta, tb, tc = tmp(dst, "a"), tmp(dst, "b"), tmp(dst, "c")
                out += [f"{ind}{ta} = bitcast float {a} to i32", f"{ind}{tb} = bitcast float {b} to i32",
                        f"{ind}{tc} = call i32 @__ltsf2(i32 {ta}, i32 {tb})", f"{ind}{dst} = icmp {c} i32 {tc}, 0{rest}"]
                used.add("__ltsf2")
            n += 1
            continue
        m = RE_F2I.match(line)
        if m and int(m.group(5)) <= 32:
            ind, dst, op, a, bits, rest = m.groups()
            # to i32: the signed or unsigned routine; narrower: through the signed one, as the native lowering does
            fn = "__fixunssfsi" if op == "fptoui" and bits == "32" else "__fixsfsi"
            ta, tc = tmp(dst, "a"), tmp(dst, "c")
            out += [f"{ind}{ta} = bitcast float {a} to i32", f"{ind}{tc} = call i32 @{fn}(i32 {ta})"]
            out.append(f"{ind}{dst} = {'trunc i32 ' + tc + ' to i' + bits if bits != '32' else 'add i32 ' + tc + ', 0'}{rest}")
            used.add(fn)
            n += 1
            continue
        m = RE_I2F.match(line)
        if m and int(m.group(4)) <= 32:
            ind, dst, op, bits, a, rest = m.groups()
            fn = "__floatunsisf" if op == "uitofp" and bits == "32" else "__floatsisf"
            ta, tc = tmp(dst, "a"), tmp(dst, "c")
            if bits == "32":
                out.append(f"{ind}{ta} = add i32 {a}, 0")
            else:  # narrower values are widened first (sign- or zero-extended as the operation says)
                out.append(f"{ind}{ta} = {'sext' if op == 'sitofp' else 'zext'} i{bits} {a} to i32")
            out += [f"{ind}{tc} = call i32 @{fn}(i32 {ta})", f"{ind}{dst} = bitcast i32 {tc} to float{rest}"]
            used.add(fn)
            n += 1
            continue
        if RE_BAD.search(line):
            bad.append((no, line.strip()))
        out.append(line)
    for no, line in enumerate(out, 1):  # the safety net (line numbers of the output)
        if RE_LEFT.search(line):
            bad.append((no, line.strip()))
    text = "\n".join(out)
    for fn in sorted(used):  # declarations, unless the module declares or defines the routine itself
        if not re.search(rf"^(declare|define)\b[^\n]*@{fn}\(", text, re.M):
            text += f"\ndeclare {DECL[fn]} {ATTR}\n"
    return text, n, bad

def main():
    src, dst = sys.argv[1], sys.argv[2]
    text, n, bad = fix(open(src, encoding="latin-1").read())
    if bad:
        for no, l in bad[:20]:
            print(f"{src}:{no}: float operation without a PS2 routine: {l[:200]}", file=sys.stderr)
        sys.exit(1)
    open(dst, "w", encoding="latin-1", newline="\n").write(text)
    print(n)

if __name__ == "__main__":
    main()
