#!/usr/bin/env python3
"""Validate bundled translations; optionally exercise the native loader.

Build the integration runner from the repository root:
  gcc -std=gnu99 -g -O1 -no-pie -fsanitize=address,undefined -Iport/src/gs \
      port/tests/menu_font/translations.c port/src/gs/menu_translation.c \
      port/src/gs/ui_text_translate.c port/src/plat_settings.c -lpthread \
      -o port/build/translations_test
  python3 port/tools/check_translations.py --runtime port/build/translations_test
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import shutil

import tbo_strings
import interface_strings
from translation_formats import slots

ROOT = Path(__file__).resolve().parents[2]
CATALOGS = ROOT / "port/translations"
LANGUAGES = {"es": "Español", "fr": "Français", "de": "Deutsch", "it": "Italiano", "ca": "Català"}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read(path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def online_rows():
    result = dict(read(CATALOGS / "port/en.json")["labels"])
    for source in tbo_strings.SOURCES:
        for original, note in tbo_strings.collect(source):
            result[tbo_strings.line_id(original)] = {"original": original}
    return result


def validate():
    expected = {"labels": read(CATALOGS / "labels/en.json")["labels"], "port": online_rows()}
    total = 0
    for order, (locale, name) in enumerate(LANGUAGES.items(), 2):
        for kind, source in expected.items():
            path = CATALOGS / kind / (locale + ".json")
            catalog = read(path)
            assert catalog["schema_version"] == 1, path
            assert catalog["locale"] == locale and catalog["name"] == name, path
            assert catalog["order"] == order, path
            rows = catalog["labels"]
            assert catalog["labels_count"] == len(rows), path
            assert rows.keys() == source.keys(), f"{path}: missing or stale IDs"
            for key, row in rows.items():
                original, text = row["original"], row["text"]
                assert original == source[key]["original"], f"{path}:{key}: original changed"
                assert isinstance(text, str) and text.strip(), f"{path}:{key}: empty translation"
                assert "\0" not in text, f"{path}:{key}: embedded NUL"
                assert len(text.encode("utf-8")) < (256 if kind == "labels" else 1024), f"{path}:{key}: too long"
                assert slots(original) == slots(text), f"{path}:{key}: format placeholders differ"
                if kind == "labels":
                    assert "|" not in text, f"{path}:{key}: label contains group separator"
                total += 1
    print(f"10 catalogs: {total} entries, IDs, metadata, UTF-8 limits, and placeholders OK")
    documents = interface_strings.catalogs()
    print(f"6 interface catalogs: {sum(len(d['labels']) for d in documents)} entries, IDs, widget identities, and format arguments OK")


def runtime(executable):
    executable = executable.resolve()
    runs = 0
    for locale in LANGUAGES:
        for mode in ("bundled", "override", "bad-format"):
            labels = read(CATALOGS / "labels" / (locale + ".json"))
            port = read(CATALOGS / "port" / (locale + ".json"))
            with tempfile.TemporaryDirectory(prefix="bt3-translations-") as directory:
                root = Path(directory)
                if mode == "override":
                    # Existing user catalogs must survive the first-run bundle copy.
                    labels["labels"]["main-6/0/0"]["text"] = "Personalitzat: à, ç, ï, ü, ß"
                    folder = root / "translations/labels"
                    folder.mkdir(parents=True)
                    (folder / (locale + ".json")).write_text(json.dumps(labels, ensure_ascii=False), encoding="utf-8")
                line = port["labels"][tbo_strings.line_id("Match %d: %s vs %s")]["text"]
                if mode == "bad-format":
                    row = port["labels"][tbo_strings.line_id("Match %d: %s vs %s")]
                    row["text"] = "Missing player names: %d"
                    folder = root / "translations/port"
                    folder.mkdir(parents=True)
                    (folder / (locale + ".json")).write_text(json.dumps(port, ensure_ascii=False), encoding="utf-8")
                    line = ""
                subprocess.run([
                    str(executable), str(root), locale,
                    labels["labels"]["main-6/0/0"]["text"],
                    port["labels"]["quit/confirm"]["text"],
                    port["labels"][tbo_strings.line_id("Quick Match")]["text"], line,
                ], cwd=ROOT, env=dict(os.environ, BT3_SETTINGS=""), check=True)
                runs += 1
    print(f"{runs} native runs: five locales, user edits preserved, invalid formats rejected")


def reject_invalid_interface_catalogs():
    source = interface_strings.DIRECTORY
    with tempfile.TemporaryDirectory(prefix="bt3-interface-check-") as directory:
        root = Path(directory) / 'interface'
        shutil.copytree(source, root)
        path = root / 'es.json'
        original = path.read_text(encoding='utf-8')
        try:
            interface_strings.DIRECTORY = root
            for kind in ('float-argument', 'widget-id', 'duplicate-key'):
                doc = json.loads(original)
                if kind == 'duplicate-key':
                    text = original.rstrip()[:-1] + ',"locale":"es"}\n'
                else:
                    row = next(r for r in doc['labels'].values() if r['original'] == ('stick  %+.2f %+.2f' if kind == 'float-argument' else 'Deadzone##%d'))
                    row['text'] = 'stick %s %+.2f' if kind == 'float-argument' else row['text'].replace('##', '##changed')
                    text = json.dumps(doc, ensure_ascii=False)
                path.write_text(text, encoding='utf-8')
                try:
                    interface_strings.catalogs()
                except (AssertionError, ValueError):
                    pass
                else:
                    raise AssertionError(f"interface catalog accepted {kind}")
        finally:
            interface_strings.DIRECTORY = source
    print('Invalid interface printf arguments, changed widget IDs, and duplicate JSON keys rejected')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--runtime", type=Path, help="compiled port/tests/menu_font/translations.c runner")
    args = parser.parse_args()
    validate()
    reject_invalid_interface_catalogs()
    if args.runtime:
        runtime(args.runtime)


if __name__ == "__main__":
    main()
