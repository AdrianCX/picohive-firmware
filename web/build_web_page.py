#!/usr/bin/env python3
#
# Builds a board's web page from its page.yaml, see README.md next to this:
#
#   build_web_page.py PAGE_YAML -o OUTPUT_HTML [--libraries a,b] [--depfile F] [--explain]
#
# The page is os/web/base (skins, socket, HUD, the Look and Server sections)
# plus the features page.yaml turns on, plus the board's own sections and
# pages, plus the board's text. pack_web_page.py then minifies and gzips it
# as it does any hand-written page.
import argparse
import difflib
import html
import json
import os
import re
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, "base")
FEATURES = os.path.join(HERE, "features")

SKINS = ["cartoon", "clean", "xeno", "fallout"]
FALLBACK = "cartoon"            # T() falls back to it, so it must have every key
SHOWN = "clean"                 # the skin a first visit gets: <title> and <h1> before any script runs

PARAM = re.compile(r"\{\{\s*([\w.]+)\s*\}\}")


class PageError(Exception):
    pass


def fail(message):
    raise PageError(message)


def suggest(word, choices):
    close = difflib.get_close_matches(word, list(choices), n=3)
    return (" (did you mean " + ", ".join(close) + "?)") if close else ""


# --- page.yaml ---------------------------------------------------------------

def read_yaml(path, used):
    used.add(path)
    try:
        with open(path, encoding="utf-8") as f:
            return yaml.safe_load(f) or {}
    except FileNotFoundError:
        fail(f"{path}: no such file")


# Where page.yaml adds the board's own parts: config.add_section, tabs.add_tab.
ADDS = (("config", "add_section"), ("tabs", "add_tab"))
KIND = {"config": "section", "tabs": "page"}


def resolve_paths(page, folder):
    """Paths in a page.yaml are relative to that file; make them absolute so an
    included file's paths still point where its author meant."""
    def rel(p):
        return p if os.path.isabs(p) else os.path.normpath(os.path.join(folder, p))

    if isinstance(page.get("text"), str):
        page["text"] = rel(page["text"])
    elif isinstance(page.get("text"), list):
        page["text"] = [rel(p) if isinstance(p, str) else p for p in page["text"]]

    for block, add in ADDS:
        added = (page.get(block) or {}).get(add) or {}
        for name, entry in added.items():
            if isinstance(entry, str):
                added[name] = rel(entry)
            elif isinstance(entry, dict):
                for key in ("from", "html", "script", "style"):
                    if isinstance(entry.get(key), str):
                        entry[key] = rel(entry[key])
    return page


def merge(base, over):
    """over wins; maps merge key by key, anything else is replaced."""
    if not isinstance(base, dict) or not isinstance(over, dict):
        return over
    out = dict(base)
    for key, value in over.items():
        out[key] = merge(base[key], value) if key in base else value
    return out


def load_page(path, used, seen=()):
    path = os.path.abspath(path)
    if path in seen:
        fail(f"{path}: includes itself")
    page = resolve_paths(read_yaml(path, used), os.path.dirname(path))

    includes = page.pop("include", [])
    if isinstance(includes, str):
        includes = [includes]
    merged = {}
    for inc in includes:
        inc = os.path.normpath(os.path.join(os.path.dirname(path), inc))
        merged = merge(merged, load_page(inc, used, seen + (path,)))
    return merge(merged, page)


# --- strings -------------------------------------------------------------------

