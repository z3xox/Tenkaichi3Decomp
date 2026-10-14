#!/usr/bin/env python3
"""Unpacks the user's own disc image into loose folders for the PC build.

    port/tools/extract_disc.py <disc.iso> [out_dir]        (out_dir default: gamedata/)

Result: out_dir/pzs3us0/00000.bin ..., out_dir/pzs3us1/..., out_dir/pzs3us2/...: one file per entry of each AFS
archive, named by its index (the id the game asks for), plus the loose disc files under out_dir/disc/.
A file placed in out_dir/mods/<same path> is used instead of the original (see port/src/plat_file.c).
Needs 7-Zip to read the disc image (on the PATH, or installed in Program Files on Windows); on macOS its own bsdtar
does without. Nothing extracted may be committed or redistributed."""
import os, pathlib, shutil, struct, subprocess, sys

def disc_reader():
    """7z on the PATH, or where the Windows installer puts it (it does not add itself to the PATH); on macOS without
    7-Zip, the system's own bsdtar (it reads disc images)."""
    found = shutil.which("7z") or shutil.which("7za")
    if found:
        return found
    for base in (os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)")):
        if base and pathlib.Path(base, "7-Zip", "7z.exe").is_file():
            return str(pathlib.Path(base, "7-Zip", "7z.exe"))
    if sys.platform == "darwin" and shutil.which("bsdtar") is not None:
        return "bsdtar"
    raise SystemExit("7-Zip is needed to read the disc image: install it from https://www.7-zip.org/ "
                     "(or put 7z on the PATH), then run this again.")

def split_afs(afs, out):
    out.mkdir(parents=True, exist_ok=True)
    with open(afs, "rb") as f:
        magic, count = struct.unpack("<4sI", f.read(8))
        if magic != b"AFS\0":
            raise SystemExit(f"{afs}: not an AFS archive")
        table = struct.unpack(f"<{count * 2}I", f.read(count * 8))
        for i in range(count):
            f.seek(table[i * 2])
            (out / f"{i:05d}.bin").write_bytes(f.read(table[i * 2 + 1]))
    return count

def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    iso = sys.argv[1]
    out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "gamedata")
    if not pathlib.Path(iso).is_file():
        raise SystemExit(f"{iso}: no such file (give the path of your disc image, an .iso)")
    tool = disc_reader()
    disc = out / "disc"
    disc.mkdir(parents=True, exist_ok=True)
    if tool == "bsdtar":
        subprocess.run(["bsdtar", "-xf", iso, "-C", str(disc), "DATA", "BIN", "SLUS_216.78"], check=True, stdout=subprocess.DEVNULL)
    else:
        subprocess.run([tool, "x", "-y", f"-o{disc}", iso, "DATA", "BIN", "SLUS_216.78"], check=True,
                       stdout=subprocess.DEVNULL)
    for afs in sorted((disc / "DATA").glob("*.AFS")):
        n = split_afs(afs, out / afs.stem.lower())
        print(f"{afs.name}: {n} files")
        afs.unlink()  # the loose files replace it
    print(f"done: {out}")

main()
