#!/usr/bin/env python3
"""Extract Dragon Net Battle from a Wii ISO, RVZ or an already extracted wzs3*1.afs.

The PAL archive has several language copies. The first DragonNe entry is English.
Output is placed in gamedata/dragon-net-battle (ignored by Git). Wii ISO or RVZ input requires dolphin-tool.
"""

import argparse
from contextlib import nullcontext
import html
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


def table(data, label):
    if len(data) < 8:
        raise ValueError(f"{label}: short offset table")
    count = struct.unpack_from(">I", data)[0]
    if not 0 < count <= 4096 or 8 + 4 * count > len(data):
        raise ValueError(f"{label}: invalid count")
    values = struct.unpack_from(f">{count + 1}I", data, 4)
    if values[0] < 8 + 4 * count or values[-1] > len(data) or any(a >= b for a, b in zip(values, values[1:])):
        raise ValueError(f"{label}: invalid offsets")
    return values


def bpe(data, label):
    if len(data) < 8:
        raise ValueError(f"{label}: short BPE header")
    size, packed = struct.unpack_from(">II", data)
    if size > 64 * 1024 * 1024 or packed > len(data) - 8:
        raise ValueError(f"{label}: invalid BPE size")
    pos, end, out = 8, 8 + packed, bytearray()
    while pos < end:
        left, right = list(range(256)), [0] * 256
        symbol = 0
        while symbol < 256:
            if pos >= end:
                raise ValueError(f"{label}: truncated symbol table")
            count = data[pos]
            pos += 1
            if count > 127:
                symbol += count - 127
                count = 0
            if symbol == 256:
                break
            for _ in range(count + 1):
                if pos >= end or symbol >= 256:
                    raise ValueError(f"{label}: invalid symbol table")
                left[symbol] = data[pos]
                pos += 1
                if left[symbol] != symbol:
                    if pos >= end:
                        raise ValueError(f"{label}: truncated symbol pair")
                    right[symbol] = data[pos]
                    pos += 1
                symbol += 1
        if pos + 2 > end:
            raise ValueError(f"{label}: truncated block size")
        block = struct.unpack_from(">H", data, pos)[0]
        pos += 2
        if pos + block > end:
            raise ValueError(f"{label}: truncated block")
        for value in data[pos:pos + block]:
            stack = [value]
            while stack:
                value = stack.pop()
                if left[value] == value:
                    out.append(value)
                else:
                    stack.extend((right[value], left[value]))
                if len(stack) > 256 or len(out) > size:
                    raise ValueError(f"{label}: invalid expansion")
        pos += block
    if len(out) != size:
        raise ValueError(f"{label}: size mismatch")
    return bytes(out)