class Strings:
    """Every skin's text. Keys come from the base, the features and inline
    sections; the board's text may only change keys that exist."""

    def __init__(self):
        self.table = {}             # key -> {skin: text}
        self.source = {}            # key -> where it was defined
        self.narration = {s: [] for s in SKINS}
        self.lost = []
        self.unused = set()         # keys of features that are off: text for them is skipped
        self.skipped = []

    @staticmethod
    def per_skin(value, where, key):
        if isinstance(value, dict):
            unknown = [s for s in value if s not in SKINS]
            if unknown:
                fail(f"{where}: {key}: no skin {unknown[0]}{suggest(unknown[0], SKINS)}")
            return {s: str(v) for s, v in value.items()}
        return {s: str(value) for s in SKINS}

    def define(self, key, value, where):
        if key in self.table:
            fail(f"{where}: {key} is already defined by {self.source[key]}")
        self.table[key] = self.per_skin(value, where, key)
        self.source[key] = where

    def add_lines(self, data, where, replace):
        if "lost" in data:
            self.lost = list(data["lost"]) if replace else self.lost + list(data["lost"])
        for skin, lines in (data.get("narration") or {}).items():
            if skin not in SKINS:
                fail(f"{where}: narration: no skin {skin}{suggest(skin, SKINS)}")
            if isinstance(lines, str):          # "clean: cartoon" - the same lines
                self.narration[skin] = lines
            elif replace or isinstance(self.narration[skin], str):
                self.narration[skin] = list(lines)
            else:
                self.narration[skin] = self.narration[skin] + list(lines)

    def load(self, path, where, used):
        used.add(path)
        with open(path, encoding="utf-8") as f:
            data = yaml.safe_load(f) or {}
        for key, value in data.items():
            if key not in ("narration", "lost"):
                self.define(key, value, where)
        self.add_lines(data, where, replace=False)

    def override(self, data, where):
        for key, value in data.items():
            if key in ("narration", "lost"):
                continue
            if key not in self.table and key in self.unused:
                self.skipped.append(key)    # its feature is off; turning it back on uses it again
                continue
            if key not in self.table:
                fail(f"{where}: {key} is not a string any part of this page uses{suggest(key, self.table)}")
            self.table[key].update(self.per_skin(value, where, key))
            self.source[key] += f", {where}"
        self.add_lines(data, where, replace=True)

    def text(self, key, skin=SHOWN):
        entry = self.table.get(key, {})
        return entry.get(skin, entry.get(FALLBACK, ""))

    def js(self):
        str_table = {s: {} for s in SKINS}
        for key, entry in self.table.items():
            for skin, value in entry.items():
                str_table[skin][key] = value
            # T() falls back to cartoon, so a key only some skins have still shows.
            if FALLBACK not in entry:
                str_table[FALLBACK][key] = entry.get(SHOWN, next(iter(entry.values())))
        narration = {}
        for skin in SKINS:
            lines = self.narration[skin]
            narration[skin] = self.narration[lines] if isinstance(lines, str) else lines
        dump = lambda v: json.dumps(v, ensure_ascii=False, indent=4)
        return ("// Generated by build_web_page.py from every strings.yaml and the board's text.\n"
                f'var THEMES = {json.dumps(SKINS)};\n'
                f"var STR = {dump(str_table)};\n"
                f"var PAGE_NARRATION = {dump(narration)};\n"
                f"var PAGE_LOST = {dump(self.lost)};\n")


# --- parts ---------------------------------------------------------------------

class Part:
    """One section of Config or one page (a main tab) of the page."""

    def __init__(self, name, kind, where):
        self.name = name
        self.kind = kind            # "section" | "page"
        self.where = where
        self.html = ""
        self.css = []
        self.js = []
        self.dom_id = f"sub_{name}" if kind == "section" else name.capitalize()
        self.library = None

    @property
    def label_key(self):
        return f"sub.{self.name}" if self.kind == "section" else f"tab.{self.name}"


def read(path, used):
    used.add(path)
    with open(path, encoding="utf-8") as f:
        return f.read()


def substitute(text, params, where):
    def one(m):
        if m.group(1) not in params:
            fail(f"{where}: {{{{{m.group(1)}}}}} is not set{suggest(m.group(1), params)}")
        return str(params[m.group(1)])
    return PARAM.sub(one, text)


def feature_kind(folder):
    return "section" if os.path.exists(os.path.join(folder, "section.html")) else "page"


def feature_default(folder, used):
    """A built-in a board does not mention is off, unless its feature.yaml says
    default: on (Firmware, so no board loses its upgrade path by accident)."""
    meta = os.path.join(folder, "feature.yaml")
    return bool(os.path.exists(meta) and read_yaml(meta, used).get("default"))


