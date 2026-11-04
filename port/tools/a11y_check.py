#!/usr/bin/env python3
"""Checks of the accessibility (V6.1 on), one line of plain text each, in Spanish: "correcto: ..." or "falla: ...,
esperaba ...", so that a screen reader reads them one by one. Exit status 1 when one fails.

    python3 port/tools/a11y_check.py tones <tones.txt>          the sounds (port/tests/a11y_tones prints the file)
    python3 port/tools/a11y_check.py log <a11y_log.txt> [what]  a game's log (BT3_A11Y_LOG): what = profile,
                                                                profile-off, where, winner, names, all (default)
    python3 port/tools/a11y_check.py music <run folder> [menu track] [percent]
                                                                a run with only the music audible, recorded with
                                                                SDL's disk driver: in a fight the music is
                                                                a11y_fight_music under the dump (BT3_SND_DUMP)
    python3 port/tools/a11y_check.py lang [a11y_es.txt]         the Spanish file: every line's key is its note's
                                                                English, and every label of the code has a line

Only the standard library; run with python3 -I.
"""
import math
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
results = []


def ok(text):
    results.append(True)
    print("correcto: " + text)


def fail(text, expected):
    results.append(False)
    print("falla: %s, esperaba %s" % (text, expected))


def check(cond, text, expected):
    (ok if cond else lambda t: fail(t, expected))(text)


def num(x):
    return ("%.1f" % x).replace(".", ",")


# ---- the sounds ----

# The kinds that mean different things: none of them may have a note of another (a quarter tone apart at least).
# Each kind's own variants (yours and the rival's, one tick or two) share their notes on purpose.
FAMILY = {
    "tick": "distancia", "tick_close": "distancia", "wall": "pared", "ceiling": "techo", "floor": "suelo",
    "ring": "ring", "health": "vida", "health_rival": "vida", "ki": "ki", "ki_rival": "ki", "max_power": "maximo poder",
    "where": "donde esta el rival", "transform": "transformacion", "transform_rival": "transformacion",
    "ultimate": "ataque definitivo", "ultimate_rival": "ataque definitivo",
}
BANDS = {"wall": (250, 600), "ceiling": (4000, 8000), "floor": (35, 70)}
UNUSED = {"floor"}  # (made, never played yet)


