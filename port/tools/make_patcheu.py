#!/usr/bin/env python3
"""Build a drop-in `patcheu/` language pack from a Europe disc (SLES-549.45).

    python3 port/tools/make_patcheu.py --eu-iso <Europe.iso>
    python3 port/tools/make_patcheu.py --eu-iso <Europe.iso> --out path/to/patcheu

Result (next to the game exe, beside gamedata/):

    patcheu/
      README.txt
      READY
      manifest.txt
      pzs3us1/00448.bin ...   Italian UI archives (USA paths)

The USA port must already be installed from the USA disc. Put `patcheu` next to
Tenkaichi3Decomp.exe (same folder as gamedata/). In game: F1 → UI → Menu & dialogue text → Italian
(voices stay English/Japanese under Audio — there is no Italian dub).

This only reads a few dozen MB from the Europe ISO (menu archives), not a full unpack."""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MAP = ROOT / "port" / "tools" / "map_eu_lang.py"

README = """Tenkaichi3Decomp - patcheu (Italian menu/dialogue TEXT from Europe disc)

How to use
----------
1. Install the game from the USA disc (SLUS-21678) as usual.
2. Put this whole `patcheu` folder next to Tenkaichi3Decomp.exe
   (same place as the `gamedata` folder).
3. Start the game: F1, UI, Menu & dialogue text, Italian.

Notes
-----
- Menu / dialogue / UI text comes from your Europe disc (SLES-549.45).
- There is no Italian voice acting; use F1, Audio, Voice language for En/Ja.
- This pack does not replace the USA install; it only overlays Italian text files.
- Rebuild anytime:  python3 port/tools/make_patcheu.py --eu-iso <Europe.iso>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--eu-iso", type=pathlib.Path, required=True, help="Europe disc image (SLES-549.45)")
    ap.add_argument("--out", type=pathlib.Path, default=None,
                    help="output folder (default: ./patcheu next to this repo, or cwd/patcheu)")
    ap.add_argument("--usa-data", type=pathlib.Path, default=ROOT / "gamedata")
    args = ap.parse_args()

    if not args.eu_iso.is_file():
        raise SystemExit(f"No such file: {args.eu_iso}")

    out = args.out
    if out is None:
        # Prefer next to an existing gamedata / release; else repo root; else cwd.
        for base in (ROOT, pathlib.Path.cwd(), args.usa_data.parent if args.usa_data else None):
            if base is None:
                continue
            if (base / "gamedata").is_dir() or (base / "Tenkaichi3Decomp.exe").is_file() or (
                    base / "Tenkaichi3Decomp").is_file() or (base / "install.py").is_file():
                out = base / "patcheu"
                break
        if out is None:
            out = pathlib.Path.cwd() / "patcheu"

    out = out.resolve()
    cmd = [
        sys.executable,
        str(MAP),
        "--eu-iso",
        str(args.eu_iso),
        "--usa-data",
        str(args.usa_data),
        "--out",
        str(out),
    ]
    print(f"Building patcheu -> {out}")
    r = subprocess.run(cmd)
    if r.returncode != 0:
        return r.returncode

    (out / "README.txt").write_text(README, encoding="utf-8")
    # Marker so users see the pack name clearly
    (out / "PATCHEU").write_text(
        "Tenkaichi3Decomp Italian UI pack (from Europe disc SLES-549.45)\n",
        encoding="utf-8",
    )
    print()
    print(f"OK: {out}")
    print("  Copy the `patcheu` folder next to Tenkaichi3Decomp.exe (beside gamedata/).")
    print("  In game: F1, UI, Menu & dialogue text, Italian.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
