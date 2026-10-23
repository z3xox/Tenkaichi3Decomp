#!/usr/bin/env python3
"""The sizes of the Tenkaichi Budokai Online pictures into include/menu/tbo_sizes.h.

The game's code lays the pictures out by their sizes (Ps_PicW / Ps_PicH, src/menu/port_screen.c), so a picture
replaced by one of another size needs this run again:  python3 port/tools/tbo_sizes.py

The PC hub pictures are in port/tbo/ui; Wii picture sizes are preserved from the header after extraction.
The pictures are the list of include/menu/tbo_art.h, in its order. The third number of a piece of the kit (the
corner that keeps its shape when the piece is stretched, in pixels) is not in the file: it is kept from the header as
it is. A picture that is not there (one no screen draws any more) keeps its line too.
The first row of the Wii battle label textures also supplies the text bounds for Custom Battle (requires Pillow
when those extracted textures are present).
"""
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ART = ROOT / "port/tbo/ui"
LIST = ROOT / "include/menu/tbo_art.h"
OUT = ROOT / "include/menu/tbo_sizes.h"
WII = {
    "wii_pan_eyes_open": (2, 1),
    "wii_pan_eyes_closed": (2, 2),
    "wii_pan_mouth_open": (2, 3),
    "wii_pan_mouth_closed": (2, 4),
    "wii_pan_point": (2, 5),
    "wii_pan_point_eyes_closed": (2, 7),
    "wii_pan_point_mouth_open": (2, 8),
    "wii_pan_point_mouth_closed": (2, 9),
    "wii_giru_eye_bright": (2, 11),
    "wii_giru_eye_dim": (2, 12),
    "wii_settings_label_selected": (5, 2),
    "wii_settings_label_idle": (5, 5),
    "wii_settings_title": (4, 3),
}
WII_NESTED = {
    "wii_disconnect_giru": (10, 0, 1),
    "wii_disconnect_giru_eye": (10, 0, 2),
    "wii_name_title": (13, 2, 4),
    "wii_name_button_labels": (13, 2, 1),
    "wii_name_keyboard": (13, 1, 0),
    "wii_name_glyphs": (13, 2, 0),
    "wii_name_key_highlight": (13, 1, 7),
    "wii_name_key_wide_highlight": (13, 1, 6),
    "wii_name_delete_x": (13, 1, 1),
    "wii_name_cursor": (13, 1, 10),
    "wii_name_input_frame": (13, 1, 9),
}
WII_BATTLE = {
    "wii_battle_title": (4, 1),
    "wii_battle_nebula": (3, 2),
    "wii_battle_stars_0": (3, 0),
    "wii_battle_stars_1": (3, 1),
    "wii_battle_bar_blue": (3, 26),
    "wii_battle_bar_gold": (3, 27),
    "wii_battle_label_idle": (3, 29),
    "wii_battle_label_selected": (3, 30),
    "wii_battle_giru": (2, 0),
    "wii_battle_giru_eye_bright": (2, 1),
    "wii_battle_giru_eye_dim": (2, 2),
    "wii_battle_point_panel": (3, 6),
    "wii_battle_point_label": (3, 41),
    "wii_battle_point_digits": (3, 5),
    "wii_custom_title": (4, 2),
    "wii_normal_title": (4, 3),
    "wii_result_win": (3, 23),
    "wii_result_badge": (3, 25),
    "wii_result_label_idle": (3, 3),
    "wii_result_label_selected": (3, 4),
    "wii_result_bar_blue": (3, 17),
    "wii_result_bar_gold": (3, 18),
    "wii_vs_panels": (3, 20),
    "wii_vs_mark": (3, 40),
    "wii_vs_lines": (3, 21),
    "wii_vs_players": (3, 22),
    "wii_vs_digits": (3, 38),
    "wii_vs_wins_digits": (3, 38),
    "wii_vs_record_labels": (3, 42),
}
WII_BATTLE.update({f"wii_battle_bg_{i}": (1, i) for i in range(8)})


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        raise SystemExit(f"{path}: not a PNG")
    return struct.unpack(">II", head[16:24])


def first_label_row(path):
    from PIL import Image
    with Image.open(path) as im:
        bounds = im.convert("RGBA").getchannel("A").crop((0, 0, im.width, 32)).getbbox()
    if bounds is None:
        raise SystemExit(f"{path}: the first label row is empty")
    return bounds


def first_label_fill(path):
    from PIL import Image
    with Image.open(path) as source:
        im = source.convert("RGBA").crop((0, 0, source.width, 32))
    pixels = im.load()
    # The idle sheet's light-gray interior is distinct from its purple outline.
    points = [(x, y) for y in range(im.height) for x in range(im.width)
              if pixels[x, y][3] > 128 and min(pixels[x, y][:3]) >= 140]
    if not points:
        raise SystemExit(f"{path}: the first label row has no light-gray fill")
    return min(x for x, y in points), min(y for x, y in points), max(x for x, y in points) + 1, max(y for x, y in points) + 1


