#!/usr/bin/env python3
"""Build an Italian (and later Multi5) language overlay for the USA port from a Europe disc.

    port/tools/map_eu_lang.py --eu-iso <Europe.iso> [--usa-data gamedata] [--out gamedata/lang/it]

The USA port keeps its own file ids (pzs3us*). The Europe disc (SLES-549.45) stores five text
language packs in PZS3EU1 at USA_index+3 + 31*lang (En=0, Fr=1, De=2, Es=3, It=4). Italian is
lang index 4: USA pzs3us1/N.bin <- EU pzs3eu1/(N+127).bin. The same Multi5 layout can build
French / German / Spanish overlays by switching EU_LANG_* (and the matching story-text band);
voices stay En/Ja only on that disc.

Battle voices on the Europe disc are still English/Japanese only (same two sets as USA, file ids
shifted by +246 into partition 2). Italian therefore overlays menu/UI text archives; spoken battle
lines stay English when Italian is selected.

Writes:
  <out>/pzs3us1/NNNNN.bin   Italian archives under the USA paths the game asks for
  <out>/manifest.txt        one line per file: usa_rel<TAB>note
  <out>/READY               marker for the port / installer

Needs only the Europe ISO for the copy (reads AFS entries from the ISO without a full unpack).
When --usa-data is set, each USA source file must exist so the mapping is checked against the
installed USA tree."""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# Italian = USA AFS index + 127 on Europe PZS3EU1 (Multi5 menu packs + GSCF story scripts).
# Menu band covers every archive the USA build loads via gProgress->baseFile + N
# (title, modes, Dragon History, saga packs, options, logos, sim day, MC flow, …).
MENU_USA_LO = 448
MENU_USA_HI = 478  # inclusive AFS index (= game file id - 1)
EU_LANG_BASE = 3          # En pack = USA_index + 3
EU_LANG_STRIDE = 31       # next language
EU_LANG_IT = 4            # En, Fr, De, Es, It
EU_IT_DELTA = EU_LANG_BASE + EU_LANG_STRIDE * EU_LANG_IT  # 127
PT2_ID_DELTA = 246        # USA file id in pt2 + 246 = EU file id (documented; voices stay En/Ja)
GSCF_MAGIC = b"GSCF"

# Story battle dialogue (UTF-16): USA file id = story + 0x231 → AFS index 560+story.
# Europe Multi5 text bands stride 50 from EU 656 (En, Es, De, Fr, It); Italian story 0 = 856.
# USA keeps unk0==0 so it always asks for the English ids; we overlay Italian onto those paths.
STORY_TEXT_USA_LO = 560
STORY_TEXT_COUNT = 48
STORY_TEXT_EU_IT0 = 856  # EU index of Italian story 0 ("Così, sei venuto davvero…")

# Pause / battle UI strings (UTF-16): battle_load requests file id (unk0[0] + 6).
# USA English is AFS index 5 (and 6 as the Ja/alt slot). Europe Multi5: 5=En,6=Es,7=De,8=Fr,9=It.
PAUSE_UI_USA = (5, 6)
PAUSE_UI_EU_IT = 9

# Dragon History battle scripts (GSCF): USA AFS 510..557 (48 episodes, file id 511+line).
# Europe Italian GSCF at +96 are byte-identical to USA (dialogue lives in STORY_TEXT 560+).
# Never overlay them. Old packs wrongly used menu Multi5 +127, which is 31 scripts ahead —
# Freezer episode 3 (script 9) loaded Hildegarn (script 40). Stale files in that range are purged.
GSCF_USA_LO = 510
GSCF_USA_COUNT = 48