def png(path, width, height, pixels):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def textures(data):
    """GX I4, C4 and C8 sheets with the Wii pack's split RGBA CLUT."""
    if len(data) < 8:
        return []
    count, words = struct.unpack_from(">II", data)
    start = words * 4
    if not 0 < count <= 128 or start < 8 or start + count * 96 > len(data):
        return []
    result = []
    for index in range(count):
        entry = start + index * 96
        pix_words, pal_words, pix_size, pal_size = struct.unpack_from(">4I", data, entry)
        log_h, log_w, pal_fmt, fmt = data[entry + 40:entry + 44]
        if fmt not in (0, 8, 9) or log_w > 10 or log_h > 10 or (fmt != 0 and pal_fmt != 0):
            continue
        width, height = 1 << log_w, 1 << log_h
        pix_at, pal_at = pix_words * 4, pal_words * 4
        colors = 256 if fmt == 9 else 16 if fmt == 8 else 0
        if pix_at + pix_size > len(data) or pal_at + pal_size > len(data) or (colors and pal_size < colors * 4):
            continue
        tw, th = (8, 4) if fmt == 9 else (8, 8)
        if pix_size < ((width + tw - 1) // tw) * ((height + th - 1) // th) * 32:
            continue
        raw = data[pix_at:pix_at + pix_size]
        pal_raw = data[pal_at:pal_at + pal_size]
        # The CLUT is two planes: G,R pairs followed by A,B pairs. A is in
        # the game's 0..128 alpha range. Reading adjacent bytes as IA8 was
        # the cause of the earlier grayscale/incorrect extraction.
        palette = [(pal_raw[i * 2 + 1], pal_raw[i * 2], pal_raw[colors * 2 + i * 2 + 1],
                    min(255, pal_raw[colors * 2 + i * 2] * 2)) for i in range(colors)]
        rgba = bytearray(width * height * 4)
        tile_pos = 0
        for by in range(0, height, th):
            for bx in range(0, width, tw):
                tile = raw[tile_pos:tile_pos + 32]
                tile_pos += 32
                for y in range(th):
                    for x in range(tw):
                        p = y * tw + x
                        value = tile[p] if fmt == 9 else (tile[p // 2] >> 4 if p % 2 == 0 else tile[p // 2] & 15)
                        color = (value * 17,) * 3 + (255,) if fmt == 0 else palette[value] if value < len(palette) else (0, 0, 0, 0)
                        dx, dy = bx + x, by + y
                        if dx < width and dy < height:
                            rgba[(dy * width + dx) * 4:(dy * width + dx + 1) * 4] = bytes(color)
        result.append((index, width, height, rgba))
    return result


def fod_records(data, code):
    """Records of one big-endian Wii FOD tag (the PS2 player reads LIT)."""
    if data[:8] != b"FOD\x11BIG\0":
        raise ValueError("Wii movie is not a BIG FOD")
    pos = 8
    while pos + 8 <= len(data):
        tag, count, size = data[pos], struct.unpack_from(">H", data, pos + 2)[0], struct.unpack_from(">I", data, pos + 4)[0]
        end = pos + 8 + size
        if end > len(data):
            raise ValueError("Wii movie has a truncated tag")
        if tag == code:
            records = []
            at = pos + 8
            for _ in range(count):
                if at + 8 > end:
                    raise ValueError("Wii movie has a truncated record")
                length = struct.unpack_from(">I", data, at + 4)[0]
                if at + 8 + length > end:
                    raise ValueError("Wii movie has an invalid record size")
                records.append((at, at + 8 + length))
                at += 8 + length
            return records
        if tag == 0:
            break
        pos = end
    raise ValueError(f"Wii movie has no tag {code}")


def fod_frames(data, record):
    pos, end = record
    pos += 8
    frames = []
    while pos < end:
        name_end = data.find(b"\0", pos, end)
        if name_end < 0 or name_end + 3 > end:
            raise ValueError("Wii movie has a truncated frame")
        size = struct.unpack_from(">H", data, name_end + 1)[0]
        start = name_end + 3
        if size == 0:
            break
        if start + size > end:
            raise ValueError("Wii movie has an invalid frame size")
        frames.append((data[pos:name_end].decode("ascii", "replace"), start, start + size))
        pos = start + size
    return frames


def fod_bits(data, pos, bit, width, signed=False):
    value = 0
    for offset in range(width):
        at = bit + offset
        value = (value << 1) | ((data[pos + at // 8] >> (7 - at % 8)) & 1)
    if signed and width and value & (1 << (width - 1)):
        value -= 1 << width
    return value, bit + width


def fod_matrix(data, pos):
    flags = data[pos]
    pos += 1
    bit = 0
    result = [1.0, 1.0, 0.0, 0.0]  # scale x/y, translate x/y
    if flags & 0x11:
        width, bit = fod_bits(data, pos, bit, 5)
        if flags & 1:
            value, bit = fod_bits(data, pos, bit, width, True)
            result[0] = value / 65536.0
        if flags & 0x10:
            value, bit = fod_bits(data, pos, bit, width, True)
            result[1] = value / 65536.0
    if flags & 0x0A:
        width, bit = fod_bits(data, pos, bit, 5)
        for flag in (2, 8):
            if flags & flag:
                _, bit = fod_bits(data, pos, bit, width, True)
    if flags & 0x24:
        width, bit = fod_bits(data, pos, bit, 5)
        if flags & 4:
            value, bit = fod_bits(data, pos, bit, width, True)
            result[2] = value * 0.05
        if flags & 0x20:
            value, bit = fod_bits(data, pos, bit, width, True)
            result[3] = value * 0.05
    return result, pos + (bit + 7) // 8


def fod_cxform(data, pos):
    flags = data[pos]
    pos += 1
    bit = 0
    alpha = 1.0
    if flags:
        width, bit = fod_bits(data, pos, bit, 4)
        for flag in range(8):
            if flags & (1 << flag):
                value, bit = fod_bits(data, pos, bit, width, True)
                if flag == 3:
                    alpha = value / 256.0
    return alpha, pos + (bit + 7) // 8


def fod_frame_place(data, frame, depth):
    """Matrix and alpha updates of a depth in one FOD frame."""
    pos, end = frame[1:]
    found_matrix = found_alpha = None
    while pos < end:
        code = struct.unpack_from(">H", data, pos)[0]
        pos += 2
        if code == 1:
            break
        if code == 4:
            place_depth = struct.unpack_from(">H", data, pos)[0]
            kind = data[pos + 2]
            pos += 5
            matrix, pos = fod_matrix(data, pos)
            alpha = None
            if kind == 6:
                alpha, pos = fod_cxform(data, pos)
            if place_depth == depth:
                found_matrix, found_alpha = matrix, alpha
        elif code == 26:
            flags = data[pos]
            place_depth = struct.unpack_from(">H", data, pos + 1)[0]
            pos += 3
            if flags & 2:
                pos += 3
            matrix = alpha = None
            if flags & 4:
                matrix, pos = fod_matrix(data, pos)
            if flags & 8:
                alpha, pos = fod_cxform(data, pos)
            if flags & 0x20:
                pos = data.index(0, pos, end) + 1
            if place_depth == depth:
                if matrix is not None:
                    found_matrix = matrix
                if alpha is not None:
                    found_alpha = alpha
        elif code == 5:
            pos += 5
        elif code == 28:
            pos += 2
        elif code == 12:
            while True:
                action = data[pos]
                pos += 1
                if action == 0:
                    break
                if action == 0x81:
                    pos += 2
                elif action == 0x83:
                    pos = data.index(0, pos, end) + 1
                    pos = data.index(0, pos, end) + 1
                elif action == 0x8A:
                    pos += 3
                elif action in (0x8B, 0x8C):
                    pos = data.index(0, pos, end) + 1
                elif action not in (4, 5, 6, 7):
                    raise ValueError(f"unsupported Wii movie action {action:#x}")
        else:
            raise ValueError(f"unsupported Wii movie frame tag {code}")
    return found_matrix, found_alpha


def fod_track(data, frames, depth, field, count, base=0.0):
    if len(frames) < count:
        raise ValueError("Wii movie animation is shorter than expected")
    values = []
    last = base
    for frame in frames[:count]:
        matrix, _ = fod_frame_place(data, frame, depth)
        if matrix is not None:
            last = matrix[field]
        values.append(last)
    return values


def fod_alpha_track(data, frames, depth, count):
    if len(frames) < count:
        raise ValueError("Wii movie animation is shorter than expected")
    values = []
    last = 1.0
    for frame in frames[:count]:
        _, alpha = fod_frame_place(data, frame, depth)
        if alpha is not None:
            last = alpha
        values.append(last)
    return values


def home_motion(data):
    """Eight home-screen tracks, sampled directly from DragonNe section 0."""
    root = fod_frames(data, fod_records(data, 7)[0])
    sprites = fod_records(data, 6)
    pan = fod_frames(data, sprites[6])
    radar = fod_frames(data, sprites[11])
    giru = fod_frames(data, sprites[3])
    sparkles = fod_frames(data, sprites[1])
    if root[1][0] != "fl_btn_in_1" or pan[0][0] != "fl_loop_start" or radar[0][0] != "fl_on_loop":
        raise ValueError("Wii home animation labels do not match the expected movie")
    tracks = [fod_track(data, root, 20, 2, 18),       # bars: x = 531 .. 171
              fod_track(data, root, 10, 2, 18),       # Pan entrance: x = -272 .. -12
              fod_track(data, pan, 1, 3, 500, 256),  # Pan's vertical idle motion
              fod_track(data, radar, 1, 0, 50, 1),   # radar pulse scale
              fod_track(data, giru, 1, 2, 30),       # Giru entrance x
              fod_track(data, giru, 1, 3, 30),       # Giru entrance y
              fod_alpha_track(data, sparkles, 1, 60), # lake sparkles, first layer
              fod_alpha_track(data, sparkles, 2, 60)] # lake sparkles, second layer
    return b"WMO2" + struct.pack("<8H", *(len(track) for track in tracks)) + b"".join(
        struct.pack(f"<{len(track)}f", *track) for track in tracks)


def home_dialog(text_data, lip_data, voice_afs, out):
    """Selected pack's subtitles and both original voice/lip sets for the 44 lines."""
    text_offsets = table(text_data, "online dialog text")
    lip_offsets = table(lip_data, "online lip tracks")
    if len(text_offsets) != 45 or len(lip_offsets) != 89:
        raise ValueError("unexpected number of Wii home dialog lines or lip tracks")
    voice_dir = out / "voices"
    if voice_afs is not None:
        for language in ("ja", "en"):
            folder = voice_dir / language
            folder.mkdir(parents=True, exist_ok=True)
            for old in folder.glob("[0-9][0-9][0-9].adx"):
                old.unlink()
    binary = bytearray(b"WDL2" + struct.pack("<HH", 44, 2))
    with voice_afs.open("rb") if voice_afs is not None else nullcontext(None) as archive:
        if archive is not None:
            header = archive.read(8)
            if header[:4] != b"AFS\0" or struct.unpack_from("<I", header, 4)[0] < 32203:
                raise ValueError("Wii voice archive is not the expected AFS")
        for line in range(44):
            raw_text = text_data[text_offsets[line]:text_offsets[line + 1]]
            if raw_text[:2] != b"\xff\xfe":
                raise ValueError(f"online dialog line {line} lacks UTF-16LE marker")
            phrase = raw_text[2:].decode("utf-16le").split("\0", 1)[0].encode("utf-8")
            lip_sets = []
            for language, track, voice_start in (("ja", 0, 30787), ("en", 1, 32159)):
                raw_lips = lip_data[lip_offsets[line * 2 + track]:lip_offsets[line * 2 + track + 1]]
                if raw_lips[:4] != b"LIPS" or struct.unpack_from("<I", raw_lips, 4)[0] != 2:
                    raise ValueError(f"online {language} lip track {line} is invalid")
                count = struct.unpack_from("<I", raw_lips, 8)[0]
                if count < 2 or 20 + count * 4 > len(raw_lips):
                    raise ValueError(f"online {language} lip track {line} is truncated")
                events = [event for event in struct.iter_unpack("<HH", raw_lips[20:20 + count * 4])
                          if event[0] or event[1]]
                if not events or any(state not in (0, 1) for _, state in events) or \
                        any(a[0] > b[0] for a, b in zip(events, events[1:])):
                    raise ValueError(f"online {language} lip track {line} has invalid events")
                if archive is not None:
                    archive.seek(8 + (voice_start + line) * 8)
                    offset, size = struct.unpack("<II", archive.read(8))
                    if size < 0x40 or size > 2 * 1024 * 1024:
                        raise ValueError(f"online {language} voice {line} has invalid size")
                    archive.seek(offset)
                    voice = archive.read(size)
                    if len(voice) != size or voice[:8] != b"\x80\0\x00\x24\x03\x12\x04\x01":
                        raise ValueError(f"online {language} voice {line} is not supported ADX")
                    rate, samples = struct.unpack_from(">II", voice, 8)
                    # The Japanese streams retain more silence after the last
                    # lip event; the PAL English streams end much closer to it.
                    drift = samples * 60 / rate - events[-1][0] if rate else 999
                    if drift < -2 or drift > (45 if language == "ja" else 12):
                        raise ValueError(f"online {language} voice {line} does not match its lip track")
                    (voice_dir / language / f"{line:03}.adx").write_bytes(voice)
                lip_sets.append(events)
            binary += struct.pack("<6H", len(phrase), len(lip_sets[0]), len(lip_sets[1]),
                                  lip_sets[0][-1][0], lip_sets[1][-1][0], 0)
            binary += phrase
            for events in lip_sets:
                binary += b"".join(struct.pack("<HH", frame, state) for frame, state in events)
    (out / "home-dialog.bin").write_bytes(binary)


def read_online(afs, requested):
    with afs.open("rb") as stream:
        header = stream.read(8)
        if len(header) != 8 or header[:4] != b"AFS\0":
            raise ValueError("input is not an AFS archive")
        count = struct.unpack_from("<I", header, 4)[0]
        stream.seek(8 + count * 8)
        name_offset, name_size = struct.unpack("<II", stream.read(8))
        if name_size < count * 48:
            raise ValueError("AFS filename table is missing")
        stream.seek(name_offset)
        names = [stream.read(48)[:32].split(b"\0")[0].decode("ascii", "replace") for _ in range(count)]
        matches = [i for i, name in enumerate(names) if name.startswith("DragonNe")]
        if requested is None:
            if not matches:
                raise ValueError("no DragonNe entry in archive")
            index = matches[0]
        else:
            index = requested
            if index not in matches:
                raise ValueError(f"entry {index} is {names[index] if index < count else 'out of range'}, not DragonNe")
        stream.seek(8 + index * 8)
        offset, size = struct.unpack("<II", stream.read(8))
        if size > 64 * 1024 * 1024:
            raise ValueError("online pack is too large")
        stream.seek(offset)
        pack = stream.read(size)
        if len(pack) != size:
            raise ValueError("online pack is truncated")
        return index, pack, matches


def online_locales(afs, matches):
    if afs.name.lower() == "wzs3eu1.afs":
        if len(matches) != 5:
            raise ValueError("PAL DragonNe archive does not have five language entries")
        return ("en", "es", "de", "fr", "it")
    return ("en",)


def extract_battle_art(section, art_root, locale):
    """Save the Wii battle page's language-dependent lettering sheets."""
    positions = table(section, f"{locale} section 1")
    wanted = {3: {29, 30, 41, 42}, 4: {1, 2, 3}}
    for resource_index, indices in wanted.items():
        resource = section[positions[resource_index]:positions[resource_index + 1]]
        folder = art_root / locale / "section-01" / f"resource-{resource_index:03d}-textures"
        folder.mkdir(parents=True, exist_ok=True)
        found = set()
        for texture_index, width, height, pixels in textures(resource):
            if texture_index in indices:
                png(folder / f"texture-{texture_index:03d}.png", width, height, pixels)
                found.add(texture_index)
        if found != indices:
            raise ValueError(f"{locale} battle art resource {resource_index} lacks textures {sorted(indices - found)}")


def extract_menu_art(afs, out, matches):
    """Decode language-specific Wii headings, labels, battle lettering, and name screen."""
    art_root = out / "languages"
    if art_root.exists():
        shutil.rmtree(art_root)
    for locale, index in zip(online_locales(afs, matches), matches):
        _, pack, _ = read_online(afs, index)
        boundaries = table(pack, f"{locale} online pack")
        section = bpe(pack[boundaries[0]:boundaries[1]], f"{locale} section 0")
        positions = table(section, f"{locale} section 0")
        for resource_index in (4, 5):
            resource = section[positions[resource_index]:positions[resource_index + 1]]
            folder = art_root / locale / f"resource-{resource_index:03d}-textures"
            folder.mkdir(parents=True, exist_ok=True)
            for texture_index, width, height, pixels in textures(resource):
                png(folder / f"texture-{texture_index:03d}.png", width, height, pixels)
        resource = section[positions[13]:positions[14]]
        parts = table(resource, f"{locale} nickname pack")
        folder = art_root / locale / "resource-013-part-02-textures"
        folder.mkdir(parents=True, exist_ok=True)
        for texture_index, width, height, pixels in textures(resource[parts[2]:parts[3]]):
            png(folder / f"texture-{texture_index:03d}.png", width, height, pixels)
        battle = bpe(pack[boundaries[1]:boundaries[2]], f"{locale} section 1")
        extract_battle_art(battle, art_root, locale)


def menu_text(text_data, out, locale):
    lines = wii_text_lines(text_data, range(66), locale)
    binary = bytearray(b"WMT1" + struct.pack("<HH", 66, 0))
    for line in lines:
        phrase = line.replace("\n", " ").encode("utf-8")
        if not phrase or len(phrase) >= 512:
            raise ValueError(f"{locale} Dragon Net menu text has an invalid line length")
        binary += struct.pack("<H", len(phrase)) + phrase
    (out / f"menu-text-{locale}.bin").write_bytes(binary)
    return lines


def wii_text_lines(text_data, indexes, locale):
    offsets = table(text_data, f"{locale} Wii text")
    if len(offsets) <= max(indexes) + 1:
        raise ValueError(f"{locale} Wii text bank is missing expected lines")
    lines = []
    for line in indexes:
        raw = text_data[offsets[line]:offsets[line + 1]]
        if raw[:2] != b"\xff\xfe":
            raise ValueError(f"{locale} Wii text line {line} lacks UTF-16LE marker")
        lines.append(raw[2:].decode("utf-16le").split("\0", 1)[0])
    return lines


def wii_ui_catalog(out, locale, original_dialog, original_menu, dialog, menu_lines):
    """Save stable Wii IDs and add them to an already generated UI catalog without losing edits."""
    rows = {}
    for group, originals, localized in (("dialog", original_dialog, dialog),
                                        ("menu-text", original_menu, menu_lines)):
        for index, (original, text) in enumerate(zip(originals, localized)):
            rows[f"wii-dragon-net/{group}/{index}"] = {"original": original, "text": text}
    # The C UI catalog generator also includes this fragment when making a new catalog.
    fragment = "".join(",\n    " + json.dumps(key, ensure_ascii=False) + ": " +
                       json.dumps(value, ensure_ascii=False) for key, value in rows.items())
    (out / f"ui-fragment-{locale}.json").write_text(fragment, encoding="utf-8")
    path = out.parent / "game-text/translations/ui" / f"{locale}.json"
    if not path.is_file():
        return
    catalog = json.loads(path.read_text(encoding="utf-8"))
    if catalog.get("locale") != locale or not isinstance(catalog.get("labels"), dict):
        raise ValueError(f"{path}: invalid UI catalog for {locale}")
    for key in list(catalog["labels"]):
        if key.startswith("wii-dragon-net/battle-help/"):
            del catalog["labels"][key]
    for key, row in rows.items():
        current = catalog["labels"].get(key)
        # An edited text field belongs to the user. Refresh only untouched Wii entries.
        if current is None or current.get("text") == current.get("original"):
            catalog["labels"][key] = row
    catalog["labels_count"] = len(catalog["labels"])
    temp = path.with_suffix(".json.part")
    temp.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


def extract_subtitles(afs, out, matches):
    """Write each PAL DragonNe dialogue and menu-text bank for the menu language selector."""
    locales = online_locales(afs, matches)
    for old in out.glob("home-subtitles-*.bin"):
        old.unlink()
    for old in out.glob("battle-help-*.bin"):
        old.unlink()
    for old in out.glob("menu-text-*.bin"):
        old.unlink()
    for old in out.glob("ui-fragment-*.json"):
        old.unlink()
    original_dialog = wii_text_lines((out / "section-00/resource-007.bin").read_bytes(), range(44), "en")
    original_menu = wii_text_lines((out / "section-00/resource-011.bin").read_bytes(), range(66), "en")
    for locale, index in zip(locales, matches):
        _, pack, _ = read_online(afs, index)
        boundaries = table(pack, f"{locale} online pack")
        section = bpe(pack[boundaries[0]:boundaries[1]], f"{locale} section 0")
        positions = table(section, f"{locale} section 0")
        text_data = section[positions[7]:positions[8]]
        offsets = table(text_data, f"{locale} dialogue")
        if len(offsets) != 45:
            raise ValueError(f"{locale} Dragon Net dialogue has {len(offsets) - 1} lines, expected 44")
        binary = bytearray(b"WST1" + struct.pack("<HH", 44, 0))
        for line in range(44):
            raw = text_data[offsets[line]:offsets[line + 1]]
            if raw[:2] != b"\xff\xfe":
                raise ValueError(f"{locale} Dragon Net line {line} lacks UTF-16LE marker")
            phrase = raw[2:].decode("utf-16le").split("\0", 1)[0].encode("utf-8")
            if not phrase or len(phrase) >= 256:
                raise ValueError(f"{locale} Dragon Net line {line} has invalid length")
            binary += struct.pack("<H", len(phrase)) + phrase
        (out / f"home-subtitles-{locale}.bin").write_bytes(binary)
        help_data = section[positions[11]:positions[12]]
        menu_lines = menu_text(help_data, out, locale)
        wii_ui_catalog(out, locale, original_dialog, original_menu,
                       wii_text_lines(text_data, range(44), locale),
                       menu_lines)
    # Other installed UI languages can still edit the Wii IDs; their source
    # wording falls back to English until that language has a Wii text bank.
    ui_dir = out.parent / "game-text/translations/ui"
    for path in ui_dir.glob("*.json"):
        if path.stem not in locales:
            wii_ui_catalog(out, path.stem, original_dialog, original_menu,
                           original_dialog, original_menu)


def extract(source, out, dolphin_tool, requested, voice_afs=None, require_eu=False):
    if source.suffix.lower() == ".afs":
        if require_eu and source.name.lower() != "wzs3eu1.afs":
            raise ValueError("European Wii extraction needs wzs3eu1.afs")
        afs = source
        temporary = None
        if voice_afs is None:
            candidate = source.with_name(source.name.replace("1.afs", "2.afs"))
            voice_afs = candidate if candidate.is_file() else None
    else:
        if not dolphin_tool:
            raise ValueError("Wii ISO or RVZ input needs dolphin-tool; use --dolphin-tool PATH")
        temporary = tempfile.TemporaryDirectory(prefix="bt3-wii-online-")
        root = Path(temporary.name)
        listing = subprocess.run([dolphin_tool, "extract", "-i", str(source.resolve()), "-g", "-l"],
                                 check=True, capture_output=True, text=True).stdout
        archives = [name for name in ("wzs3eu1.afs", "wzs3us1.afs") if name in listing]
        if len(archives) != 1:
            raise ValueError("could not identify the PAL or USA Wii archive")
        if require_eu and archives[0] != "wzs3eu1.afs":
            raise ValueError("European Wii extraction needs a PAL Wii disc (wzs3eu1.afs)")
        subprocess.run([dolphin_tool, "extract", "-i", str(source.resolve()), "-o", str(root), "-g", "-s", archives[0], "-q"], check=True)
        found = list(root.rglob(archives[0]))
        if len(found) != 1:
            raise ValueError(f"could not extract {archives[0]}")
        afs = found[0]
        if voice_afs is None:
            # This archive is large; callers with an already extracted copy
            # can pass --voice-afs to reuse it.
            voice_afs = Path("wii/raw/DATA/files") / archives[0].replace("1.afs", "2.afs")
            if not voice_afs.is_file():
                voice_name = archives[0].replace("1.afs", "2.afs")
                subprocess.run([dolphin_tool, "extract", "-i", str(source.resolve()), "-o", str(root), "-g", "-s", voice_name, "-q"], check=True)
                voice_afs = next(root.rglob(voice_name))
    index, pack, matches = read_online(afs, requested)
    out.mkdir(parents=True, exist_ok=True)
    # Replace only this tool's generated files. A second extraction must not
    # leave images or whole sections from a previously selected AFS entry.
    for old in out.glob("entry-*.bin"):
        old.unlink()
    (out / "home-motion.bin").unlink(missing_ok=True)
    (out / "home-dialog.bin").unlink(missing_ok=True)
    for old in out.glob("section-*"):
        if old.is_dir() and old.name[8:].isdigit():
            shutil.rmtree(old)
    (out / f"entry-{index:05d}.bin").write_bytes(pack)
    manifest = {"archive": afs.name, "entry": index, "dragon_net_entries": matches, "sections": []}
    boundaries = table(pack, "online pack")
    for si, (a, b) in enumerate(zip(boundaries, boundaries[1:])):
        section = bpe(pack[a:b], f"section {si}")
        folder = out / f"section-{si:02d}"
        folder.mkdir(exist_ok=True)
        (folder / "decoded.bin").write_bytes(section)
        positions = table(section, f"section {si}")
        items = []
        for ri, (x, y) in enumerate(zip(positions, positions[1:])):
            resource = section[x:y]
            (folder / f"resource-{ri:03d}.bin").write_bytes(resource)
            if si == 0 and ri == 0:
                (out / "home-motion.bin").write_bytes(home_motion(resource))
            pics = []
            for ti, w, h, pixels in textures(resource):
                image_dir = folder / f"resource-{ri:03d}-textures"
                image_dir.mkdir(exist_ok=True)
                filename = f"texture-{ti:03d}.png"
                png(image_dir / filename, w, h, pixels)
                pics.append({"file": f"{image_dir.name}/{filename}", "width": w, "height": h})
            item = {"index": ri, "offset": x, "size": y - x, "textures": pics}
            # Some resources are packs of smaller texture sets: the small dialog Giru is in
            # section 0 / resource 10 / part 0, and the name keyboard is in resource 13.
            try:
                parts = table(resource, f"section {si} resource {ri} nested pack")
            except ValueError:
                parts = ()
            nested_pics = []
            for part, (a, b) in enumerate(zip(parts, parts[1:])):
                for ti, w, h, pixels in textures(resource[a:b]):
                    image_dir = folder / f"resource-{ri:03d}-part-{part:02d}-textures"
                    image_dir.mkdir(exist_ok=True)
                    filename = f"texture-{ti:03d}.png"
                    png(image_dir / filename, w, h, pixels)
                    nested_pics.append({"file": f"{image_dir.name}/{filename}", "part": part,
                                        "width": w, "height": h})
            if nested_pics:
                item["nested_textures"] = nested_pics
            items.append(item)
        manifest["sections"].append({"index": si, "decoded_size": len(section), "resources": items})
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    home_dialog((out / "section-00/resource-007.bin").read_bytes(),
                (out / "section-00/resource-006.bin").read_bytes(),
                voice_afs if voice_afs is not None and voice_afs.is_file() else None, out)
    extract_subtitles(afs, out, matches)
    extract_menu_art(afs, out, matches)
    if voice_afs is None or not voice_afs.is_file():
        print("Wii voice archive unavailable: extracted text and lip tracks; pass --voice-afs for the voices")
    cards = []
    for section in manifest["sections"]:
        for resource in section["resources"]:
            for texture in resource["textures"]:
                path = f"section-{section['index']:02d}/{texture['file']}"
                label = f"section {section['index']} · resource {resource['index']} · {texture['width']}×{texture['height']}"
                cards.append(f'<figure><a href="{html.escape(path)}"><img loading="lazy" src="{html.escape(path)}" alt="{html.escape(label)}"></a><figcaption>{html.escape(label)}</figcaption></figure>')
            for texture in resource.get("nested_textures", []):
                path = f"section-{section['index']:02d}/{texture['file']}"
                label = f"section {section['index']} · resource {resource['index']} · part {texture['part']} · {texture['width']}×{texture['height']}"
                cards.append(f'<figure><a href="{html.escape(path)}"><img loading="lazy" src="{html.escape(path)}" alt="{html.escape(label)}"></a><figcaption>{html.escape(label)}</figcaption></figure>')
    (out / "index.html").write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8"><title>Dragon Net Battle assets</title>'
        '<style>body{font:14px sans-serif;background:#222;color:#eee;margin:24px}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:12px}'
        'figure{margin:0;background:#333;padding:8px}img{width:100%;height:150px;object-fit:contain;background:#777}figcaption{padding:6px}</style>'
        f'<h1>Dragon Net Battle · {html.escape(afs.name)} · entry {index}</h1><p>GX texture sheets decoded with their original color palettes.</p><main>'
        + ''.join(cards) + '</main></html>\n', encoding="utf-8")
    print(f"{afs.name}: Dragon Net Battle entry {index}, {len(manifest['sections'])} sections -> {out}")
    if temporary:
        temporary.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Wii .iso, .rvz, or extracted wzs3*1.afs")
    parser.add_argument("--out", type=Path, default=Path("gamedata/dragon-net-battle"))
    parser.add_argument("--entry", type=int, help="choose a specific DragonNe language entry")
    parser.add_argument("--voice-afs", type=Path, help="already extracted wzs3eu2.afs for Japanese and English dialogue voices")
    parser.add_argument("--dolphin-tool", default=shutil.which("dolphin-tool"))
    parser.add_argument("--require-eu", action="store_true", help="require the European Wii archive with all five subtitle languages")
    args = parser.parse_args()
    if not args.source.is_file():
        parser.error(f"file not found: {args.source}")
    try:
        extract(args.source, args.out, args.dolphin_tool, args.entry, args.voice_afs, args.require_eu)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
