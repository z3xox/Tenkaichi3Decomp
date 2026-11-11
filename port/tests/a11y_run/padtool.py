"""Pad files for BT3_PAD_PLAY whose every read is numbered (bytes 14..17, the L1 R1 L2 R2 pressures, are the read's
index when those buttons are not pressed), and the map from a BT3_PAD_TABLE file back to read indexes per blank.

    padtool.py make <script> <out.pad> <total reads>    script lines: "<read index> <buttons> [hold reads]" (duel.txt)
    padtool.py map <table file>                         prints "blank read" for each blank
    padtool.py at <table file> <blank>                  the read index of the first read in that blank
"""
import struct, sys

BITS = dict(select=0x0001, l3=0x0002, r3=0x0004, start=0x0008, up=0x0010, right=0x0020, down=0x0040, left=0x0080,
            l2=0x0100, r2=0x0200, l1=0x0400, r1=0x0800, triangle=0x1000, circle=0x2000, cross=0x4000, square=0x8000)


def rec(btn, index):
    b = bytearray(18)
    b[0] = ~btn & 0xFF
    b[1] = ~(btn >> 8) & 0xFF
    b[2:6] = b"\x80\x80\x80\x80"
    order = (0x0020, 0x0080, 0x0010, 0x0040, 0x1000, 0x2000, 0x4000, 0x8000)
    for i, bit in enumerate(order):
        b[6 + i] = 0xFF if btn & bit else 0
    if not btn & 0x0F00:
        struct.pack_into("<I", b, 14, index | 0x80000000)  # (marked: never a real pressure pattern)
    return bytes(b)


def make(script, out, total):
    presses = {}
    for line in open(script):
        w = line.split("#")[0].split()  # (a note after "#")
        if not w:
            continue
        at, names = int(w[0]), w[1]
        hold = int(w[2]) if len(w) > 2 else 8
        p1 = p2 = 0
        for n in names.split("+"):
            if n.startswith("p2:"):
                p2 |= BITS[n[3:]]
            else:
                p1 |= BITS[n]
        for k in range(at, at + hold):
            presses[k] = (p1, p2)
    data = []
    for i in range(total):
        p1, p2 = presses.get(i, (0, 0))
        data.append(rec(p1 | p2, i))  # (both ports: which read goes to which port is not fixed)
    open(out, "wb").write(b"".join(data))


def table(path):
    t = open(path, "rb").read()
    out = []
    for blank in range(len(t) // 36):
        row = t[blank * 36:blank * 36 + 36]
        v = struct.unpack_from("<I", row, 14)[0]
        out.append(v & 0x7FFFFFFF if v & 0x80000000 else -1)
    return out


if __name__ == "__main__":
    if sys.argv[1] == "make":
        make(sys.argv[2], sys.argv[3], int(sys.argv[4]))
    elif sys.argv[1] == "map":
        for b, r in enumerate(table(sys.argv[2])):
            print(b + 1, r)
    elif sys.argv[1] == "at":
        t = table(sys.argv[2])
        b = int(sys.argv[3])
        print(t[b - 2] + 1 if b >= 2 and t[b - 2] >= 0 else t[b - 1])