def read_iso9660_files(iso_path: pathlib.Path):
    """Return {UPPER/PATH: (byte_offset, size)} for every file in a PS2 ISO 9660 image."""
    files = {}
    with iso_path.open("rb") as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        if pvd[0] != 1 or pvd[1:6] != b"CD001":
            raise SystemExit(f"{iso_path}: not an ISO 9660 image")
        root_lba, root_size = struct.unpack_from("<I4xI", pvd, 158)

        def walk(lba, size, prefix):
            f.seek(lba * 2048)
            data = f.read(size)
            off = 0
            while off < len(data):
                reclen = data[off]
                if reclen == 0:
                    off = (off // 2048 + 1) * 2048
                    continue
                extent = struct.unpack_from("<I", data, off + 2)[0]
                esize = struct.unpack_from("<I", data, off + 10)[0]
                flags = data[off + 25]
                nlen = data[off + 32]
                name = data[off + 33 : off + 33 + nlen]
                if name not in (b"\x00", b"\x01"):
                    plain = name.split(b";")[0].decode("ascii", "replace").upper()
                    path = f"{prefix}/{plain}" if prefix else plain
                    if flags & 2:
                        walk(extent, esize, path)
                    else:
                        files[path] = (extent * 2048, esize)
                off += reclen

        walk(root_lba, root_size, "")
    return files


def load_afs_table(fp, offset, size):
    fp.seek(offset)
    head = fp.read(8)
    if head[:4] != b"AFS\0":
        raise SystemExit("DATA/PZS3EU1.AFS on the Europe disc is not an AFS archive")
    count = struct.unpack_from("<I", head, 4)[0]
    if 8 + count * 8 > size:
        raise SystemExit("DATA/PZS3EU1.AFS on the Europe disc looks damaged")
    table = struct.unpack(f"<{count * 2}I", fp.read(count * 8))
    return count, table


def bpe_decode(src: bytes) -> bytes:
    size, pack_size = struct.unpack_from("<ii", src, 0)
    inp = memoryview(src)[8:]
    out = bytearray()
    pos = 0
    while pos != pack_size:
        left = list(range(256))
        right = [0] * 256
        count = inp[pos]
        pos += 1
        c = 0
        while True:
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = inp[pos]
                pos += 1
                if c != left[c]:
                    right[c] = inp[pos]
                    pos += 1
                c += 1
            if c == 256:
                break
            count = inp[pos]
            pos += 1
        hi = inp[pos]
        pos += 1
        block_size = inp[pos] + (hi << 8)
        pos += 1
        stack: list[int] = []
        while True:
            if stack:
                c = stack.pop()
            else:
                if block_size == 0:
                    break
                block_size -= 1
                c = inp[pos]
                pos += 1
            if c == left[c]:
                out.append(c)
            else:
                stack.append(right[c])
                stack.append(left[c])
    if len(out) != size:
        raise ValueError(f"BPE size mismatch ({len(out)} != {size})")
    return bytes(out)


def bpe_encode_literal(raw: bytes) -> bytes:
    """Store `raw` as BPE with an identity dictionary (no pairs). Large but correct."""
    payload = bytearray()
    off = 0
    while True:
        chunk = raw[off : off + 0xFFFF]
        # count is 0..127 = (count+1) literal left[] writes; >127 skips (count-127) slots.
        # Two chunks of 128 write left[0..255] = identity.
        payload.append(127)
        payload.extend(range(128))
        payload.append(127)
        payload.extend(range(128, 256))
        n = len(chunk)
        payload.append((n >> 8) & 0xFF)
        payload.append(n & 0xFF)
        payload.extend(chunk)
        off += n
        if off >= len(raw):
            break
    return struct.pack("<ii", len(raw), len(payload)) + bytes(payload)


def mpack_entries(blob: bytes) -> list[bytes]:
    n = struct.unpack_from("<I", blob, 0)[0]
    if n <= 0 or n > 4096:
        raise ValueError(f"bad mpack count {n}")
    offs = [struct.unpack_from("<I", blob, 4 + 4 * i)[0] & ~3 for i in range(n)]
    out = []
    for i, o in enumerate(offs):
        end = offs[i + 1] if i + 1 < n else len(blob)
        if o >= len(blob):
            out.append(b"")
        else:
            out.append(blob[o:min(end, len(blob))])
    return out


def mpack_build(entries: list[bytes], align: int = 64) -> bytes:
    n = len(entries)
    hdr = 4 + 4 * n
    cursor = (hdr + (align - 1)) & ~(align - 1)
    offs = []
    body = bytearray()
    # pad between header and first entry
    body.extend(b"\x00" * (cursor - hdr))
    for ent in entries:
        offs.append(cursor)
        body.extend(ent)
        cursor += len(ent)
        pad = (-cursor) & 3
        body.extend(b"\x00" * pad)
        cursor += pad
    out = bytearray(struct.pack("<I", n))
    for o in offs:
        out.extend(struct.pack("<I", o))
    out.extend(body)
    return bytes(out)


def outer_sections(data: bytes) -> list[bytes]:
    if len(data) < 4:
        return []
    n = struct.unpack_from("<I", data, 0)[0]
    if n <= 0 or n > 4096 or 4 + 4 * n > len(data):
        return []
    out = []
    for si in range(n):
        o = struct.unpack_from("<I", data, 4 + 4 * si)[0] & ~3
        if si + 1 < n:
            eoff = 4 + 4 * (si + 1)
            e = struct.unpack_from("<I", data, eoff)[0] & ~3 if eoff + 4 <= len(data) else len(data)
        else:
            e = len(data)
        o = min(o, len(data))
        e = min(max(e, o), len(data))
        out.append(data[o:e])
    return out


def graft_missing_usa_entries(it_data: bytes, usa_data: bytes) -> tuple[bytes, int]:
    """Copy USA inner MPACK blobs into Italian where EU left an empty slot the USA build still reads.

    Returns (fixed_archive, grafts). Used for title (449) and FirstRun logos (473).
    """
    it_secs = outer_sections(it_data)
    usa_secs = outer_sections(usa_data)
    if not it_secs or len(it_secs) != len(usa_secs):
        return it_data, 0

    grafts = 0
    fixed_secs: list[bytes] = []
    for si, (it_chunk, usa_chunk) in enumerate(zip(it_secs, usa_secs)):
        try:
            it_inner = bpe_decode(it_chunk)
            usa_inner = bpe_decode(usa_chunk)
            it_ents = mpack_entries(it_inner)
            usa_ents = mpack_entries(usa_inner)
        except Exception:
            fixed_secs.append(it_chunk)
            continue
        if len(it_ents) != len(usa_ents):
            fixed_secs.append(it_chunk)
            continue
        changed = False
        for i, (ie, ue) in enumerate(zip(it_ents, usa_ents)):
            if not ie and ue:
                it_ents[i] = ue
                grafts += 1
                changed = True
        if changed:
            fixed_secs.append(bpe_encode_literal(mpack_build(it_ents)))
        else:
            fixed_secs.append(it_chunk)

    if grafts == 0:
        return it_data, 0
    return mpack_build(fixed_secs), grafts


def discover_overlay_indices(usa1: pathlib.Path, table, count: int) -> list[int]:
    """Menu band, pause UI, story dialogue texts, and GSCF scripts with an Italian twin."""
    found: set[int] = set()
    for usa_idx in range(MENU_USA_LO, MENU_USA_HI + 1):
        if (usa1 / f"{usa_idx:05d}.bin").is_file() and usa_idx + EU_IT_DELTA < count:
            found.add(usa_idx)
    for usa_idx in PAUSE_UI_USA:
        if (usa1 / f"{usa_idx:05d}.bin").is_file() and PAUSE_UI_EU_IT < count:
            found.add(usa_idx)
    for s in range(STORY_TEXT_COUNT):
        usa_idx = STORY_TEXT_USA_LO + s
        eu_idx = STORY_TEXT_EU_IT0 + s
        if (usa1 / f"{usa_idx:05d}.bin").is_file() and eu_idx < count:
            found.add(usa_idx)
    return sorted(found)


def eu_source_index(usa_idx: int) -> int:
    """Europe AFS index that holds the Italian (or Multi5) twin for a USA path."""
    if usa_idx in PAUSE_UI_USA:
        return PAUSE_UI_EU_IT
    if STORY_TEXT_USA_LO <= usa_idx < STORY_TEXT_USA_LO + STORY_TEXT_COUNT:
        return STORY_TEXT_EU_IT0 + (usa_idx - STORY_TEXT_USA_LO)
    return usa_idx + EU_IT_DELTA


def purge_stale_gscf(out_part: pathlib.Path) -> int:
    """Drop any leftover GSCF overlays (wrong +127 packs broke saga→script mapping)."""
    n = 0
    for s in range(GSCF_USA_COUNT):
        path = out_part / f"{GSCF_USA_LO + s:05d}.bin"
        if path.is_file():
            path.unlink()
            n += 1
    return n


def copy_eu_movies(iso: pathlib.Path, out: pathlib.Path, files: dict, manifest: list) -> int:
    """Overlay Europe opening/ending movies onto the USA disc/DATA names when Italian is on."""
    pairs = (
        ("DATA/ZS3EUOP.PSS", "disc/DATA/ZS3USOP.PSS"),
        ("DATA/ZS3EUOP.ADX", "disc/DATA/ZS3USOP.ADX"),
        ("DATA/ZS3EUED.PSS", "disc/DATA/ZS3USED.PSS"),
        ("DATA/ZS3EUED.ADX", "disc/DATA/ZS3USED.ADX"),
    )
    n = 0
    with iso.open("rb") as fp:
        for src_key, dst_rel in pairs:
            ent = files.get(src_key)
            if ent is None:
                print(f"missing movie on Europe disc: {src_key}", file=sys.stderr)
                continue
            off, size = ent
            fp.seek(off)
            data = fp.read(size)
            if len(data) != size:
                raise SystemExit(f"short read for {src_key}")
            dst = out / dst_rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
            manifest.append(f"{dst_rel}\t{src_key}\t{size}")
            n += 1
            print(f"{dst_rel} <- {src_key} ({size} bytes)")
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--eu-iso", type=pathlib.Path, help="Europe disc image (SLES-549.45)")
    ap.add_argument("--eu-data", type=pathlib.Path, help="already-unpacked Europe tree (pzs3eu1/ inside)")
    ap.add_argument("--usa-data", type=pathlib.Path, default=ROOT / "gamedata", help="USA gamedata (optional check)")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "gamedata" / "lang" / "it")
    ap.add_argument("--lang", default="it", choices=("it",), help="language pack to build (only it for now)")
    args = ap.parse_args()

    if args.eu_iso is None and args.eu_data is None:
        raise SystemExit("Give --eu-iso <Europe.iso> or --eu-data <folder with pzs3eu1/>")

    usa1 = args.usa_data / "pzs3us1"
    check_usa = usa1.is_dir() and any(usa1.glob("*.bin"))
    if not check_usa:
        print(f"note: USA data not found at {usa1} (overlay will still be written; install USA first to play)",
              file=sys.stderr)

    out = args.out
    out_part = out / "pzs3us1"
    out_part.mkdir(parents=True, exist_ok=True)
    purged = purge_stale_gscf(out_part)
    if purged:
        print(f"note: removed {purged} stale GSCF overlay(s) from {out_part}")
    manifest = []
    copied = 0

    def maybe_graft(usa_idx: int, data: bytes, dst_rel: str) -> bytes:
        if not check_usa or not (MENU_USA_LO <= usa_idx <= MENU_USA_HI):
            return data
        usa_bin = (usa1 / f"{usa_idx:05d}.bin").read_bytes()
        try:
            fixed, grafts = graft_missing_usa_entries(data, usa_bin)
        except Exception as exc:
            print(f"  note: graft skipped for {dst_rel}: {exc}", file=sys.stderr)
            return data
        if grafts:
            print(f"  note: grafted {grafts} USA blob(s) into Italian {dst_rel}")
            return fixed
        return data

    if args.eu_data is not None:
        eu1 = args.eu_data / "pzs3eu1"
        if not eu1.is_dir():
            raise SystemExit(f"{eu1}: missing (unpack the Europe disc with extract_disc.py first)")
        # Build a fake size table from loose files for discovery of GSCF peers.
        eu_files = {int(p.stem): p for p in eu1.glob("*.bin") if p.stem.isdigit()}
        max_eu = max(eu_files) if eu_files else 0
        fake_table = [0] * ((max_eu + 1) * 2)
        for idx, p in eu_files.items():
            fake_table[idx * 2 + 1] = p.stat().st_size
        indices = discover_overlay_indices(usa1, fake_table, max_eu + 1) if check_usa else list(range(MENU_USA_LO, MENU_USA_HI + 1))
        print(f"overlay list: {len(indices)} files")
        for usa_idx in indices:
            eu_idx = eu_source_index(usa_idx)
            src = eu1 / f"{eu_idx:05d}.bin"
            dst_rel = f"pzs3us1/{usa_idx:05d}.bin"
            if not src.is_file():
                print(f"missing: {src}", file=sys.stderr)
                continue
            data = maybe_graft(usa_idx, src.read_bytes(), dst_rel)
            (out_part / f"{usa_idx:05d}.bin").write_bytes(data)
            manifest.append(f"{dst_rel}\teu_pzs3eu1/{eu_idx:05d}.bin\t{len(data)}")
            copied += 1
            print(f"{dst_rel} <- pzs3eu1/{eu_idx:05d}.bin ({len(data)} bytes)")
    else:
        iso = args.eu_iso
        files = read_iso9660_files(iso)
        if "SLES_549.45" not in files and "SLES_549.45;1" not in {k.split("/")[-1] for k in files}:
            # path keys are UPPER without ;version in our walker
            if "SLES_549.45" not in files:
                raise SystemExit(f"{iso.name}: not the Europe release (no SLES_549.45)")
        afs = files.get("DATA/PZS3EU1.AFS")
        if afs is None:
            raise SystemExit(f"{iso.name}: no DATA/PZS3EU1.AFS")
        with iso.open("rb") as fp:
            count, table = load_afs_table(fp, afs[0], afs[1])
            indices = discover_overlay_indices(usa1, table, count) if check_usa else list(range(MENU_USA_LO, MENU_USA_HI + 1))
            print(f"overlay list: {len(indices)} files")
            for usa_idx in indices:
                eu_idx = eu_source_index(usa_idx)
                dst_rel = f"pzs3us1/{usa_idx:05d}.bin"
                if eu_idx >= count:
                    print(f"missing index {eu_idx} in PZS3EU1", file=sys.stderr)
                    continue
                off, size = table[eu_idx * 2], table[eu_idx * 2 + 1]
                fp.seek(afs[0] + off)
                data = fp.read(size)
                if len(data) != size:
                    raise SystemExit(f"short read for EU index {eu_idx}")
                # Skip identical GSCF (no Italian delta)
                usa_path = usa1 / f"{usa_idx:05d}.bin"
                if check_usa and usa_path.is_file() and data == usa_path.read_bytes():
                    print(f"skip (identical to USA): {dst_rel}")
                    continue
                data = maybe_graft(usa_idx, data, dst_rel)
                (out_part / f"{usa_idx:05d}.bin").write_bytes(data)
                manifest.append(f"{dst_rel}\teu_pzs3eu1/{eu_idx:05d}.bin\t{len(data)}")
                copied += 1
                print(f"{dst_rel} <- eu#{eu_idx} ({len(data)} bytes)")
        copied += copy_eu_movies(iso, out, files, manifest)

    (out / "manifest.txt").write_text(
        "# usa_rel\teu_source\tbytes — Italian UI archives for the USA port\n"
        f"# pt2_id_delta={PT2_ID_DELTA} (EU voices are En/Ja only; Italian uses English audio)\n"
        + "\n".join(manifest) + ("\n" if manifest else ""),
        encoding="utf-8",
    )
    (out / "READY").write_text(
        f"lang={args.lang}\nfiles={copied}\nsource=europe\n",
        encoding="utf-8",
    )
    print(f"done: {copied} files -> {out}")
    if copied == 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