def main():
    names = re.findall(r'X\(\s*\w+\s*,\s*"([^"]+)"\s*\)', LIST.read_text())
    old = {}
    previous = OUT.read_text() if OUT.exists() else ""
    if OUT.exists():
        for w, h, c, name in re.findall(r"\{(\d+), (\d+), (\d+)\}, /\* (\S+) \*/", previous):
            old[name] = (int(w), int(h), int(c))
    lines = []
    for name in names:
        path = ART / f"{name}.png"
        corner = old.get(name, (0, 0, 0))[2]
        if path.exists():
            w, h = png_size(path)
        elif name in WII:
            resource, texture = WII[name]
            w, h = png_size(ROOT / f"gamedata/dragon-net-battle/section-00/resource-{resource:03}-textures/texture-{texture:03}.png")
        elif name in WII_NESTED:
            resource, part, texture = WII_NESTED[name]
            w, h = png_size(ROOT / f"gamedata/dragon-net-battle/section-00/resource-{resource:03}-part-{part:02}-textures/texture-{texture:03}.png")
        elif name in WII_BATTLE:
            resource, texture = WII_BATTLE[name]
            w, h = png_size(ROOT / f"gamedata/dragon-net-battle/section-01/resource-{resource:03}-textures/texture-{texture:03}.png")
        elif name in old:
            w, h = old[name][:2]
        else:
            raise SystemExit(f"{path}: missing, and not in {OUT.name} before")
        lines.append(f"    {{{w}, {h}, {corner}}}, /* {name} */")
    label_rows = []
    for name, texture in (("wii_battle_label_idle", 29), ("wii_battle_label_selected", 30)):
        path = ROOT / f"gamedata/dragon-net-battle/section-01/resource-003-textures/texture-{texture:03}.png"
        if path.exists():
            bounds = first_label_row(path)
        else:
            match = re.search(r"\{(\d+), (\d+), (\d+), (\d+)\}, /\* " + name + r" row 0 \*/", previous)
            if match is None:
                raise SystemExit(f"{path}: missing label texture and no saved bounds in {OUT.name}")
            bounds = tuple(map(int, match.groups()))
        label_rows.append(f"    {{{bounds[0]}, {bounds[1]}, {bounds[2]}, {bounds[3]}}}, /* {name} row 0 */")
    idle_path = ROOT / "gamedata/dragon-net-battle/section-01/resource-003-textures/texture-029.png"
    if idle_path.exists():
        fill = first_label_fill(idle_path)
    else:
        match = re.search(r"sTboWiiBattleLabelFillRow0\[4\] = \{(\d+), (\d+), (\d+), (\d+)\};", previous)
        if match is None:
            raise SystemExit(f"{idle_path}: missing label texture and no saved fill bounds in {OUT.name}")
        fill = tuple(map(int, match.groups()))
    cap_heights = []
    for section, resource, texture in ((0, 5, 3), (1, 3, 29), (0, 5, 5)):
        path = ROOT / f"gamedata/dragon-net-battle/section-{section:02}/resource-{resource:03}-textures/texture-{texture:03}.png"
        if path.exists():
            bounds = first_label_fill(path)
            cap_heights.append(bounds[3] - bounds[1])
        else:
            match = re.search(r"sTboWiiLabelCapHeight\[3\] = \{(\d+), (\d+), (\d+)\};", previous)
            if match is None:
                raise SystemExit(f"{path}: missing label texture and no saved cap heights in {OUT.name}")
            cap_heights = list(map(int, match.groups()))
            break
    OUT.write_text("/* Generated by port/tools/tbo_sizes.py: sizes for include/menu/tbo_art.h. Wii sizes come from the extractor. */\n"
                   "#ifndef TBO_SIZES_H\n#define TBO_SIZES_H\n\n"
                   "/* width, height in pixels, and for the pieces of the kit the corner that keeps its shape */\n"
                   "static const u16 sTboSize[TBO_ART_COUNT][3] = {\n" + "\n".join(lines) + "\n};\n\n"
                   "/* First-row alpha bounds, x0/y0 inclusive and x1/y1 exclusive, in the Wii label sheets. */\n"
                   "static const u16 sTboWiiBattleLabelRow0[2][4] = {\n" + "\n".join(label_rows) + "\n};\n"
                   "/* Light-gray letter fill inside the idle row, excluding its purple outline. */\n"
                   f"static const u16 sTboWiiBattleLabelFillRow0[4] = {{{fill[0]}, {fill[1]}, {fill[2]}, {fill[3]}}};\n"
                   "/* First-row letter-fill heights: home, battle, settings. */\n"
                   f"static const u16 sTboWiiLabelCapHeight[3] = {{{cap_heights[0]}, {cap_heights[1]}, {cap_heights[2]}}};\n\n#endif\n")
    print(f"{OUT.relative_to(ROOT)}: {len(lines)} pictures")


if __name__ == "__main__":
    sys.exit(main())