def tones(path):
    sounds, places, gains = {}, [], {}
    for line in open(path, encoding="utf-8"):
        w = line.split()
        if not w:
            continue
        if w[0] == "sound":
            d = dict(zip(w[2::2], w[3::2]))
            d["notes"] = [] if d["notes"] == "-" else [float(x) for x in d["notes"].split(",")]
            sounds[w[1]] = d
        elif w[0] == "place":
            places.append(dict(zip(w[::2], w[1::2])))
        elif w[0] == "gain":
            gains[int(w[1])] = float(w[2])
    check(len(sounds) == len(FAMILY), "el programa de prueba midió %d sonidos" % len(sounds), "%d" % len(FAMILY))

    # each one's level and its central frequency, as information
    for name, d in sounds.items():
        print("dato: %s: %s ms, pico %s dBFS, eficaz %s dBFS, frecuencia central %s Hz, notas %s" % (
            name, num(float(d["ms"])), num(float(d["peak"])), num(float(d["rms"])), num(float(d["main"])),
            ", ".join("%d" % round(n) for n in d["notes"]) or "ninguna (ruido)"))

    t = sounds.get("tick")
    if t:
        check(float(t["ms"]) >= 55, "el tic dura %s ms" % num(float(t["ms"])), "55 ms o más")
        check(float(t["main"]) > 1000, "el tic está en %s Hz" % num(float(t["main"])), "más de 1000 Hz")
        check(float(t["attack_ms"]) <= 2, "el tic llega a su pico en %s ms" % num(float(t["attack_ms"])), "2 ms o menos (un transitorio)")
    for name, (lo, hi) in BANDS.items():
        d = sounds.get(name)
        if d is None:
            continue
        share = float(d["band"])
        if name in UNUSED:
            print("dato: %s todavía no suena en la pelea; %d %% de su energía entre %d y %d Hz" % (name, round(share * 100), lo, hi))
            continue
        check(share >= 0.75, "%s: %d %% de su energía entre %d y %d Hz" % (name, round(share * 100), lo, hi), "75 % o más")
        inside = [(o, n) for o, od in sounds.items() if FAMILY[o] != FAMILY[name] for n in od["notes"] if lo <= n <= hi]
        check(not inside, "%s: ninguna nota de otro sonido dentro de su banda" % name,
              "ninguna, y está " + ", ".join("%s (%d Hz)" % (o, n) for o, n in inside))
    clashes = []
    names = sorted(sounds)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            if FAMILY[a] == FAMILY[b]:
                continue
            for na in sounds[a]["notes"]:
                for nb in sounds[b]["notes"]:
                    if abs(math.log2(na / nb)) * 12 < 0.5:
                        clashes.append("%s y %s (%d y %d Hz)" % (a, b, na, nb))
    check(not clashes, "ningún par de sonidos distintos comparte una nota", "ninguno, y lo comparten " + "; ".join(clashes))
    for a, b in (("health", "health_rival"), ("ki", "ki_rival"), ("transform", "transform_rival"), ("ultimate", "ultimate_rival")):
        if a in sounds and b in sounds:
            ca, cb = float(sounds[a]["centroid"]), float(sounds[b]["centroid"])
            check(cb > ca * 1.15, "el timbre del rival es otro en %s: brillo %d Hz contra %d Hz el tuyo" % (FAMILY[a], cb, ca),
                  "el del rival un 15 % más brillante o más")
    peaks = [float(d["peak"]) for d in sounds.values()]
    check(max(peaks) <= -0.5, "ningún sonido satura: el pico más alto es %s dBFS a todo volumen" % num(max(peaks)), "-0,5 dBFS o menos")

    # the "where" ping from a direction
    by = {(int(p["place"]), int(p["mono"])): p for p in places}
    def lr(az, mono=0):
        p = by[(az, mono)]
        return float(p["right"]) - float(p["left"])
    if (9000, 0) in by and (-9000, 0) in by and (0, 0) in by:
        check(lr(9000) >= 15, "a la derecha suena %s dB más fuerte en el oído derecho" % num(lr(9000)), "15 dB o más")
        check(lr(-9000) <= -15, "a la izquierda suena %s dB más fuerte en el oído izquierdo" % num(-lr(-9000)), "15 dB o más")
        check(abs(lr(0)) < 0.5, "delante suena igual en los dos oídos (%s dB de diferencia)" % num(abs(lr(0))), "menos de 0,5 dB")
    if (4500, 0) in by:
        check(0 < lr(4500) < lr(9000), "a 45 grados suena entre el centro y la derecha (%s dB)" % num(lr(4500)), "entre 0 y el de 90 grados")
    if (2000, 0) in by:
        # the game's own pan law (BtlCharSnd_CalcVolPan); the one before it gave 1.8 dB here, too little to tell
        check(lr(2000) >= 4, "a 20 grados ya se oye el lado: %s dB más fuerte en el oído derecho" % num(lr(2000)), "4 dB o más")
    if (18000, 0) in by and (0, 0) in by:
        front, back = float(by[(0, 0)]["centroid"]), float(by[(18000, 0)]["centroid"])
        check(back < front * 0.95, "detrás suena apagado: brillo %d Hz contra %d Hz delante" % (back, front), "detrás al menos un 5 % menos brillante")
        check(abs(lr(18000)) < 0.5, "detrás suena igual en los dos oídos", "menos de 0,5 dB de diferencia")
    if (9000, 1) in by:
        check(abs(lr(9000, 1)) < 0.5, "en mono, un sonido a la derecha suena igual en los dos oídos", "menos de 0,5 dB de diferencia")

    # the volume's scale
    if gains:
        check(gains.get(100) == 0.0, "el volumen al 100 %% es %s dB" % num(gains.get(100, -999)), "0 dB")
        check(abs(gains.get(60, 0) + 12) < 0.2, "el volumen al 60 %% (el de fábrica) es %s dB" % num(gains.get(60, 0)), "-12 dB, como antes")
        if 85 in gains and 60 in gains:
            check(gains[85] - gains[60] > 7, "del 60 %% al 85 %% se ganan %s dB" % num(gains[85] - gains[60]), "más de 7 dB (antes eran 3)")
        check(gains.get(0) is not None and gains[0] < -900, "el volumen en 0 % no suena", "silencio")
        steps = [gains[k] for k in sorted(gains) if k > 0]
        check(all(a < b for a, b in zip(steps, steps[1:])), "la escala del volumen siempre sube", "que suba")


# ---- a game's log ----

