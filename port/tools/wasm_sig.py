#!/usr/bin/env python3
"""Calls whose declared type differs from the called function's definition, repaired for WebAssembly.

The game's sources declare the functions they call locally, and not always as they are defined (a function defined
`int f(int)` declared `void f()` elsewhere, ...). On the PS2, and in the native builds, such a call works: an unused
return register is ignored, a missing argument is whatever the register holds. On WebAssembly every call is typed,
and wasm-ld replaces a direct call with a mismatched type by a stub that traps ("function signature mismatch").

This finds every such call in the objects (port/tools/wasm_obj.py: the type each object declares an imported function
with, against the type of its definition) and rewrites the CALLER's LLVM IR to call the function with its true type:
  - arguments: same type as is; pointer / narrower integer -> i32 (ptrtoint / sext for `signext`, else zext);
    float <-> i32 bit for bit (as the native software-float build passes floats in integer registers);
    double -> float fptrunc, float -> double fpext; i64 <-> i32 trunc / zext;
    missing ones are passed as 0, extra ones dropped (the PS2 would pass whatever its registers held);
  - result: an unused / undeclared result is dropped; a result the definition does not produce reads as 0;
    a result of another type is converted as the arguments are.
Every repair is listed in port/build/wasm_sig.txt. Calls through function pointers are not covered (no static type
to compare with); a mismatch there traps at run time with the function's name in the stack.

Usage: python wasm_sig.py <folder with objects>...   (rewrites <object>.sf.ll / .ll to .sig.ll and recompiles)"""
import collections, pathlib, re, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import wasm_obj

ROOT = pathlib.Path(__file__).resolve().parents[2]
WRAPPED = {"Progress_Main"}  # linked with --wrap: callers of X reach __wrap_X, __real_X reaches X
IR_TYPE = {"i32": "i32", "i64": "i64", "f32": "float", "f64": "double"}
ZERO = {"i32": "0", "i64": "0", "float": "0.000000e+00", "double": "0.000000e+00", "ptr": "null"}
ATTRS = re.compile(r"^(noundef|signext|zeroext|nonnull|noalias|nocapture|readonly|readnone|writeonly|inreg|returned|"
                   r"immarg|noext|captures\([^)]*\)|align \d+|dereferenceable(?:_or_null)?\(\d+\)|"
                   r"(?:byval|sret|elementtype)\([^()]*\)|range\([^)]*\))\s+")

def true_type(name, defs):
    if name.startswith("__real_") and name[7:] in WRAPPED:
        return defs.get(name[7:])
    if name in WRAPPED:
        return defs.get("__wrap_" + name)
    return defs.get(name)