def folder_part(name, folder, options, strings, used, want_kind=None):
    """A feature folder: section.html or page.html, its *.css and *.js, its
    strings.yaml and, when it needs one, a feature.yaml."""
    where = os.path.relpath(folder)
    if not os.path.isdir(folder):
        fail(f"{where}: no such feature folder")

    meta = {}
    if os.path.exists(os.path.join(folder, "feature.yaml")):
        meta = read_yaml(os.path.join(folder, "feature.yaml"), used)

    has_section = os.path.exists(os.path.join(folder, "section.html"))
    has_page = os.path.exists(os.path.join(folder, "page.html"))
    if has_section == has_page:
        fail(f"{where}: needs exactly one of section.html (a Config section) or page.html (a tab)")
    kind = "section" if has_section else "page"
    if want_kind and kind != want_kind:
        fail(f"{where}: is a {'tab' if kind == 'page' else 'Config section'}, "
             f"add it with {'tabs: add_tab' if kind == 'page' else 'config: add_section'}")

    # Options: declared in feature.yaml, set in page.yaml, used as {{name}}.
    declared = meta.get("options") or {}
    params = {}
    for opt, spec in declared.items():
        spec = spec or {}
        if opt in options:
            value = options[opt]
            if "choices" in spec and value not in spec["choices"]:
                fail(f"{where}: {opt}: {value!r} is not one of {spec['choices']}")
            params[opt] = value
        elif "default" in spec:
            params[opt] = spec["default"]
        elif spec.get("required"):
            fail(f"{where}: needs {opt}: set it in page.yaml")
    for opt in options:
        if opt not in declared:
            fail(f"{where}: has no option {opt}{suggest(opt, declared)}")

    part = Part(name, kind, where)
    part.dom_id = meta.get("id", part.dom_id)
    part.library = meta.get("library")

    def files(listed, ext):
        if listed is None:
            return sorted(f for f in os.listdir(folder) if f.endswith(ext))
        return [substitute(f, params, where) for f in listed]

    load = lambda f: substitute(read(os.path.join(folder, f), used), params, f"{where}/{f}")
    part.html = load(f"{kind}.html")
    part.css = [(f"{where}/{f}", load(f)) for f in files(meta.get("styles"), ".css")]
    part.js = [(f"{where}/{f}", load(f)) for f in files(meta.get("scripts"), ".js")]

    if os.path.exists(os.path.join(folder, "strings.yaml")):
        strings.load(os.path.join(folder, "strings.yaml"), f"{where}/strings.yaml", used)
    return part


def label_html(key, strings):
    return html.escape(strings.text(key))


def inline_part(name, kind, entry, strings, used, where):
    """A section or page written straight into page.yaml: buttons and fields,
    no folder of its own."""
    known = {"label", "title", "buttons", "fields", "html", "script", "style"}
    for key in entry:
        if key not in known:
            fail(f"{where}: {name}: no setting {key}{suggest(key, known)}")

    part = Part(name, kind, f"{where}: {name}")
    strings.define(part.label_key, entry.get("label", name.capitalize()), part.where)

    rows = []
    if "title" in entry:
        strings.define(f"{name}.title", entry["title"], part.where)
        rows.append(f'  <th colspan="2" data-t="{name}.title">{label_html(name + ".title", strings)}</th>')

    for i, field in enumerate(entry.get("fields") or [], 1):
        rows.append(field_html(name, i, field, strings, part.where))

    buttons = []
    for i, button in enumerate(entry.get("buttons") or [], 1):
        if isinstance(button, dict) and len(button) == 1 and "label" not in button:
            (label, command), = button.items()
            button = {"label": label, "command": command}
        key = f"{name}.b{i}"
        strings.define(key, button["label"], part.where)
        if "command" in button:
            action = f"sendCommand({json.dumps(button['command'])}, T('{key}'))"
        elif "onclick" in button:
            action = button["onclick"]
        else:
            fail(f"{part.where}: button {i} needs command: or onclick:")
        if button.get("confirm"):
            ckey = f"{key}.confirm"
            strings.define(ckey, button["confirm"], part.where)
            action = f"if (confirm(T('{ckey}'))) {action}"
        cls = ' class="danger"' if button.get("danger") else ""
        buttons.append(f'    <input type="button"{cls} data-t="{key}" value="{label_html(key, strings)}" '
                       f'onclick="{html.escape(action)}">')

    out = []
    if rows:
        out.append("<table>\n" + "\n".join(rows) + "\n</table>")
    if buttons:
        out.append('<div class="control-panel app-buttons">\n' + "\n".join(buttons) + "\n</div>")
    if "html" in entry:
        out.append(read(entry["html"], used))
    part.html = "\n".join(out) + "\n"
    for key, attr in (("script", "js"), ("style", "css")):
        if key in entry:
            getattr(part, attr).append((os.path.relpath(entry[key]), read(entry[key], used)))
    return part


FIELD_TYPES = ("text", "number", "checkbox", "secret", "select")