def read_log(path):
    """[(blank, kind, text)]: kind '#' a note, '+' a line said queued, '!' a line said interrupting."""
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"(-?\d+) (# |\+ )?(.*)$", line)
        if m:
            out.append((int(m.group(1)), (m.group(2) or "! ").strip(), m.group(3)))
    return out


def log_profile(log, on):
    notes = [t for b, k, t in log if k == "#" and t.startswith("profile:")]
    applied = [t for t in notes if "blind player applied" in t]
    if on:
        check(len(applied) == 1, "el perfil para jugar sin ver se aplicó %d vez/veces" % len(applied), "una sola vez")
        said = [t for b, k, t in log if k != "#" and ("Perfil para jugar sin ver" in t or "Blind player profile" in t)]
        check(len(said) == 1, "el aviso del perfil se dijo %d vez/veces" % len(said), "una sola vez")
    else:
        check(not applied, "el perfil no se aplicó (lector que no es NVDA, JAWS ni VoiceOver)", "que no se aplique")
    fights = [t for b, k, t in log if k == "#" and t.startswith("fight alerts:")]
    if not fights:
        print("dato: el registro no tiene ninguna pelea")
        return
    m = re.search(r"distance (\d+), edge (\d+), health (\d+)", fights[0])
    if m:
        d, e, h = map(int, m.groups())
        if on:
            check(d & 1 and e & 1 and h & 1, "en la primera pelea, distancia %d, borde %d y vida %d" % (d, e, h), "los tres con sonido (1 o 3)")
        else:
            check(d == 0 and e == 0 and h == 0, "en la primera pelea, distancia, borde y vida apagados", "apagados sin el perfil")


SIDES = {"right": +1, "left": -1, "ahead": 0}


def log_where(log):
    asked = [t for b, k, t in log if k == "#" and t.startswith("where asked:")]
    if not asked:
        fail("el registro no tiene ninguna pregunta de dónde está el rival", "al menos una")
        return
    bad_side = bad_behind = bad_height = 0
    for t in asked:
        m = re.search(r"az (-?\d+), el (-?\d+), d (\d+); said: (.*)$", t)
        if not m:
            continue
        az, el, said = int(m.group(1)), int(m.group(2)), m.group(4)
        behind = abs(az) > 100
        side = "right" if "right" in said else "left" if "left" in said else "ahead"
        # the side said follows the angle's sign; "behind" only beyond 100 degrees; up and down with the elevation
        want = "ahead" if abs(az) <= 15 or abs(az) >= 165 else "right" if az > 0 else "left"
        bad_side += side != want
        bad_behind += ("behind" in said) != behind
        want_h = "above" if el >= 20 else "below" if el <= -20 else ""
        got_h = "above" if "above" in said else "below" if "below" in said else ""
        bad_height += got_h != want_h
    n = len(asked)
    check(bad_side == 0, "en %d preguntas, el lado dicho coincide con el signo del ángulo" % n, "todas, y fallan %d" % bad_side)
    check(bad_behind == 0, "detrás se dice solo por encima de 100 grados", "todas bien, y fallan %d" % bad_behind)
    check(bad_height == 0, "arriba y abajo coinciden con la elevación", "todas bien, y fallan %d" % bad_height)
    tests = [re.search(r"median \|az\| (\d+) over (\d+)", t) for b, k, t in log if k == "#" and t.startswith("where: sign test")]
    tests = [(int(m.group(1)), int(m.group(2))) for m in tests if m]
    if tests:
        check(all(med < 15 for med, n in tests), "prueba de signo: con la fijación puesta, el ángulo medio al rival es de %s grados en %d tramos de pelea (%d cuadros)" % (
            " y ".join(sorted(set(str(med) for med, n in tests))), len(tests), sum(n for med, n in tests)), "menos de 15 en todas")
    else:
        fail("el registro no tiene la prueba de signo", "una por pelea con la fijación puesta")
    # the side placed (rel, as the battle camera draws the two) against the side the game draws the rival on
    screen = [re.search(r"median \|az\| (\d+) over (\d+) frames locked on \(the rival from the middle (\d+)\); apart (\d+) to the left "
                        r"and (\d+) to the right, the side as drawn in (\d+)", t) for b, k, t in log if k == "#" and t.startswith("where: screen test")]
    screen = [tuple(int(x) for x in m.groups()) for m in screen if m]
    if screen:
        for med, n, mid, left, right, same in screen:
            print("dato: prueba de pantalla: %d cuadros, el rival a %d grados del luchador y a %d del medio (medianas); "
                  "dibujado a la izquierda del luchador en %d y a la derecha en %d" % (n, med, mid, left, right))
        check(all(same == left + right for med, n, mid, left, right, same in screen),
              "prueba de pantalla: el lado del sonido es el lado donde se dibuja al rival en los %d cuadros apartados" % sum(
                  s[3] + s[4] for s in screen), "todos, y fallan %d" % sum(s[3] + s[4] - s[5] for s in screen))
    else:
        fail("el registro no tiene la prueba de pantalla", "una por pelea con la fijación puesta")