def split_args(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out

def parse_arg(a):
    """'i32 noundef signext %3' -> ('i32', 'signext' in attrs, '%3')"""
    m = re.match(r"^(ptr|i\d+|float|double|half|<[^>]*>|\{[^}]*\}|\[[^\]]*\])\s+(.*)$", a)
    if not m:
        raise ValueError(f"argument: {a}")
    ty, rest = m.group(1), m.group(2)
    signext = False
    while True:
        m2 = ATTRS.match(rest)
        if not m2:
            break
        signext |= m2.group(1) == "signext"
        rest = rest[m2.end():]
    return ty, signext, rest.strip()

class Fixer:
    def __init__(self, tag):
        self.tag, self.n, self.out = tag, 0, []
    def tmp(self):
        self.n += 1
        return f'%"sig.{self.tag}.{self.n}"'
    def conv(self, ind, ty, val, want, signext=False):
        """Value `val` of IR type `ty` as IR type `want`; emits instructions, returns the new value."""
        if ty == want:
            return val
        t = self.tmp()
        if ty == "ptr" and want in ("i32", "i64"):
            self.out.append(f"{ind}{t} = ptrtoint ptr {val} to {want}")
        elif ty == "ptr":
            return self.conv(ind, "i32", self.conv(ind, "ptr", val, "i32"), want)
        elif want == "ptr":
            v = self.conv(ind, ty, val, "i32")
            self.out.append(f"{ind}{t} = inttoptr i32 {v} to ptr")
        elif ty.startswith("i") and want.startswith("i"):
            a, b = int(ty[1:]), int(want[1:])
            op = "trunc" if a > b else ("sext" if signext else "zext")
            self.out.append(f"{ind}{t} = {op} {ty} {val} to {want}")
        elif {ty, want} == {"float", "i32"} or {ty, want} == {"double", "i64"}:
            self.out.append(f"{ind}{t} = bitcast {ty} {val} to {want}")
        elif ty == "double" and want == "float":
            self.out.append(f"{ind}{t} = fptrunc double {val} to float")
        elif ty == "float" and want == "double":
            self.out.append(f"{ind}{t} = fpext float {val} to double")
        elif ty == "float" and want == "i64":
            return self.conv(ind, "i32", self.conv(ind, "float", val, "i32"), "i64")
        elif ty == "i64" and want == "float":
            return self.conv(ind, "i32", self.conv(ind, "i64", val, "i32"), "float")
        elif ty.startswith("i") and want == "float":
            return self.conv(ind, "i32", self.conv(ind, ty, val, "i32", signext), "float")
        elif ty.startswith("i") and want == "double":
            return self.conv(ind, "i64", self.conv(ind, ty, val, "i64", signext), "double")
        elif ty == "double" and want.startswith("i"):
            return self.conv(ind, "i64", self.conv(ind, "double", val, "i64"), want)
        else:
            raise ValueError(f"no conversion {ty} -> {want}")
        return t

def fix_module(text, wrong, tag):
    """Rewrites calls of the functions in `wrong` ({name: (params, results)} true wasm types). Returns (text, log)."""
    lines, out, log = text.split("\n"), [], []
    for line in lines:
        m = re.match(r"^declare\b.*?@([-\w.$]+)\(", line)
        if m and m.group(1) in wrong:
            p, r = wrong[m.group(1)]
            out.append(f"declare {IR_TYPE[r[0]] if r else 'void'} @{m.group(1)}({', '.join(IR_TYPE[x] for x in p)})")
            continue
        m = re.match(r"^(\s*)(?:(%[-\w.$]+|%\"[^\"]*\") = )?((?:tail |musttail |notail )?call )(.*?)@([-\w.$]+)\((.*)$", line)
        if not m or m.group(5) not in wrong:
            out.append(line)
            continue
        ind, dst, call, mid, name, rest = m.groups()
        depth, k = 1, 0
        while k < len(rest) and depth:  # the closing parenthesis of the argument list
            depth += {"(": 1, ")": -1}.get(rest[k], 0)
            k += 1
        args, tail = rest[:k - 1], rest[k:]
        tail = re.sub(r"^\s*#\d+", "", tail)  # call-site attributes of the old type
        decl_ret = re.sub(r"\(.*\)\s*$", "", re.sub(r"^(?:(?:fast|nnan|ninf|nsz|arcp|contract|afn|reassoc|\w+cc|"
                                                     r"noundef|signext|zeroext|nonnull|noalias|inreg|"
                                                     r"(?:range|align|dereferenceable)\S*)\s+)*", "", mid.strip())).strip()
        decl_ret = decl_ret.split()[0] if decl_ret else "void"
        # a call declared variadic (`f(int, int, ...)`) passes all its arguments positionally, as the native ABI does;
        # variadic arguments arrive promoted already (float -> double, char -> int)
        p, r = wrong[name]
        fx = Fixer(f"{tag}.{len(log)}")
        fx.out = out
        given = [parse_arg(a) for a in split_args(args)]
        vals = []
        for i, w in enumerate(p):
            want = IR_TYPE[w]
            if i < len(given):
                ty, sx, v = given[i]
                vals.append(f"{want} {fx.conv(ind, ty, v, want, sx)}")
            else:
                vals.append(f"{want} {ZERO[want]}")
        newret = IR_TYPE[r[0]] if r else "void"
        call_text = f"{call}{newret} @{name}({', '.join(vals)})"
        log.append(f"{name}: declared {decl_ret}({', '.join(g[0] for g in given)}) -> called as {newret}({', '.join(IR_TYPE[x] for x in p)})")
        if dst is None and newret != "void":  # (an unnamed non-void call would take a number and shift the others)
            out.append(f"{ind}{fx.tmp()} = {call_text}{tail}")
        elif dst is None:
            out.append(f"{ind}{call_text}{tail}")
        elif newret == "void":
            out.append(f"{ind}{call_text}{tail}")
            out.append(f"{ind}{dst} = select i1 true, {decl_ret} {ZERO.get(decl_ret, 'zeroinitializer')}, {decl_ret} {ZERO.get(decl_ret, 'zeroinitializer')}")
        elif newret == decl_ret:
            out.append(f"{ind}{dst} = {call_text}{tail}")
        else:
            t = fx.tmp()
            out.append(f"{ind}{t} = {call_text}{tail}")
            v = fx.conv(ind, newret, t, decl_ret)
            out.append(f"{ind}{dst} = select i1 true, {decl_ret} {v}, {decl_ret} {v}")
    return "\n".join(out), log

def main():
    folders = [pathlib.Path(f) for f in (sys.argv[1:] or [ROOT / "port/build/obj_wasm", ROOT / "port/build/obj_wasm_data"])]
    objs = [o for f in folders for o in sorted(f.glob("*.o"))]
    defs, imps = {}, {}
    for o in objs:
        d, i = wasm_obj.functions(o)
        for k, v in d.items():
            defs.setdefault(k, v)
        imps[o] = i
    report, failed, fixed = [], 0, 0
    for o in objs:
        if "obj_wasm_data" in str(o):  # the data tables only take addresses: the type does not matter there
            continue
        wrong = {k: true_type(k, defs) for k, v in imps[o].items() if true_type(k, defs) is not None and v != true_type(k, defs)}
        if not wrong:
            continue
        src = o.with_suffix(".sf.ll") if o.with_suffix(".sf.ll").exists() else o.with_suffix(".ll")
        sig = o.with_suffix(".sig.ll")
        # a repair already made to this same build of the object (a later pass of the link: the stubs it added can
        # bring new mismatches) is kept: the new repairs go on top of it, not on the unrepaired IR
        if sig.exists() and src.exists() and sig.stat().st_mtime >= src.stat().st_mtime:
            src = sig
        if not src.exists():
            report.append(f"{o.name}: NO IR to repair: {', '.join(sorted(wrong))}")
            failed += 1
            continue
        try:
            text, log = fix_module(src.read_text(encoding="latin-1"), wrong, o.stem)
        except ValueError as e:
            report.append(f"{o.name}: FAILED: {e}")
            failed += 1
            continue
        sig.write_text(text, encoding="latin-1", newline="\n")
        tmp = o.with_suffix(".sig.o")  # (emcc deletes its output on failure: the object is replaced only on success)
        r = subprocess.run(["emcc", "-c", "-O2", str(sig), "-o", str(tmp)], capture_output=True, text=True)
        if r.returncode == 0:
            tmp.replace(o)
        if r.returncode:
            report.append(f"{o.name}: FAILED to compile the repaired IR: {r.stderr.strip()[-500:]}")
            failed += 1
            continue
        fixed += 1
        report += [f"{o.name}: {l}" for l in log]
    (ROOT / "port/build/wasm_sig.txt").write_text("\n".join(report) + "\n")
    print(f"{fixed} objects repaired, {sum(1 for l in report if '->' in l)} calls; {failed} failed (port/build/wasm_sig.txt)")
    for l in report:
        if "FAILED" in l or "NO IR" in l:
            print("  " + l[:400])
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
