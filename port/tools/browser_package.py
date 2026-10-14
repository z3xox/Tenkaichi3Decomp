#!/usr/bin/env python3
"""Packages the browser build (port/build/web) as a release folder and a .zip, for local use. Nothing is uploaded.

    python port/tools/browser_package.py <release name> [out folder]     (default out: port/build/release_browser)

Contents (an explicit list: anything else in port/build/web, such as test results, is left out):
  web/            the page and the program: index.html, bt3.js, bt3.wasm and the page's scripts
  play.py         starts the game: checks the data folder, runs the local server, opens the page
  devserver.py    the local server (HTTP Range, folder listings, cross-origin isolation), 127.0.0.1 only
  extract_disc.py unpacks the user's own disc into a data folder
  README.md       docs/BROWSER_RELEASE.md
  licenses/       the project's licence and those of what the program contains (SDL3, Dawn's webgpu.h, Emscripten's
                  runtime, newlib's libm, xxHash)
No game data: the program is built with the data tables' shapes only (port/data), filled from the user's disc at start;
the package is checked for any file from a data folder and fails if one is there.
"""
import os, pathlib, shutil, sys, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
WEB = ROOT / "port/build/web"
PAGE = ["index.html", "bt3.js", "bt3.wasm", "bt3_data.js", "settings.js", "dashboard.js", "controllers.js", "toasts.js", "saves.js", "touch.js", "gamepad.svg"]
EMSDK = pathlib.Path(os.environ.get("EMSDK", ROOT.parent / "tools/emsdk"))
LICENSES = {
    "Tenkaichi3Decomp-AGPL.txt": ROOT / "LICENSE",       # (AGPL-3.0-only from the original project's 06e4671 on)
    "Tenkaichi3Decomp-MIT.txt": ROOT / "LICENSE-MIT",    # (the code as it was up to then)
    "SDL3-LICENSE.txt": EMSDK / "upstream/emscripten/cache/ports/sdl3/SDL-release-3.4.2/LICENSE.txt",
    "Dawn-webgpu-LICENSE.txt": EMSDK / "upstream/emscripten/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu_cpp/LICENSE",
    "Emscripten-LICENSE.txt": EMSDK / "upstream/emscripten/LICENSE",
    "newlib-libm-COPYING.txt": ROOT / "port/third_party/newlib_libm/COPYING.NEWLIB",
    "xxHash-LICENSE.txt": ROOT / "port/third_party/xxhash/LICENSE",
}
# names that only a game data folder has (port/tools/extract_disc.py's layout): the package must hold none of them
DATA_MARKERS = ("pzs3us0", "pzs3us1", "pzs3us2", "SLUS_216.78", "DBZP.BIN", ".afs", ".pss", ".adx", ".unk")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    name = sys.argv[1]
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "port/build/release_browser"
    missing = [f for f in PAGE if not (WEB / f).is_file()] + [str(p) for p in LICENSES.values() if not p.is_file()]
    if missing:
        sys.exit("not packaged; missing: " + ", ".join(missing) + " (build first: port/tools/wasm_build.py)")
    folder = out / name
    if folder.exists():
        shutil.rmtree(folder)
    (folder / "web").mkdir(parents=True)
    (folder / "licenses").mkdir()
    for f in PAGE:
        shutil.copy2(WEB / f, folder / "web" / f)
    shutil.copy2(ROOT / "port/web/devserver.py", folder / "devserver.py")
    shutil.copy2(ROOT / "port/web/play.py", folder / "play.py")
    shutil.copy2(ROOT / "port/tools/extract_disc.py", folder / "extract_disc.py")
    shutil.copy2(ROOT / "docs/BROWSER_RELEASE.md", folder / "README.md")
    for dst, src in LICENSES.items():
        shutil.copy2(src, folder / "licenses" / dst)
    files = sorted(p for p in folder.rglob("*") if p.is_file())
    bad = [p for p in files if any(m.lower() in p.name.lower() for m in DATA_MARKERS)]
    if bad:
        shutil.rmtree(folder)
        sys.exit("not packaged: game data in the package: " + ", ".join(str(p) for p in bad))
    archive = out / (name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, pathlib.Path(name) / p.relative_to(folder))
    for p in files:
        print(f"{p.stat().st_size:>10}  {p.relative_to(folder).as_posix()}")
    print(f"{archive} ({archive.stat().st_size} bytes, {len(files)} files)")


if __name__ == "__main__":
    main()
