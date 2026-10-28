#!/usr/bin/env python3
"""Maintain and compile the shared settings/setup interface translations."""
import argparse
import ast
import json
from pathlib import Path
import re
import tbo_strings
from translation_formats import slots

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [ROOT / "port/src/gs/ui.cpp", ROOT / "port/setup/setup.cpp"]
DIRECTORY = ROOT / "port/translations/interface"
LANGUAGES = [("en", "English"), ("es", "Español"), ("fr", "Français"), ("de", "Deutsch"), ("it", "Italiano"), ("ca", "Català")]
STRINGS = re.compile(r'"(?:[^"\\\n]|\\.)*"(?:\s*"(?:[^"\\\n]|\\.)*")*')
LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')

def collect():
    result = set()
    for source in SOURCES:
        code = tbo_strings.strip_comments(source.read_text(encoding="utf-8"))
        code = re.sub(r"\bGAME\b", '"Tenkaichi3Decomp"', code)
        for match in STRINGS.finditer(code):
            before = code[max(0, match.start()-300):match.start()]
            calls = re.findall(r"(\w+)\s*\((?![^()]*\))", before)
            if calls and calls[-1] in {"printf", "fprintf", "sscanf", "strcmp", "strncmp", "getenv", "fopen", "SDL_SetEnvironmentVariable", "SDL_SetStringProperty"}:
                continue
            value = "".join(ast.literal_eval(m.group()) for m in LITERAL.finditer(match.group()))
            for text in value.split("\0"):
                # Internal IDs, settings/environment keys, paths and numeric-only formats are not wording.
                if not re.search(r"[A-Za-z]{2}", text) or text.startswith(("##", "BT3_", "/usr/", "C:\\")):
                    continue
                if re.fullmatch(r"[a-z0-9_./-]+", text) and text not in {"done", "failed", "skipped", "none", "off", "on"}:
                    continue
                if re.search(r"\.(?:h|cpp|json|ppm|txt|dat|exe|rgba)$", text) or text.startswith(("@", "#version", "file://", "https://", "http://", "github.com/", "bt3:", "Tenkaichi3Decomp-setup:")):
                    continue
                if re.fullmatch(r"[a-z]+(?:;[a-z]+)+", text) or text.startswith("Tenkaichi3Decomp-setup --install"):
                    continue
                if re.fullmatch(r"\{[A-Za-z_][A-Za-z_0-9]*\}", text):
                    continue  # a formatter's token, not a UI label
                result.add(text)
    # These labels are returned dynamically to the controls table by the input layer.
    controls = (ROOT / 'port/src/gs/gs_input.c').read_text(encoding='utf-8')
    actions = controls.split('kActions[PORT_ACTIONS] = {', 1)[1].split('};', 1)[0]
    result.update(ast.literal_eval(m.group(1)) for m in re.finditer(r'\{"[^"]+",\s*("[^"]+")', actions))
    # Native setup errors are translated before publication to its event queue. Protocol keys stay English.
    native = tbo_strings.strip_comments((ROOT / 'port/setup/native.cpp').read_text(encoding='utf-8'))
    for match in re.finditer(r'Bt3Ui::T\(\s*(' + STRINGS.pattern + r')\s*\)', native):
        result.add(''.join(ast.literal_eval(m.group()) for m in LITERAL.finditer(match.group(1))))
    for match in re.finditer(r'say\(\s*(' + STRINGS.pattern + r')\s*\)', native):
        value = ''.join(ast.literal_eval(m.group()) for m in LITERAL.finditer(match.group(1)))
        if value.startswith(('@note ', '@ok ', '@skip ')):
            result.add(value.partition(' ')[2])
    # Network errors arrive as English data from the unchanged networking layer.
    # Collect the presentation messages, leaving packets, service IDs and logs alone.
    match_code = tbo_strings.strip_comments((ROOT / 'port/src/gs/net_match.c').read_text(encoding='utf-8'))
    net_code = tbo_strings.strip_comments((ROOT / 'port/src/gs/net.c').read_text(encoding='utf-8'))
    fragments = re.findall(r'set_state\(-1,\s*(.*?)\);', match_code, re.S)
    fragments += re.findall(r'static const char \*key_refused\(.*?\n\}', match_code, re.S)
    fragments += re.findall(r'static const char \*spec_no_text\(.*?\n\}', net_code, re.S)
    fragments += re.findall(r'k(?:DataDiffer|SpecNoAnswer)\[\]\s*=\s*(.*?);', net_code, re.S)
    fragments += re.findall(r'Port_UiNotice\((.*?)\);', net_code, re.S)
    for fragment in fragments:
        for match in STRINGS.finditer(fragment):
            value = ''.join(ast.literal_eval(m.group()) for m in LITERAL.finditer(match.group()))
            if re.match(r'[A-Z][A-Za-z]+ ', value):
                result.add(value)
    return sorted(result)

