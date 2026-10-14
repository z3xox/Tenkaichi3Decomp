#!/usr/bin/env python3
"""Local development server for the browser build. Binds to 127.0.0.1 only: the game's data never leaves the machine.

  - serves a folder (default: port/build/web) with correct types for .wasm / .js / .mjs;
  - HTTP Range requests (206 Partial Content, one range), HEAD, Accept-Ranges and Content-Length on every file,
    so the game can read single files or parts of them on demand;
  - Cross-Origin-Opener-Policy / Cross-Origin-Embedder-Policy (cross-origin isolation, for SharedArrayBuffer and
    precise memory measurement) unless --no-isolation;
  - POST /__result?name=<n>: appends the request body to <folder>/__results/<n>.txt (test pages report here);
  - extra mounts: --mount /gamedata=D:/path/to/gamedata serves another folder under a URL prefix. A mount may lie
    inside another (--mount /gamedata/stages=D:/my/stages): the longest prefix wins;
  - GET <folder>/?list: the folder's entries as JSON (the browser build looks through the data folder with it).

Usage: python port/web/devserver.py [--port 8080] [--root DIR] [--mount /prefix=DIR]... [--no-isolation]"""
import argparse, http.server, json, os, pathlib, re, shutil, sys, urllib.parse

TYPES = {".wasm": "application/wasm", ".js": "text/javascript", ".mjs": "text/javascript", ".html": "text/html",
         ".json": "application/json", ".txt": "text/plain; charset=utf-8", ".css": "text/css", ".png": "image/png", ".svg": "image/svg+xml",
         ".data": "application/octet-stream", ".bin": "application/octet-stream"}

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    root = pathlib.Path(".")
    mounts = {}
    isolation = True

    def log_message(self, fmt, *args):
        if os.environ.get("DEVSERVER_QUIET") is None:
            sys.stderr.write("%s %s\n" % (self.command, fmt % args))

    def common_headers(self):
        if self.isolation:
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-cache")

    def resolve(self, path, folder_as_is=False):
        path = urllib.parse.unquote(path.split("?", 1)[0])
        for prefix in sorted(self.mounts, key=len, reverse=True):  # the longest prefix: a mount inside a mount wins
            if path == prefix or path.startswith(prefix + "/"):
                base, rel = self.mounts[prefix], path[len(prefix):]
                break
        else:
            base, rel = self.root, path
        f = (base / rel.lstrip("/")).resolve()
        if base.resolve() not in f.parents and f != base.resolve():
            return None  # outside the served folder
        if f.is_dir() and not folder_as_is:
            f = f / "index.html"
        return f

    def send_listing(self):
        """GET <folder>/?list: the folder's entries as JSON, [{"name", "dir", "size"}] (the browser build looks through
        the data folder with it: added stages, songs, texture packs). Mounts directly inside the folder are entries."""
        f = self.resolve(self.path, folder_as_is=True)
        if f is None or not f.is_dir():
            return self.send_error_text(404, "not a folder\n")
        entries = {e.name: {"name": e.name, "dir": e.is_dir(), "size": 0 if e.is_dir() else e.stat().st_size}
                   for e in f.iterdir()}
        here = urllib.parse.unquote(self.path.split("?", 1)[0]).rstrip("/")
        for prefix in self.mounts:
            parent, _, name = prefix.rpartition("/")
            if parent == here and name and self.mounts[prefix].is_dir():
                entries[name] = {"name": name, "dir": True, "size": 0}
        body = json.dumps(sorted(entries.values(), key=lambda e: e["name"])).encode()
        self.send_response(200)
        self.common_headers()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_error_text(self, code, text):
        body = text.encode()
        self.send_response(code)
        self.common_headers()
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        if urllib.parse.urlparse(self.path).query == "list":
            return self.send_listing()
        f = self.resolve(self.path)
        if f is None or not f.is_file():
            return self.send_error_text(404, "not found\n")
        size = f.stat().st_size
        start, end, partial = 0, size - 1, False
        rng = self.headers.get("Range")
        if rng:
            m = re.fullmatch(r"bytes=(\d*)-(\d*)", rng.strip())
            if not m or (m.group(1) == "" and m.group(2) == ""):
                return self.send_error_text(416, "bad range\n")
            if m.group(1) == "":  # suffix: the last N bytes
                start, end = max(0, size - int(m.group(2))), size - 1
            else:
                start = int(m.group(1))
                end = min(int(m.group(2)), size - 1) if m.group(2) else size - 1
            if start >= size or start > end:
                self.send_response(416)
                self.common_headers()
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            partial = True
        self.send_response(206 if partial else 200)
        self.common_headers()
        self.send_header("Content-Type", TYPES.get(f.suffix.lower(), "application/octet-stream"))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if partial:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if self.command == "HEAD":
            return
        with open(f, "rb") as fp:
            fp.seek(start)
            left = end - start + 1
            while left > 0:
                chunk = fp.read(min(left, 1 << 20))
                if not chunk:
                    break
                self.wfile.write(chunk)
                left -= len(chunk)

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        if u.path != "/__result":
            return self.send_error_text(404, "not found\n")
        name = re.sub(r"[^\w.-]", "_", urllib.parse.parse_qs(u.query).get("name", ["result"])[0])
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        out = self.root / "__results"
        out.mkdir(exist_ok=True)
        with open(out / f"{name}.txt", "ab") as fp:
            fp.write(body + b"\n")
        self.send_error_text(200, "ok\n")

class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        # the browser closing a connection (a reload, a closed tab, a read it no longer needs) is normal: not an error
        if isinstance(sys.exc_info()[1], (ConnectionResetError, ConnectionAbortedError, BrokenPipeError)):
            return
        super().handle_error(request, client_address)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[1] / "build/web"))
    ap.add_argument("--mount", action="append", default=[])
    ap.add_argument("--no-isolation", action="store_true")
    a = ap.parse_args()
    Handler.root = pathlib.Path(a.root)
    Handler.root.mkdir(parents=True, exist_ok=True)
    Handler.isolation = not a.no_isolation
    for m in a.mount:
        prefix, folder = m.split("=", 1)
        Handler.mounts["/" + prefix.strip("/")] = pathlib.Path(folder)
    srv = Server(("127.0.0.1", a.port), Handler)
    print(f"serving {Handler.root} on http://127.0.0.1:{a.port}/ (isolation {'on' if Handler.isolation else 'off'})", flush=True)
    srv.serve_forever()

if __name__ == "__main__":
    main()
