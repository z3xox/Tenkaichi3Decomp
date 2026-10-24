"""Build/run offline rc_client integration tests. Requires SDL3 and a C compiler.

CC selects the compiler. SDL3_CFLAGS and SDL3_LIBS override pkg-config, or the
bundled retcomm compiler's sibling deps directory on Windows. No live network
implementation is compiled into this test executable.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import sys
import json

root = Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"), posix=os.name != "nt")
compiler = shutil.which(cc[0])
if not compiler:
    raise SystemExit("Set CC to an available C compiler")
deps = Path(compiler).resolve().parent.parent / "deps"
if "SDL3_CFLAGS" in os.environ:
    cflags = shlex.split(os.environ["SDL3_CFLAGS"])
    libs = shlex.split(os.environ["SDL3_LIBS"])
elif os.name == "nt" and (deps / "include/SDL3/SDL.h").exists():
    cflags = ["-I" + str(deps / "include")]
    libs = ["-L" + str(deps / "lib"), "-lSDL3", "-lm", "-lkernel32", "-luser32",
            "-lgdi32", "-lwinmm", "-limm32", "-lole32", "-loleaut32", "-lversion",
            "-luuid", "-ladvapi32", "-lsetupapi", "-lshell32", "-ldinput8", "-lwinpthread"]
else:
    cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
    libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl3"], text=True))
    libs += ["-lm", "-pthread"]
ra = root / "port/third_party/rcheevos"
sources = [root / "port/tests/ra_client.c", root / "port/src/gs/ra_client.c"]
reference = None
if len(sys.argv) > 1:
    reference = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))["PatchData"]
    sources[0] = root / "port/tests/ra_reference.c"
    sources += [root / "port/src/gs/ra_game.c", root / "port/src/ra_memory.c"]
sources += [ra / "src" / (name + ".c") for name in ("rc_client", "rc_compat", "rc_util", "rc_version")]
sources += list((ra / "src/rapi").glob("*.c")) + list((ra / "src/rcheevos").glob("*.c"))
sources += [ra / "src/rhash/md5.c"]
with tempfile.TemporaryDirectory(prefix="bt3-ra-test-", dir=os.environ.get("BT3_TEST_TMPDIR")) as tmp:
    executable = Path(tmp) / ("ra_client.exe" if os.name == "nt" else "ra_client")
    command = cc + ["-std=c11", "-DRC_DISABLE_LUA", "-DRA_CLIENT_TEST",
                    "-I" + str(root / "port/src/gs"), "-I" + str(ra / "include")]
    subprocess.run(command + cflags + list(map(str, sources)) + libs + ["-o", str(executable)], check=True)
    arguments = [str(executable)]
    environment = None
    if reference:
        achievements = [a for a in reference["Achievements"] if a["ID"] != 101000001]
        award = next(a["ID"] for a in achievements if a["Title"] == "World Tournament Champ")
        response = {"Success": True, "GameId": reference["ID"], "Title": reference["Title"],
                    "ConsoleId": reference["ConsoleID"], "ImageIconUrl": reference["ImageIconURL"],
                    "RichPresenceGameId": reference["ID"], "RichPresencePatch": "",
                    "Sets": [{"AchievementSetId": 1111, "GameId": reference["ID"], "Title": None,
                              "Type": "core", "ImageIconUrl": reference["ImageIconURL"],
                              "Achievements": achievements, "Leaderboards": []}]}
        fixture = Path(tmp) / "reference.json"
        fixture.write_text(json.dumps(response), encoding="utf-8")
        (Path(tmp) / "disc").mkdir()
        (Path(tmp) / "disc/SLUS_216.78").write_bytes(b"\x7fELF" + bytes(60))
        arguments += [tmp, str(fixture), str(len(achievements)), str(award)]
        environment = {k: v for k, v in os.environ.items() if not k.startswith("BT3_")}
        environment["BT3_SAVES"] = str(Path(tmp) / "clean-saves")
    subprocess.run(arguments, check=True, timeout=30, env=environment)
