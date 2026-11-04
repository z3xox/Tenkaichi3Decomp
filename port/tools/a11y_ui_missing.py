#!/usr/bin/env python3
"""The settings window's texts that a language of the screen reader has no line for.

    python3 port/tools/a11y_ui_missing.py              Spanish (port/lang/a11y_es.txt)
    python3 port/tools/a11y_ui_missing.py fr           another language's file
    python3 port/tools/a11y_ui_missing.py --lines      the missing ones as lines to fill in and paste into the file

The window (port/src/gs/ui.cpp, F1) draws English; the reader (port/src/gs/a11y_ui.cpp) says each text the way the
language's file has it, found by the FNV-1a hash of the English (port/src/gs/a11y.c): the key "text|settings" first,
then "text". A text with a number or a name in it is said through one of a11y_ui.cpp's kPatterns, whose key is the
pattern ("%d frames"). This lists the texts of the window's source that have neither: the string literals given to
Dear ImGui's calls and to the window's own helpers, and the lists of choices (static arrays of strings), as they are
drawn (up to "##", "%%" as "%" where the call takes a format). A text with a conversion and no pattern is listed
apart: the reader cannot say it in another language until a11y_ui.cpp has its pattern.

One line of text for each finding, for reading with a screen reader. It reads the sources only; nothing is built."""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = ["port/src/gs/ui.cpp"]  # (fight_panels_ui.cpp draws fighters' names on the picture, not in the window)
PATTERNS = ROOT / "port/src/gs/a11y_ui.cpp"
# calls whose strings are not drawn: ids, settings, the environment, the log, comparisons
NOT_DRAWN = {"getenv", "SDL_getenv", "Port_Setting", "Port_SettingSave", "Port_SettingText", "Port_SettingTextSave", "fprintf",
             "printf", "puts", "strcmp", "strncmp", "strstr", "strchr", "SDL_strcmp", "SDL_Log", "fopen", "open_utf8", "SDL_SetHint",
             "ImGui::PushID", "ImGui::GetID", "ImGui::BeginChild", "ImGui::BeginTable", "ImGui::IsPopupOpen", "ImGui::OpenPopup",
             "ImGui::BeginTabBar", "ImGui::DockSpace", "ImGui::LoadIniSettingsFromDisk", "ImHashStr", "SDL_LoadFile",
             "MenuTranslation_SaveChoice", "load_strip", "Port_SettingsPath", "snprintf_path", "IM_ASSERT", "assert",
             "SDL_GetHint", "SDL_GetError", "SDL_SetClipboardText", "Ui_Note", "PortA11y_Note", "fputs", "strcpy", "strcat",
             "SDL_strlcpy", "SDL_strlcat", "sscanf", "strtol", "atoi", "SDL_strncmp", "tabFlags"}
# texts said as they are in every language: names
SAME = {"Vulkan", "OpenGL", "Direct3D 12", "Metal", "(F11)", "Tenkaichi Budokai Online"}
# functions that draw what the reader does not say: the connection meter over the picture (a11y_ui.cpp: "##meter")
SILENT = {"meter_draw"}
# calls that take a printf format (what is drawn has "%" for "%%")
FORMATS = {"ImGui::Text", "ImGui::TextWrapped", "ImGui::TextDisabled", "ImGui::TextColored", "ImGui::BulletText",
           "ImGui::SetTooltip", "ImGui::SetItemTooltip", "ImGui::LabelText", "snprintf", "sprintf", "tip", "help"}


def fnv1a(text):
    h = 0xCBF29CE484222325
    for b in text.encode("utf-8"):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "0": "\0", "a": "\a"}


def unescape(body):
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            e = body[i + 1]
            if e == "x":
                m = re.match(r"[0-9a-fA-F]+", body[i + 2:])
                out.append(chr(int(m.group(0), 16)))
                i += 2 + len(m.group(0))
                continue
            out.append(ESCAPES.get(e, e))
            i += 2
            continue
        out.append(c)
        i += 1
    raw = "".join(out)
    try:  # (the source is UTF-8: a literal's bytes as text)
        return raw.encode("latin-1").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        return raw


def literals(src):
    """(text, the call or list it is in, line) for every string literal of a C or C++ source, adjacent ones joined."""
    stack = []  # ('(' | '{', name)
    i, n, line = 0, len(src), 1
    pending, pending_line, pending_ctx = None, 0, None
    found = []

    def context():
        for kind, name in reversed(stack):
            if kind == "(" and name:
                return name
            if kind == "{":
                return name
        return ""

    def flush():
        nonlocal pending
        if pending is not None:
            found.append((pending, pending_ctx, pending_line))
            pending = None

    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            line += src.count("\n", i, n if j < 0 else j)
            i = n if j < 0 else j + 2
            continue
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if c == "#" and src[:i].rstrip(" \t").endswith("\n"):  # a preprocessor line
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "'":
            m = re.match(r"'(\\.|[^\\'])*'", src[i:])
            i += len(m.group(0)) if m else 1
            flush()
            continue
        if c == '"':
            m = re.match(r'"((?:\\.|[^\\"\n])*)"', src[i:])
            body = unescape(m.group(1))
            if pending is None:
                pending, pending_line, pending_ctx = body, line, context()
            else:
                pending += body
            i += len(m.group(0))
            continue
        flush()
        if c in "({":
            before = src[max(0, i - 200):i].rstrip()
            if c == "(":
                m = re.search(r"([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)$", before)
                name = m.group(1) if m else ""
                if name in ("if", "while", "for", "switch", "return", "sizeof"):
                    name = ""
            else:
                name = "[]" if re.search(r"\[[A-Za-z0-9_ ]*\]\s*=$", before) else ""
            stack.append((c, name))
        elif c in ")}" and stack:
            stack.pop()
        i += 1
    flush()
    return found


