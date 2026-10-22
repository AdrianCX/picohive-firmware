#!/usr/bin/env python3
#
# Minifies and gzips a web page into a C header, see web.cmake:
#
#   web/pack_web_page.py BASE_DIR INPUT_FILE OUTPUT_FILE
#
# The array is named the way xxd -i names ./INPUT_FILE.gz, so sources written
# against the old xxd output keep working.
import gzip
import os
import re
import sys

import minify_html

# Blocks taken out of the plain HTML handling: scripts and styles are minified,
# pre and textarea keep their whitespace as it is.
BLOCK = re.compile(r"<(script|style|pre|textarea)\b.*?</\1\s*>", re.IGNORECASE | re.DOTALL)
COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
WHITESPACE = re.compile(r"\s+")


def minify_text(html):
    # Collapsing a run of whitespace to one space renders the same, unlike
    # dropping it, which glues inline elements together ("Skin:Original").
    return WHITESPACE.sub(" ", COMMENT.sub("", html))


# A / starts a regular expression, not a division, after one of these - or
# after nothing at all.
REGEX_AFTER = set("(,=:[!&|?{};+-*%<>~^")
REGEX_KEYWORDS = ("return", "typeof", "case", "do", "else", "in", "of", "new", "delete", "void", "throw")


def strip_js_comments(js):
    """The script with its comments taken out, and nothing else changed.

    minify-html squeezes the whitespace out of a script but leaves its comments
    in, and on a page written to be read they were a quarter of the gzipped
    size. Stripping them needs the strings, template literals and regular
    expressions recognised, so a // inside "http://..." or a regex is left
    alone; a line comment's newline is kept, so semicolon insertion reads the
    script the same way.
    """
    out = []
    i = 0
    n = len(js)
    last = ""                       # the last significant character written
    word = ""                       # the identifier or keyword ending at it
    braces = []                     # open template ${ } nesting: depth when each began
    depth = 0

    while i < n:
        c = js[i]
        nxt = js[i + 1] if i + 1 < n else ""

        if c == "/" and nxt == "/":
            j = js.find("\n", i)
            i = n if j < 0 else j
            continue

        if c == "/" and nxt == "*":
            j = js.find("*/", i + 2)
            body = js[i:n if j < 0 else j + 2]
            out.append("\n" if "\n" in body else " ")
            i = n if j < 0 else j + 2
            continue

        if c in "'\"":
            j = i + 1
            while j < n and js[j] != c:
                j += 2 if js[j] == "\\" else 1
            out.append(js[i:j + 1])
            i = j + 1
            last, word = c, ""
            continue

        if c == "`" or (c == "}" and braces and braces[-1] == depth):
            # A template literal, or the rest of one after a ${ } closes.
            if c == "}":
                braces.pop()
            j = i + 1
            while j < n and js[j] != "`":
                if js[j] == "\\":
                    j += 2
                    continue
                if js[j] == "$" and j + 1 < n and js[j + 1] == "{":
                    braces.append(depth)
                    j += 2
                    break
                j += 1
            else:
                j += 1
            out.append(js[i:j])
            i = j
            last, word = "`", ""
            continue

        if c == "/" and (last == "" or last in REGEX_AFTER or word in REGEX_KEYWORDS):
            j = i + 1
            klass = False
            while j < n:
                if js[j] == "\\":
                    j += 2
                    continue
                if js[j] == "[":
                    klass = True
                elif js[j] == "]":
                    klass = False
                elif js[j] == "/" and not klass:
                    break
                elif js[j] == "\n":
                    break
                j += 1
            j += 1
            while j < n and (js[j].isalnum()):
                j += 1
            out.append(js[i:j])
            i = j
            last, word = "/", ""
            continue

        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1

        out.append(c)

        if not c.isspace():
            if c.isalnum() or c in "_$":
                word = word + c if (last.isalnum() or last in "_$") else c
            else:
                word = ""
            last = c

        i += 1

    return "".join(out)


SCRIPT = re.compile(r"^(<script\b[^>]*>)(.*)(</script\s*>)$", re.IGNORECASE | re.DOTALL)


def minify_block(match):
    block = match.group(0)
    if match.group(1).lower() == "script":
        parts = SCRIPT.match(block)
        if parts:
            block = parts.group(1) + strip_js_comments(parts.group(2)) + parts.group(3)
    if match.group(1).lower() in ("script", "style"):
        return minify_html.minify(block, minify_js=True, minify_css=True)
    return block


def minify(page):
    # minify-html is only trusted with the inline JS and CSS: on the HTML itself
    # it drops the whitespace between elements and the opening <html><head>.
    out = []
    pos = 0
    for match in BLOCK.finditer(page):
        out.append(minify_text(page[pos:match.start()]))
        out.append(minify_block(match))
        pos = match.end()
    out.append(minify_text(page[pos:]))
    return "".join(out)


def main():
    if len(sys.argv) != 4:
        sys.exit(f"usage: {sys.argv[0]} BASE_DIR INPUT_FILE OUTPUT_FILE")

    base_dir, input_file, output_file = sys.argv[1:]

    with open(os.path.join(base_dir, input_file), encoding="utf-8") as f:
        page = f.read()

    minified = minify(page).encode("utf-8")

    # mtime 0 so the same page always gives the same bytes, and the same build id.
    packed = gzip.compress(minified, compresslevel=9, mtime=0)

    name = re.sub(r"[^0-9A-Za-z]", "_", f"./{input_file}.gz")

    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(packed), 12):
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in packed[i:i + 12]) + ",")
    lines.append("};")
    lines.append(f"unsigned int {name}_len = {len(packed)};")

    with open(output_file, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"pack_web_page: {input_file} {len(page)} bytes, minified {len(minified)}, gzipped {len(packed)}")


if __name__ == "__main__":
    main()