def field_html(section, i, field, strings, where):
    path = field.get("config")
    if not path:
        fail(f"{where}: field {i} needs config: (where it lives in the settings, e.g. app.city)")
    kind = field.get("type", "text")
    if kind not in FIELD_TYPES:
        fail(f"{where}: field {i}: no type {kind}{suggest(kind, FIELD_TYPES)}")
    dom_id = "cfg_" + re.sub(r"\W", "_", path)
    key = f"{section}.f{i}"
    strings.define(key, field.get("label", path.split(".")[-1] + ":"), where)

    attrs = f'id="{dom_id}" data-config="{html.escape(path)}"'
    if "default" in field and kind != "checkbox":
        attrs += f' data-default="{html.escape(str(field["default"]))}"'
    if "placeholder" in field:
        attrs += f' placeholder="{html.escape(str(field["placeholder"]))}"'

    if kind == "checkbox":
        checked = " checked" if field.get("default") else ""
        control = f'<input type="checkbox" {attrs}{checked}>'
    elif kind == "select":
        choices = field.get("choices") or {}
        if isinstance(choices, list):
            choices = {c: c for c in choices}
        options = "".join(f'<option value="{html.escape(str(v))}"{" selected" if v == field.get("default") else ""}>'
                          f'{html.escape(str(t))}</option>' for v, t in choices.items())
        control = f'<select {attrs}>{options}</select>'
    elif kind == "secret":
        control = (f'<div class="secret-row"><input type="text" {attrs} value="" data-hint="" autocomplete="off" spellcheck="false">'
                   f'<input type="button" id="{dom_id}_forget" data-t="cfg.forget" value="Forget" '
                   f'onclick="forgetSecret(\'{dom_id}\')" style="display:none"></div>')
    else:
        number = ' data-type="number"' if kind == "number" else ""
        control = f'<input type="text" {attrs}{number} value="">'

    return (f'  <tr>\n    <td style="width: 130px;"><label for="{dom_id}" data-t="{key}">{label_html(key, strings)}</label></td>\n'
            f"    <td>{control}</td>\n  </tr>")


def base_section(name, strings, used):
    part = Part(name, "section", f"base/sections/{name}.html")
    part.html = read(os.path.join(BASE, "sections", f"{name}.html"), used)
    return part


# --- the page ------------------------------------------------------------------

def is_on(value):
    return value is True or isinstance(value, dict)


def build(page_path, libraries=None):
    used = set()
    page = load_page(page_path, used)
    where = os.path.relpath(page_path)

    known = {"name", "text", "config", "tabs"}
    for key in page:
        if key not in known:
            fail(f"{where}: no setting {key}{suggest(key, known)}")

    strings = Strings()
    strings.load(os.path.join(BASE, "strings.yaml"), "base/strings.yaml", used)

    available = sorted(os.listdir(FEATURES))
    for name in available:
        path = os.path.join(FEATURES, name, "strings.yaml")
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                strings.unused.update(k for k in (yaml.safe_load(f) or {}) if k not in ("narration", "lost"))
    kinds = {name: feature_kind(os.path.join(FEATURES, name)) for name in available}
    defaults = {name: feature_default(os.path.join(FEATURES, name), used) for name in available}

    # config: is the Config tab, tabs: the row of tabs. In each, a built-in is
    # on, off, or on with its options; add_section / add_tab add the board's own.
    parts = {"config": [], "tabs": []}
    added = {"config": [], "tabs": []}
    off = []
    for block, add in ADDS:
        kind = KIND[block]
        settings = page.get(block) or {}
        if not isinstance(settings, dict):
            fail(f"{where}: {block}: a map of features (on / off) and {add}")
        for name in available:
            if kinds[name] == kind and name not in settings and defaults[name]:
                settings = dict(settings, **{name: True})     # on unless the board says off
        for name, value in settings.items():
            if (name == add) or ((block == "tabs") and (name == "order")):
                continue
            if name not in available:
                other = [n for n in available if kinds[n] == kind] + [add]
                fail(f"{where}: {block}: no built-in {name}{suggest(name, other)}; "
                     f"there are {', '.join(other[:-1])}, or use {add}: for this board's own")
            if kinds[name] != kind:
                right = next(b for b in KIND if KIND[b] == kinds[name])
                fail(f"{where}: {block}: {name} is a {'tab' if kinds[name] == 'page' else 'Config section'}, put it under {right}:")
            if value is False:
                off.append(name)
                continue
            if not is_on(value):
                fail(f"{where}: {block}: {name}: use on, off, or its options")
            parts[block].append(folder_part(name, os.path.join(FEATURES, name),
                                            value if isinstance(value, dict) else {}, strings, used))

        for name, entry in (settings.get(add) or {}).items():
            if name in available:
                fail(f"{where}: {block}: {add}: {name} is a built-in, switch it on with `{name}: on` instead")
            if isinstance(entry, str):
                part = folder_part(name, entry, {}, strings, used, kind)
            elif isinstance(entry, dict) and "from" in entry:
                options = {k: v for k, v in entry.items() if k != "from"}
                part = folder_part(name, entry["from"], options, strings, used, kind)
            elif isinstance(entry, dict):
                part = inline_part(name, kind, entry, strings, used, where)
            else:
                fail(f"{where}: {block}: {add}: {name}: a folder, or a map of its settings")
            added[block].append(part)

    # General and Server first, then the built-ins, then the board's own sections.
    # The board's own tabs come right after Config; the built-in tabs (Firmware) stay last.
    sections = [base_section("look", strings, used), base_section("server", strings, used)] \
        + parts["config"] + added["config"]
    pages = added["tabs"] + parts["tabs"]
    row = tab_row(pages, (page.get("tabs") or {}).get("order"), where)

    # Only a warning: CMake hands over the libraries the target links itself,
    # and one can come in through another.
    if libraries is not None:
        for part in sections + pages:
            if part.library and part.library not in libraries:
                print(f"build_web_page: warning: {part.where} is on, but the firmware does not link {part.library}",
                      file=sys.stderr)

    # The board's wording last: a name, then its text file(s) or inline text.
    if "name" in page:
        strings.override({"title": page["name"], "app.title": page["name"]}, where + ": name")
    texts = page.get("text") or []
    for text in (texts if isinstance(texts, list) else [texts]):
        if isinstance(text, str):
            strings.override(read_yaml(text, used), os.path.relpath(text))
        else:
            strings.override(text, where + ": text")

    return assemble(sections, pages, row, strings, used), {
        "sections": sections, "pages": pages, "row": row, "off": off, "strings": strings, "used": used}