def log_winner(log):
    # each winner noted is said right after
    wins = [(i, b, t) for i, (b, k, t) in enumerate(log) if k == "#" and t.startswith("winner:")]
    for i, b, t in wins:
        said = [t2 for b2, k2, t2 in log[i + 1:i + 6] if k2 != "#" and b2 - b < 120]
        print("dato: %d %s; dicho: %s" % (b, t, said[0] if said else "nada"))
    unsaid = [w for w in wins if not any(k2 != "#" for b2, k2, t2 in log[w[0] + 1:w[0] + 6] if b2 - w[1] < 120)]
    if wins:
        check(not unsaid, "los %d ganadores anotados se dijeron" % len(wins), "todos, y faltan %d" % len(unsaid))
    overs = [i for i, (b, k, t) in enumerate(log) if k != "#" and t in ("Fight over", "Fin de la pelea")]
    if not overs:
        (print if wins else lambda t: fail(t, "al menos uno"))("dato: el registro no tiene el menú de fin de pelea" if wins else "el registro no tiene ningún final de pelea")
        return
    missing = 0
    for i in overs:
        before = [t for b, k, t in log[max(0, i - 40):i] if k == "#" and t.startswith("winner:")]
        missing += not before
    check(missing == 0, "en %d finales de pelea, el ganador se dijo antes de \"Fin de la pelea\"" % len(overs), "todos, y faltan %d" % missing)


def log_names(log):
    names = [(b, k, t) for b, k, t in log if k == "#" and t.startswith("technique name:")]
    if not names:
        print("dato: el registro no tiene nombres de técnicas en una pelea (las peleas de demostración no los muestran)")
    else:
        said = [(i, b) for i, (b, k, t) in enumerate(log) if k == "#" and t.startswith("technique name: said")]
        gaps = [b - a for (_, a), (_, b) in zip(said, said[1:])]
        dropped = len(names) - len(said)
        check(all(g >= 90 for g in gaps), "%d nombres de técnicas dichos (%d dejados por llegar antes de 1,5 s), separados al menos 1,5 s" % (
            len(said), dropped), "1,5 s o más, y el menor fue %s s" % num(min(gaps) / 60) if gaps else "")
        # a name said is queued: the line goes to the log with "+" (an interrupting one has none)
        queued = []
        for i, b in said:
            text = log[i][2].split(": ", 2)[2]
            queued += [k2 == "+" for b2, k2, t2 in log[i + 1:i + 200] if k2 != "#" and t2 == text][:1]
        check(queued and all(queued), "los %d nombres dichos van en cola, sin cortar lo que se está diciendo" % len(queued), "todos en cola")
    # every moment noted is said after it (the speaker writes a line when it says it: one dropped from the queue by
    # an interrupting line is not in the log)
    lost = []
    for i, (b, k, t) in enumerate(log):
        if k == "#" and t.startswith("moment: "):
            text = t[len("moment: "):]
            if not any(k2 != "#" and t2 == text for b2, k2, t2 in log[i + 1:i + 200] if b2 - b < 600):
                lost.append("%d %s" % (b, text))
    n = len([1 for b, k, t in log if k == "#" and t.startswith("moment: ")])
    check(n > 0 and not lost, "los %d momentos importantes anotados se dijeron todos" % n,
          "todos, y se perdieron %d: %s" % (len(lost), "; ".join(lost[:5])) if n else "al menos uno en la prueba")


def log(path, what="all"):
    lg = read_log(path)
    print("dato: %d líneas en el registro" % len(lg))
    if what in ("profile", "all"):
        log_profile(lg, True)
    if what == "profile-off":
        log_profile(lg, False)
    if what in ("where", "all"):
        log_where(lg)
    if what in ("winner", "all"):
        log_winner(lg)
    if what in ("names", "all"):
        log_names(lg)


# ---- the music in a fight ----

BLOCK = 0.5  # seconds