def listed(src, name):
    """The strings of one of a11y_ui.cpp's lists, as written in its source."""
    start = src.index(name + "[] = {")
    end = src.index("};", start)
    return [text for text, _, _ in literals(src[start + len(name + "[] = "):end + 1])]


def as_regex(pattern):
    """A pattern as a11y_ui.cpp's match() takes it: a number for %d, as little as fits for %s."""
    out, i = "", 0
    while i < len(pattern):
        if pattern.startswith("%%", i):
            out += "%"
            i += 2
            continue
        m = re.match(r"%[-+ #0-9.hlz]*([a-zA-Z])", pattern[i:])
        if m:
            conv = m.group(1)
            out += ".+?" if conv == "s" else "." if conv == "c" else r"[-+]?[0-9a-fA-F]+" if conv in "xX" else r"[-+]?[0-9.]+" if conv in "feEgG" else r"[-+]?[0-9]+"
            i += len(m.group(0))
            continue
        out += re.escape(pattern[i])
        i += 1
    return re.compile(out + r"\Z", re.S)


def patterns():
    """a11y_ui.cpp's kPatterns (each with its regex) and kNotPatterns."""
    src = PATTERNS.read_text(encoding="utf-8")
    return [(p, as_regex(p)) for p in listed(src, "kPatterns")], set(listed(src, "kNotPatterns"))


def lang_hashes(path):
    keys = set()
    for raw in path.read_text(encoding="utf-8-sig").splitlines():
        if len(raw) > 17 and raw[16] == "\t" and not raw.startswith("#") and not raw.startswith("~"):
            keys.add(int(raw[:16], 16))
    return keys


def window_texts():
    """(what is drawn, its key or None when it needs a pattern, where) for every text of the window's sources."""
    pats, own = patterns()
    seen, out = set(), []
    for rel in SOURCES:
        path = ROOT / rel
        if not path.exists():
            continue
        src = path.read_text(encoding="utf-8")
        # (the function each text is in: a definition starts a line, "name(...) {")
        starts = [(src.count("\n", 0, m.start()) + 1, m.group(1))
                  for m in re.finditer(r"^[A-Za-z][\w \*:&]*?\b(\w+)\([^;{]*\)\s*\{", src, re.M)]
        for text, ctx, line in literals(src):
            func = next((name for first, name in reversed(starts) if first <= line), "")
            if func in SILENT or ctx in NOT_DRAWN or not (ctx.startswith("ImGui::") or ctx == "[]" or (ctx and not ctx.startswith("Port"))):
                continue
            # as the reader has it: up to "##", one space between words, no spaces around a line
            text = "\n".join(re.sub(" +", " ", part).strip() for part in text.split("##", 1)[0].split("\n"))
            if not re.search(r"[A-Za-z]", text) or text in SAME or text.startswith(("#", "http", "github.com")):
                continue
            if re.fullmatch(r"[A-Za-z0-9_:./\\-]+", text) and re.search(r"[_./\\]", text):
                continue  # (a setting's name, a file, a path)
            if re.fullmatch(r"(%[-+ #0-9.hlz]*[a-zA-Z]|[^A-Za-z%])*", text) or re.fullmatch(r"\S*/\S*\.\w+", text):
                continue  # (formats and marks only, "%s: %s"; a path, "%s/names.rgba")
            if ctx in ("snprintf", "sprintf") and re.match(r"[a-z]+: ", text):
                continue  # (a note for the log: "controls: player %d ...")
            if re.fullmatch(r"%d%%(%s)?", text):
                continue  # (a number of percent: said as it is drawn)
            drawn = text.replace("%%", "%") if ctx in FORMATS else text
            pattern = next((p for p, rx in pats if p == text or (drawn not in own and rx.match(drawn))), None)
            if pattern is not None:
                key = pattern
            elif "%" in text.replace("%%", ""):
                key = None
            else:
                key = drawn
            if (key or text) in seen:
                continue
            seen.add(key or text)
            out.append((text, key, f"{rel}:{line}"))
    return out


def shown(text):
    return text.replace("\\", "\\\\").replace("\n", "\\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    code = args[0] if args else "es"
    lang = ROOT / f"port/lang/a11y_{code}.txt"
    if not lang.exists():
        sys.exit(f"no {lang.relative_to(ROOT)}")
    have = lang_hashes(lang)
    texts = window_texts()
    missing = [(t, k, w) for t, k, w in texts if k is not None and fnv1a(k) not in have and fnv1a(k + "|settings") not in have]
    nopattern = [(t, w) for t, k, w in texts if k is None]
    if "--lines" in sys.argv:
        for text, key, where in missing:
            print(f"# {shown(key)}")
            print(f"{fnv1a(key):016x}\t")
        return
    print(f"{len(texts)} texts of the settings window, {len(missing)} without a line in {lang.relative_to(ROOT)}, "
          f"{len(nopattern)} with a number or a name and no pattern in a11y_ui.cpp.")
    for text, key, where in missing:
        print(f"missing, {where}: {shown(key)}")
    for text, where in nopattern:
        print(f"no pattern, {where}: {shown(text)}")


if __name__ == "__main__":
    main()
