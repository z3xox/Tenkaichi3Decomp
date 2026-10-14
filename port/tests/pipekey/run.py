#!/usr/bin/env python3
"""Builds and runs the pipeline-key test (test.c with port/src/gs/gs_pipekey.c) with emcc under node.
Usage: python port/tests/pipekey/run.py   (emcc and node on PATH)"""
import pathlib, subprocess, sys

D = pathlib.Path(__file__).resolve().parent
ROOT = D.parents[2]
OUT = ROOT / "port/build/pipekey_test.js"

r = subprocess.run(["emcc", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", f"-I{ROOT / 'port/src/gs'}",
                    str(D / "test.c"), str(ROOT / "port/src/gs/gs_pipekey.c"), "-o", str(OUT)], capture_output=True, text=True)
if r.returncode:
    print(r.stderr)
    sys.exit(2)
sys.exit(subprocess.run(["node", str(OUT)]).returncode)