def envelope(data, at, frames, rate):
    """dB of 16-bit stereo `data` from frame `at`, `frames` long, per BLOCK seconds."""
    fr = int(rate * BLOCK)
    out = []
    for i in range(at, at + frames - fr + 1, fr):
        v = struct.unpack_from("<%dh" % (fr * 2), data, i * 4)
        sub = v[::8]
        out.append(10 * math.log10(sum(x * x for x in sub) / len(sub) + 1e-9))
    return out


def music(run, menu_track="64974", fight_percent=70):
    """A run of the game with sound and only the music audible (settings music=100, effects=0, voices=0,
    stage_sounds=0, a11y_cue_volume=0), BT3_PACED=1, SDL's disk driver (16-bit stereo at 48 kHz into audio.raw),
    BT3_SND_DUMP into music.raw and BT3_SND_VERBOSE's lines in out.txt: for each piece of music played, the
    recording against the dump of the same moments, less the game's own volume for it, is what the accessibility
    takes off. Out of a fight none; in a fight a11y_fight_music. The game's attract mode alternates the title's
    song (menu_track) and its fights' songs."""
    rec = open(os.path.join(run, "audio.raw"), "rb").read()
    dump = open(os.path.join(run, "music.raw"), "rb").read()
    segs = []  # [track, rate, seconds or None, game volume dB]
    for line in open(os.path.join(run, "out.txt"), encoding="utf-8", errors="replace"):
        m = re.match(r"adx: player 0: start (\S+): \d ch, (\d+) Hz, [\d.]+ s, loop \d, volume (-?\d+)", line)
        if m:
            segs.append([os.path.basename(m.group(1)).split(".")[0], int(m.group(2)), None, int(m.group(3)) / 10.0])
        m = re.match(r"adx: stop player 0 at ([\d.]+) s", line)
        if m and segs and segs[-1][2] is None:
            segs[-1][2] = float(m.group(1))
    rec_env = envelope(rec, 0, len(rec) // 4, 48000)
    # The recording's pieces of sound: runs of blocks that are not silent (between two songs the game loads, and the
    # music stops), matched in order with the dump's pieces, which follow one another with no gap.
    runs, start = [], None
    for i, e in enumerate(rec_env + [-90]):
        if e > 30 and start is None:
            start = i
        elif e <= 30 and start is not None:
            if i - start >= 16:
                runs.append(start)
            start = None
    at, want, n = 0, -20 * math.log10(100.0 / fight_percent), 0
    menu_ok = fight_ok = 0
    for track, rate, secs, vol in segs:
        frames = int(secs * rate) if secs is not None else (len(dump) - at * 4) // 4
        frames = min(frames, (len(dump) - at * 4) // 4)
        env = envelope(dump, at, frames, rate)
        at += frames
        if len(env) < 16 or n >= len(runs):
            continue
        best = None
        for l in range(max(0, runs[n] - 4), runs[n] + 5):  # (the piece's start, give or take the buffers' delay)
            d = [rec_env[l + k] - e for k, e in enumerate(env) if l + k < len(rec_env) and e > 30 and rec_env[l + k] > 30]
            if len(d) < len(env) // 2:
                continue
            mean = sum(d) / len(d)
            var = sum((x - mean) ** 2 for x in d) / len(d)
            if best is None or var < best[0]:
                best = (var, l, sorted(d)[len(d) // 2])
        n += 1
        if best is None:
            continue
        cut = best[2] - vol  # what is left once the game's own volume is out: the accessibility's part
        fight = track != menu_track
        print("dato: pista %s, %s s: la grabación va %s dB respecto del volcado; el juego la pone a %s dB; queda %s dB%s" % (
            track, num(len(env) * BLOCK), num(best[2]), num(vol), num(cut), " (pelea)" if fight else ""))
        if fight:
            fight_ok += abs(cut - want) <= 0.7
            check(abs(cut - want) <= 0.7, "en la pelea (pista %s) la música queda %s dB por debajo" % (track, num(-cut)),
                  "unos %s dB (a11y_fight_music=%d)" % (num(-want), fight_percent))
        else:
            menu_ok += abs(cut) <= 0.7
            check(abs(cut) <= 0.7, "fuera de la pelea (pista %s) la música queda igual (%s dB)" % (track, num(cut)), "0 dB")
    check(menu_ok > 0 and fight_ok > 0, "la grabación tiene música de menú y de pelea comparadas", "al menos una de cada")


# ---- the Spanish file ----

def fnv1a(s):
    h = 0xCBF29CE484222325
    for c in s.encode("utf-8"):
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def c_strings(text, tables=False):
    """The labels passed to the translation calls of a C file, with their literals joined."""
    lit = r'((?:"(?:[^"\\\n]|\\.)*"\s*)+)'
    found = []
    for m in re.finditer(r'\b(?:tr|PortA11y_Tr|shown|label_id|lang_line|tab_slider|tab_tip|Voice_Tr)\(\s*' + lit, text):
        found.append(m.group(1))
    for m in re.finditer(r'\b(?:trc|PortA11y_TrSense)\(\s*(?:NULL|"[^"]*")\s*,\s*' + lit, text):
        found.append(m.group(1))
    # gs/gs_input.c: what is said of the controllers (say_add, learn_say, learn_stop) and the setup's parts (kLearn)
    for m in re.finditer(r'\bsay_add\(\s*\w+\s*,\s*sizeof\(\w+\)\s*,\s*' + lit, text):
        found.append(m.group(1))
    for m in re.finditer(r'\blearn_say\(\s*[^,()]+\s*,\s*' + lit, text):
        found.append(m.group(1))
    for m in re.finditer(r'\blearn_stop\(\s*' + lit, text):
        found.append(m.group(1))
    for m in re.finditer(r'\{\s*"[a-z]+"\s*,\s*' + lit + r',\s*' + lit + r',\s*L_', text):
        found.extend([m.group(1), m.group(2)])
    # tables whose texts are said through a variable: the glossary's {"name", "meaning", ...} and the settings
    # window's {"label", "tip"} entries
    for m in re.finditer(r'\{\s*' + lit + r',\s*' + lit + r'(?=,|\})', text) if tables else ():
        found.extend([m.group(1), m.group(2)])
    out = []
    for f in found:
        s = "".join(re.findall(r'"((?:[^"\\\n]|\\.)*)"', f))
        s = s.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")
        out.append(s)
    return out


def lang(path=None, sources=None):
    path = path or os.path.join(ROOT, "port/lang/a11y_es.txt")
    keys, wrong, note = set(), [], None
    for line in open(path, encoding="utf-8-sig"):
        line = line.rstrip("\r\n")
        if line.startswith("# "):
            note = line[2:]
            continue
        m = re.match(r"([0-9a-f]{16})\t(.+)$", line)
        if m:
            h = int(m.group(1), 16)
            keys.add(h)
            if note is not None and fnv1a(note.replace("\\n", "\n")) != h and not note.startswith("#"):
                wrong.append(note)
            note = None
    check(not wrong, "cada línea del archivo en español tiene la clave de su inglés", "todas, y no la tienen: " + " | ".join(wrong[:5]))
    sources = sources or [os.path.join(ROOT, p) for p in (
        "port/src/a11y_game.c", "port/src/a11y_fight.c", "port/src/a11y_skills.c", "port/src/a11y_text.c",
        "port/src/gs/a11y.c", "port/src/gs/a11y_glossary.c", "port/src/gs/a11y_pad.c", "port/src/gs/a11y_ui.cpp",
        "port/src/gs/gs_input.c", "port/setup/setup.cpp")]
    missing, seen = [], 0
    for src in sources:
        for s in c_strings(open(src, encoding="utf-8").read(), os.path.basename(src) in ("a11y_glossary.c", "a11y_ui.cpp")):
            if s and re.search(r"[A-Za-z]{2}", s):
                seen += 1
                if fnv1a(s) not in keys and fnv1a(s + "|settings") not in keys:
                    missing.append("%s: %s" % (os.path.basename(src), s.replace("\n", " ")))
    check(not missing, "cada uno de los %d textos de %d archivos de la accesibilidad tiene su línea en español" % (seen, len(sources)),
          "todos, y faltan %d: %s" % (len(missing), " | ".join(missing[:8])))


def main(argv):
    if not sys.stdout.isatty():  # (into a file or a pipe: UTF-8, whatever the system's code page)
        sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    if cmd == "tones":
        tones(*args)
    elif cmd == "log":
        log(*args)
    elif cmd == "music":
        music(args[0], *args[1:2], *(int(a) for a in args[2:3]))
    elif cmd == "lang":
        lang(*args[:1], sources=args[1:] or None)
    else:
        print(__doc__)
        return 2
    print("resumen: %d correctas, %d fallas" % (results.count(True), results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
