#!/usr/bin/env python3
"""The online screens' pictures (port/tbo/ui) in the fork's colours.

    .venv/bin/python port/tools/onfly_theme.py        (needs Pillow: .venv/bin/pip install pillow)

The colours are the ones the company's public site uses (read from its style sheets, 2026-11-14): the blue #009EFB
with its dark #0062AB and light #97CCFD, the orange of its buttons #FF5A05, the dark #192A3D and #121212, the cream
#FAF9F5. The interface pieces (bars, capsules, window, box) are drawn here from nothing, flat and round-ended as the site's
buttons and cards are, at the sizes of the pictures the screens came with (so the screens' slicing of ends and
corners still fits). The Dragon Radar keeps its drawing (read as it is in the commit ORIGIN), turned from green to
the blue. The background is drawn here: the blue, a globe's lines and flight paths.
The sizes do not change, so include/menu/tbo_sizes.h stays as it is.
"""
import io
import math
import pathlib
import subprocess

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parents[2]
ART = ROOT / "port/tbo/ui"
ORIGIN = "46ccfa3"  # the commit whose pictures are the ones recoloured

BLUE, BLUE_DARK, BLUE_LIGHT, BLUE_PALE = (0, 158, 251), (0, 98, 171), (151, 204, 253), (222, 240, 255)
ORANGE, ORANGE_DARK = (255, 90, 5), (215, 54, 0)
NAVY, INK, CREAM = (25, 42, 61), (18, 18, 18), (250, 249, 245)

def original(name):
    data = subprocess.run(["git", "show", f"{ORIGIN}:port/tbo/ui/{name}.png"], cwd=ROOT, capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(data)).convert("RGBA")


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


SIZES = {"kit_bar": (1273, 100), "kit_sel": (1280, 107), "kit_gold": (306, 81), "kit_steel": (306, 75),
         "kit_window": (465, 224), "kit_dialog": (1273, 204)}  # as the pictures the screens came with (tbo_sizes.h)
K = 4  # drawn this many times larger and scaled down (smooth edges)


def flat(name, fill, radius=None, border=None, ring=None):
    """One flat rounded shape filling the picture: a bar (radius None: half its height) or a box. `border` a thin
    line inside its edge, `ring` a white line round it (the chosen bar)."""
    w, h = SIZES[name]
    im = Image.new("RGBA", (w * K, h * K), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = 3 * K
    r = (h * K - 2 * m) // 2 if radius is None else radius * K
    if ring is not None:
        d.rounded_rectangle((m, m, w * K - m, h * K - m), r, fill=ring)
        m += 5 * K
        r = max(r - 5 * K, 0)
    d.rounded_rectangle((m, m, w * K - m, h * K - m), r, fill=fill)
    if border is not None:
        d.rounded_rectangle((m, m, w * K - m, h * K - m), r, outline=border, width=2 * K)
    im.resize((w, h), Image.LANCZOS).save(ART / f"{name}.png", optimize=True)


def kit():
    # flat, round-ended, one colour each, as the site's buttons and cards are: no gloss, no pattern, no ornaments
    flat("kit_bar", (30, 62, 96, 255))                                   # a row: a blue darker than the page
    flat("kit_sel", ORANGE + (255,), ring=(255, 255, 255, 255))          # the chosen row: the site's button orange
    flat("kit_gold", ORANGE_DARK + (255,))                               # a marked row, a dialog's button
    flat("kit_steel", (87, 99, 113, 255))                                # a tag ("EM BREVE", "GESTOR")
    flat("kit_window", (12, 24, 40, 242), radius=30, border=(151, 204, 253, 90))   # a card
    flat("kit_dialog", (18, 18, 18, 236), radius=40, border=BLUE + (255,))


def radar():
    im = original("radar")
    h, s, v = im.convert("RGB").convert("HSV").split()
    blue = Image.merge("HSV", (h.point(lambda x: (x + 84) % 256), s, v)).convert("RGB")  # green (about 85) to the blue (about 144 of 255)
    blue.putalpha(im.getchannel("A"))
    blue.save(ART / "radar.png", optimize=True)


def background():
    W, H, K = 1280, 720, 2  # drawn at twice the size and scaled down (smooth lines)
    im = Image.new("RGB", (W * K, H * K))
    px = im.load()
    for y in range(H * K):
        for x in range(0, W * K):
            t = min(max((0.35 * x / (W * K) + 0.65 * y / (H * K)), 0.0), 1.0)
            px[x, y] = lerp((0, 70, 128), BLUE, t)
    lines = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lines)
    cx, cy, r = int(0.72 * W * K), int(0.46 * H * K), int(0.36 * H * K)
    white = (255, 255, 255, 70)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=white, width=5 * K)
    for f in (0.36, 0.72):  # meridians, and the parallels
        d.ellipse((cx - int(r * f), cy - r, cx + int(r * f), cy + r), outline=white, width=3 * K)
    d.line((cx, cy - r, cx, cy + r), fill=white, width=3 * K)
    for f in (-0.5, 0.0, 0.5):
        half = int(r * math.sqrt(1 - f * f))
        d.line((cx - half, cy + int(r * f), cx + half, cy + int(r * f)), fill=white, width=3 * K)
    orbit = int(r * 1.28)  # the dashed path round the globe, and the dot that flies it
    for k in range(0, 360, 6):
        if 20 <= k <= 60:
            continue
        d.arc((cx - orbit, cy - orbit, cx + orbit, cy + orbit), k, k + 3, fill=(255, 255, 255, 90), width=5 * K)
    a = math.radians(-50)
    d.ellipse((cx + orbit * math.cos(a) - 12 * K, cy + orbit * math.sin(a) - 12 * K, cx + orbit * math.cos(a) + 12 * K, cy + orbit * math.sin(a) + 12 * K), fill=(255, 255, 255, 150))
    for x0, y0, x1, y1, bend in ((-0.05, 0.30, 0.50, 0.08, 0.20), (0.02, 0.86, 0.60, 0.98, -0.16)):  # two flight paths, dashed
        for i in range(0, 60, 2):
            t0, t1 = i / 60.0, (i + 1) / 60.0
            p = [((x0 + (x1 - x0) * t) * W * K, (y0 + (y1 - y0) * t - bend * math.sin(math.pi * t)) * H * K) for t in (t0, t1)]
            d.line(p, fill=(255, 255, 255, 40), width=3 * K)
    im = Image.alpha_composite(im.convert("RGBA"), lines).convert("RGB").resize((W, H), Image.LANCZOS)
    im.save(ART / "bg.png", optimize=True)


def main():
    kit()
    radar()
    background()
    print("port/tbo/ui: " + ", ".join(list(SIZES) + ["radar", "bg"]))


if __name__ == "__main__":
    main()