CONFIG_TAB = None       # Config's place in the row of tabs; every other entry is a Part


def tab_row(pages, order, where):
    """The row of tabs, left to right: Config, the board's own, the built-ins;
    or tabs: order: first, in that order, and the rest after them as they were.
    The first one is the tab the page opens on."""
    row = [CONFIG_TAB] + pages
    if order is None:
        return row
    by_name = {"config": CONFIG_TAB, **{p.name: p for p in pages}}
    if not isinstance(order, list):
        fail(f"{where}: tabs: order: a list of tabs, e.g. [arm, config]")
    for name in order:
        if name not in by_name:
            fail(f"{where}: tabs: order: no tab {name}{suggest(name, by_name)}")
    if len(set(order)) != len(order):
        fail(f"{where}: tabs: order: a tab named twice")
    first = [by_name[n] for n in order]
    return first + [entry for entry in row if entry not in first]


def tab_button(part, active, strings):
    if part is CONFIG_TAB:
        key, dom_id, text = "tab.config", "Config", label_html("tab.config", strings)
    else:
        key, dom_id, text = part.label_key, part.dom_id, label_html(part.label_key, strings)
    return (f'        <button class="tablinks{active}" data-t="{key}" data-tab="{dom_id}" '
            f"onclick=\"changeTab(event, '{dom_id}')\">{text}</button>")


def assemble(sections, pages, row, strings, used):
    parts = sections + pages
    tabs, subtabs, section_html, page_html = [], [], [], []

    for i, part in enumerate(row):
        tabs.append(tab_button(part, " active" if i == 0 else "", strings))

    for i, part in enumerate(sections):
        active = " active" if i == 0 else ""
        hidden = "" if i == 0 else ' style="display:none"'
        subtabs.append(f'        <button class="tablinks subtab{active}" data-t="{part.label_key}" '
                       f"onclick=\"changeSubTab(event, '{part.dom_id}')\">{label_html(part.label_key, strings)}</button>")
        section_html.append(f"      <!-- {part.where} -->\n"
                            f'      <div id="{part.dom_id}" class="subtabcontent"{hidden}>\n'
                            + indent(part.html, 6) + "      </div>\n")
    for part in pages:
        page_html.append(f"    <!-- {part.where} -->\n"
                         f'    <div id="{part.dom_id}" class="tabcontent">\n'
                         + indent(part.html, 6) + "    </div>\n")

    def chunks(items):
        return "\n".join(f"/* --- {src} --- */\n{text}" for src, text in items)

    style = [("base/skins.css", read(os.path.join(BASE, "skins.css"), used))] + [c for p in parts for c in p.css]
    scripts = [("base/core.js", read(os.path.join(BASE, "core.js"), used))] + [j for p in parts for j in p.js]

    fill = {
        "@style": chunks(style),
        "@strings": strings.js(),
        "@scripts": chunks(scripts),
        "@tabs": "\n".join(tabs),
        "@subtabs": "\n".join(subtabs),
        "@sections": "\n".join(section_html),
        "@pages": "\n".join(page_html),
    }

    out = []
    for line in read(os.path.join(BASE, "page.html"), used).split("\n"):
        m = re.fullmatch(r"\s*<!-- (@\w+)(?: (\S+))? -->", line)
        if m and m.group(1) == "@include":
            out.append(read(os.path.join(BASE, m.group(2)), used).rstrip("\n"))
        elif m:
            out.append(fill[m.group(1)])
        else:
            out.append(line.replace("{{title}}", html.escape(strings.text("title")))
                           .replace("{{app.title}}", html.escape(strings.text("app.title"))))
    page = "\n".join(out)
    check(page, strings)
    return page


