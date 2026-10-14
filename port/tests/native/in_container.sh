#!/bin/sh
# Runs INSIDE the release container for port/tests/native/compare.sh: a checkout at /src (its port/build is written),
# the user's game data at /data (read-only), results to /out.
#   sh in_container.sh <label> <seconds headless> <seconds rendered>
set -e
LABEL=$1; SECONDS_HEADLESS=$2; SECONDS_RENDERED=$3
cd /src
export BT3_SKELETON=1
mkdir -p port/build
build() { # <BT3_CC>: port/release/inside.sh's build of one variant
    BT3_CC=$1 python3 port/tools/undefined.py > port/build/compile_$1.txt 2>&1 || { tail -5 port/build/compile_$1.txt; exit 1; }
    if grep -q "^FAILED" port/build/compile_$1.txt; then grep "^FAILED" port/build/compile_$1.txt | head; exit 1; fi
    BT3_CC=$1 python3 port/tools/link.py > port/build/link_$1.txt 2>&1 || { cat port/build/link_$1.txt; exit 1; }
    echo "$LABEL $1: $(tail -1 port/build/link_$1.txt | cut -c1-120)"
}
trace() { grep "^vb " "$1" > "$2" || true; }

echo "$LABEL: $(clang --version | head -1); $(x86_64-w64-mingw32-gcc --version | head -1)"
build clang64
BT3_DATA=/data BT3_GS=none BT3_DEMO=1 BT3_TRACE=60 timeout "$SECONDS_HEADLESS" ./port/build/bt3_64 > /out/${LABEL}_headless.log 2>&1 || true
trace /out/${LABEL}_headless.log /out/${LABEL}_headless.trace.txt

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null && apt-get install -y -qq xvfb libgl1-mesa-dri mesa-vulkan-drivers libvulkan1 >/dev/null
Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 &
sleep 2
# Each rendered run stops at a frame, not after a time: when its last screenshot is written (OpenGL on llvmpipe is
# slow, and how far it got in a fixed time varied between runs: 11 to 13 trace lines). <seconds rendered> is only the
# limit for a run that does not get there; compare.sh then says which frame was not reached.
for API in gl vulkan; do
    case $API in gl) LAST=450 ;; vulkan) LAST=2100 ;; esac
    rm -rf port/build/shots && mkdir -p port/build/shots
    DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 BT3_DATA=/data BT3_GS=gpu BT3_GPU_API=$API BT3_DEMO=1 \
        BT3_SHOT=150 BT3_SHOT_TO=$LAST BT3_TRACE=60 ./port/build/bt3_64 > /out/${LABEL}_$API.log 2>&1 &
    PID=$!
    WAITED=0
    LAST_SHOT=$(printf 'port/build/shots/gpu_%05d.ppm' "$LAST")
    while [ ! -f "$LAST_SHOT" ] && [ "$WAITED" -lt "$SECONDS_RENDERED" ] && kill -0 "$PID" 2>/dev/null; do
        sleep 2
        WAITED=$((WAITED + 2))
    done
    sleep 2 # (the last screenshot written whole)
    kill "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    echo "$LABEL $API: $( [ -f "$LAST_SHOT" ] && echo "reached frame $LAST" || echo "did NOT reach frame $LAST") after ${WAITED} s"
    rm -rf /out/${LABEL}_$API.shots && mkdir -p /out/${LABEL}_$API.shots
    cp port/build/shots/*.ppm /out/${LABEL}_$API.shots/ 2>/dev/null || true
    trace /out/${LABEL}_$API.log /out/${LABEL}_$API.trace.txt
done

build win64
mkdir -p /out/${LABEL}_win64
cp port/build/bt3.exe port/build/bt3.dat "$BT3_SDL3/bin/SDL3.dll" /out/${LABEL}_win64/
