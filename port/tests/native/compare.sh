#!/bin/bash
# Native reference check: do two revisions of this repository build and behave the same natively? Used before any
# change to a file the native builds share with the browser build (docs/BROWSER_MIGRATION_PLAN.md, M0 / M9).
#   port/tests/native/compare.sh <rev A> <rev B> <gamedata folder> [work folder]
# Needs docker and git; the game data is the user's own (mounted read-only, never copied, never in the image).
# For each revision, in the release container (port/release/Dockerfile, built if missing):
#   - Linux x86-64 (BT3_CC=clang64, as the Linux release): build; the attract demo without a window (BT3_GS=none), its
#     fight trace (BT3_TRACE=60); then rendered on a virtual display with Mesa's software drivers (OpenGL: llvmpipe,
#     Vulkan: lavapipe), a screenshot every 150 frames (BT3_SHOT), each run stopped when its last screenshot is written
#     (OpenGL: frame 450, Vulkan: frame 2100; in_container.sh);
#   - Windows x86-64 (BT3_CC=win64, as the Windows release): build; with RUN_WINDOWS=1, run here without a window.
# Then compares A with B: traces line for line, screenshots byte for byte. Exit status 0 only if everything compared is
# identical and every step ran. REUSE=1 keeps the results of a revision already run in the same work folder.
set -euo pipefail
A=$1; B=$2; DATA=$3; WORK=${4:-$(mktemp -d)}
RUN_WINDOWS=${RUN_WINDOWS:-0}
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../../.." && pwd)
SECONDS_HEADLESS=150 SECONDS_RENDERED=900  # (rendered: the limit for a run that does not reach its last frame)
declare -A LAST_FRAME=([gl]=450 [vulkan]=2100) # the rendered runs' last screenshot (in_container.sh)
IMAGE=bt3-port-release:jammy
docker_ok() { # Docker Desktop stops when left idle: say so plainly instead of a failure halfway through
    docker info >/dev/null 2>&1 || { echo "Docker is not running (start Docker Desktop): the native check needs it" >&2; exit 2; }
}
docker_ok
docker image inspect $IMAGE >/dev/null 2>&1 || docker build -t $IMAGE "$REPO/port/release"
mkdir -p "$WORK/out"
host() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }  # (Git Bash on Windows)
for rev in "$A" "$B"; do
    # REUSE=1: a revision whose results are already in the work folder (a commit does not change) is not run again
    if [ "${REUSE:-0}" = 1 ] && [ -s "$WORK/out/${rev}_headless.trace.txt" ] && [ -d "$WORK/out/${rev}_vulkan.shots" ] &&
       { [ "$RUN_WINDOWS" != 1 ] || [ -s "$WORK/out/${rev}_win64.trace.txt" ]; }; then
        echo "$rev: results reused from $WORK/out"
        continue
    fi
    src="$WORK/src_$rev"
    rm -rf "$src" && git -c core.autocrlf=false clone -q "$REPO" "$src" && git -C "$src" checkout -q "$rev"
    docker_ok
    MSYS_NO_PATHCONV=1 docker run --rm -v "$(host "$src"):/src" -v "$(host "$DATA"):/data:ro" -v "$(host "$WORK/out"):/out" \
        -v "$(host "$HERE"):/native:ro" $IMAGE sh /native/in_container.sh "$rev" $SECONDS_HEADLESS $SECONDS_RENDERED
    if [ "$RUN_WINDOWS" = 1 ]; then
        (cd "$WORK/out/${rev}_win64" && BT3_DATA="$DATA" BT3_GS=none BT3_DEMO=1 BT3_TRACE=60 \
            timeout $SECONDS_HEADLESS ./bt3.exe > ../${rev}_win64.log 2>&1 || true)
        grep "^vb " "$WORK/out/${rev}_win64.log" > "$WORK/out/${rev}_win64.trace.txt" || true
    fi
done
cd "$WORK/out"
status=0
same_trace() { # <name>: A's and B's traces (B's may be cut where A's run stopped, and the other way round)
    local a=${A}_$1.trace.txt b=${B}_$1.trace.txt n
    n=$(( $(wc -l < $a) < $(wc -l < $b) ? $(wc -l < $a) : $(wc -l < $b) ))
    if [ "$n" -gt 0 ] && cmp -s <(head -n $n $a) <(head -n $n $b); then echo "$1 trace: $n lines identical"
    else echo "$1 trace: DIFFERENT or empty ($n lines compared)"; status=1; fi
}
same_trace headless
for api in gl vulkan; do
    same_trace $api
    n=0
    for ((frame = 0; frame <= ${LAST_FRAME[$api]}; frame += 150)); do  # every screenshot both runs must have
        name=$(printf 'gpu_%05d.ppm' $frame)
        f=${A}_$api.shots/$name g=${B}_$api.shots/$name
        if [ ! -f "$f" ] || [ ! -f "$g" ]; then
            echo "$api: frame $frame not reached by $([ -f "$f" ] || echo "$A ")$([ -f "$g" ] || echo "$B")(the run's limit: ${SECONDS_RENDERED} s)"; status=1
        elif cmp -s "$f" "$g"; then n=$((n + 1)); else echo "$api: $name DIFFERENT"; status=1; fi
    done
    echo "$api screenshots: $n identical"
done
[ "$RUN_WINDOWS" = 1 ] && same_trace win64
echo "results in $WORK/out"
exit $status