def indent(text, n):
    pad = " " * n
    return "".join((pad + line if line.strip() else line) for line in text.splitlines(True))


def check(page, strings):
    """What would otherwise only show up in a browser, if at all."""
    markup = re.sub(r"<(script|style)\b.*?</\1>", "", page, flags=re.S | re.I)
    markup = re.sub(r"<!--.*?-->", "", markup, flags=re.S)

    ids = re.findall(r'\bid="([^"]+)"', markup)
    dupes = sorted({i for i in ids if ids.count(i) > 1})
    if dupes:
        fail(f"element ids used twice: {', '.join(dupes)}")

    used_keys = set(re.findall(r'data-t="([^"]+)"', markup)) | set(re.findall(r'\bT\(["\']([\w.]+)["\']\)', page))
    missing = sorted(k for k in used_keys if k not in strings.table)
    if missing:
        fail("text used but not defined in any strings.yaml: " + ", ".join(
            k + suggest(k, strings.table) for k in missing))

    # An element a script asks for by name but that is not in the page: a
    # feature turned off whose code is still called, or a typo.
    ids = set(ids)
    for name in sorted(set(re.findall(r'getElementById\(["\']([\w-]+)["\']\)', page))):
        if name not in ids:
            print(f"build_web_page: warning: a script uses #{name}, which is not in the page", file=sys.stderr)


def explain(info):
    s = info["strings"]
    print("Config sections:")
    for p in info["sections"]:
        print(f"  {s.text(p.label_key):<16} #{p.dom_id:<16} {p.where}")
    print("Tabs (the first opens):")
    for p in info["row"]:
        if p is CONFIG_TAB:
            print(f"  {s.text('tab.config'):<16} #Config")
        else:
            print(f"  {s.text(p.label_key):<16} #{p.dom_id:<16} {p.where}")
    if info["off"]:
        print("Off: " + ", ".join(info["off"]))
    if s.skipped:
        print("Text for features that are off, not used: " + ", ".join(s.skipped))
    print("Board text:")
    for key, src in s.source.items():
        if "," in src:
            print(f"  {key:<16} {s.text(key)[:60]!r}  ({src.split(', ', 1)[1]})")
    print(f"{len(s.table)} strings, from {len(info['used'])} files")


def main():
    ap = argparse.ArgumentParser(description="Builds a board's web page from its page.yaml.")
    ap.add_argument("page", help="the board's page.yaml")
    ap.add_argument("-o", "--output", help="the index.html to write")
    ap.add_argument("--libraries", help="comma separated libraries the firmware links; warns about a feature whose library is not among them")
    ap.add_argument("--depfile", help="write a make style depfile of every file read")
    ap.add_argument("--explain", action="store_true", help="say what went into the page and where it came from")
    args = ap.parse_args()

    libraries = set(args.libraries.split(",")) if args.libraries is not None else None
    try:
        page, info = build(args.page, libraries)
    except PageError as e:
        sys.exit(f"build_web_page: {e}")

    if args.explain:
        explain(info)
    if args.output:
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(page)
        if args.depfile:
            with open(args.depfile, "w", encoding="utf-8") as f:
                f.write(args.output + ": " + " ".join(sorted(info["used"])) + "\n")
        print(f"build_web_page: {args.page} -> {args.output}, {len(page)} bytes")


if __name__ == "__main__":
    main()
