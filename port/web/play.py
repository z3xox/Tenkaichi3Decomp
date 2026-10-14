#!/usr/bin/env python3
"""Starts the game in the browser: the local server (127.0.0.1 only: nothing leaves this computer) with the page and
your data folder, then opens the page. Stop it with Ctrl+C.

    python play.py [data folder]     (default: the "gamedata" folder next to this file)

In a release package this file sits beside web/ and devserver.py; in the repository it serves port/build/web."""
import os, pathlib, socket, sys, threading, webbrowser

HERE = pathlib.Path(__file__).resolve().parent
PORTS = range(8080, 8090)  # the first free one is used
CHECK = pathlib.Path("disc/SLUS_216.78")  # a file every data folder made by extract_disc.py has


def free_port():
    for p in PORTS:
        with socket.socket() as s:
            try:
                s.bind(("127.0.0.1", p))
                return p
            except OSError:
                continue
    sys.exit(f"No free port between {PORTS.start} and {PORTS.stop - 1}: close the program using them, or the other game window.")


def main():
    data = pathlib.Path(sys.argv[1]).expanduser().resolve() if len(sys.argv) > 1 else HERE / "gamedata"
    if not (data / CHECK).is_file():
        sys.exit(f"No game data in {data}.\n"
                 f"Unpack your own disc image there first:  python extract_disc.py <your disc image> {data}\n"
                 f"or give the folder:  python play.py <data folder>")
    web = HERE / "web" if (HERE / "web" / "index.html").is_file() else HERE.parents[1] / "port/build/web"
    if not (web / "bt3.wasm").is_file():
        sys.exit(f"The page is not in {web}: build it first (port/tools/wasm_build.py).")
    port = free_port()
    url = f"http://127.0.0.1:{port}/index.html"
    print(f"Game data: {data}\nOpening {url} (use Chrome or Edge). Keep this window open while playing; Ctrl+C stops it.", flush=True)
    threading.Timer(1.0, lambda: webbrowser.open(url)).start()
    os.environ.setdefault("DEVSERVER_QUIET", "1")  # (not every request on the screen)
    sys.path.insert(0, str(HERE))
    import devserver
    sys.argv = ["devserver.py", "--port", str(port), "--root", str(web), "--mount", f"gamedata={data}"]
    try:
        devserver.main()
    except KeyboardInterrupt:
        print("Stopped.")


if __name__ == "__main__":
    main()