def key(original):
    return "interface/" + tbo_strings.line_id(original).split("/")[1]

def update():
    originals = collect()
    DIRECTORY.mkdir(exist_ok=True)
    for order, (locale, name) in enumerate(LANGUAGES, 1):
        path = DIRECTORY / (locale + ".json")
        old = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        previous = {r["original"]:r["text"] for r in old.get("labels",{}).values()}
        doc = {"schema_version":1, "labels_version":1, "translation_version":1,
               "labels_count":len(originals), "locale":locale, "name":name, "order":order,
               "labels":{key(s):{"original":s,"text":previous.get(s,s)} for s in originals}}
        path.write_text(json.dumps(doc,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")

def c_string(text):
    # JSON escapes are also C escapes here; control characters are not allowed in these catalogs.
    return json.dumps(text, ensure_ascii=False)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def catalogs():
    documents = [json.loads((DIRECTORY/(locale+".json")).read_text(encoding="utf-8"), object_pairs_hook=unique_object) for locale,name in LANGUAGES]
    originals = collect()
    assert len({key(s) for s in originals}) == len(originals), "Interface ID hash collision"
    for order, ((locale,name), doc) in enumerate(zip(LANGUAGES,documents), 1):
        assert doc["schema_version"] == 1 and doc["labels_version"] == 1
        assert doc["locale"] == locale and doc["name"] == name
        assert doc["order"] == order and doc["labels_count"] == len(originals)
        assert set(doc["labels"]) == {key(s) for s in originals}, f"Run interface_strings.py --update for {locale}"
        for original in originals:
            row = doc["labels"][key(original)]
            assert row["original"] == original, f"{locale}: {key(original)}: original changed"
            assert isinstance(row["text"], str), f"{locale}: {key(original)}: text is not a string"
            text = row["text"] or original
            assert all(ord(c) >= 32 or c in '\n\t' for c in text), f"{locale}: {key(original)}: control character"
            assert slots(original) == slots(text), f"{locale}: {key(original)}: format arguments changed"
            assert original.partition('##')[1:] == text.partition('##')[1:], f"{locale}: {key(original)}: widget identity changed"
    return documents


def generate(output):
    documents = catalogs()
    originals = collect()
    lines = ["// Generated by port/tools/interface_strings.py. Do not edit.",
             "struct Bt3UiLanguage { const char *locale, *name; };",
             "inline constexpr Bt3UiLanguage kBt3UiLanguages[] = {"]
    lines += ["    {"+c_string(locale)+", "+c_string(name)+"}," for locale,name in LANGUAGES]
    lines += ["};", f"struct Bt3UiRow {{ const char *original; const char *text[{len(LANGUAGES)}]; const char *label[{len(LANGUAGES)}]; }};",
              "inline constexpr Bt3UiRow kBt3UiRows[] = {"]
    for original in originals:
        texts = []
        labels = []
        for doc in documents:
            row = doc["labels"][key(original)]
            assert row["original"] == original
            text = row["text"] or original
            assert "\0" not in text
            texts.append(text)
            # Keep ImGui identity in English when visible labels change language.
            labels.append(text.split("##")[0] + "###" + original)
        lines.append("    {"+c_string(original)+", {"+", ".join(map(c_string,texts))+"}, {"+", ".join(map(c_string,labels))+"}},")
    lines.append("};\n")
    output.parent.mkdir(parents=True,exist_ok=True)
    content = "\n".join(lines)
    if not output.exists() or output.read_text(encoding="utf-8") != content:
        output.write_text(content,encoding="utf-8")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update",action="store_true",help="refresh catalog IDs, preserving existing translations")
    parser.add_argument("--output",type=Path,default=ROOT/"port/build/gen/gs/ui_catalog_generated.h")
    args=parser.parse_args()
    if args.update: update()
    generate(args.output)
    print(f"Interface catalogs: {len(collect())} messages, {len(LANGUAGES)} languages")

if __name__=="__main__": main()
