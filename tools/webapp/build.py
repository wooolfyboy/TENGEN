#!/usr/bin/env python3
"""Builds the tablet app into one offline HTML file, gzipped into src/web/tablet_page.h.
Sources: tools/webapp/src (index.html with /*@INLINE path*/ markers). One page serves both
languages: i18n/ru.json is embedded, the language follows the device (st.lang).

    python3 tools/webapp/build.py                    # build src/web/tablet_page.h
    python3 tools/webapp/build.py --check            # CI: page matches the sources
    python3 tools/webapp/build.py --html out.html --only-html [--lang ru]   # HTML only (start language, for testing)
"""
import gzip, hashlib, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src")
WEB = os.path.join(HERE, "..", "..", "src", "web")
LANGS = ("en", "ru")
DICT_DIR = os.path.join(HERE, "i18n")


OUT = os.path.join(WEB, "tablet_page.h")
FW = os.path.join(HERE, "..", "..", "src")


def c_array(text, name):
    """Numbers from `static const ... NAME[] = { ... };` as a flat list."""
    m = re.search(r"static const [^=;]*\b%s\b[^=;]*=\s*\{(.*?)\};" % re.escape(name), text, re.S)
    assert m, name
    return [int(x) for x in re.findall(r"-?\d+", re.sub(r'"[^"]*"', "", m.group(1)))]


def data_js():
    """Sky data (stars, constellation lines, land outlines) read straight from the firmware header so they never drift apart."""
    with open(os.path.join(FW, "gallery", "astrodata.h"), encoding="utf-8") as fh:
        a = fh.read()
    con = re.search(r"CON_NAMES\[\]\s*=\s*\{(.*?)\};", a, re.S).group(1)
    cons = [[int(r), int(d), ab, ru] for r, d, ab, ru in re.findall(r'\{\s*(-?\d+),\s*(-?\d+),\s*"([^"]*)",\s*"([^"]*)"\s*\}', con)]
    names = re.search(r"STAR_NAMES\[\]\s*=\s*\{(.*?)\};", a, re.S).group(1)
    snames = [[int(i), n] for i, n in re.findall(r'\{\s*(\d+),\s*"([^"]*)"\s*\}', names)]
    d = {
        "stars": c_array(a, "STARS"),            # triples: ra*100, dec*100, (mag+2)*10
        "starNames": snames,
        "cl": c_array(a, "CL_PTS"),              # pairs ra*100, dec*100 (uint16 -> restore the sign)
        "clStart": c_array(a, "CL_START"),
        "con": cons,                             # [ra*100, dec*100, "Ori", "name"]
        "land": c_array(a, "LAND_PTS"),          # pairs (as in gallery_sky.cpp)
        "landStart": c_array(a, "LAND_START"),
    }
    return "T.DATA=" + json.dumps(d, ensure_ascii=False, separators=(",", ":")) + ";"


def _strip_js_comments(code):
    """Lexer-aware stripper for // and /*...*/ comments (preserves strings, template literals, and regex literals)."""
    out = []
    i, n = 0, len(code)
    prev_tok = ""                                  # last non-whitespace char/word to disambiguate '/' regex vs division
    while i < n:
        c = code[i]
        if c in ("'", '"'):
            q = c
            out.append(c); i += 1
            while i < n:
                ch = code[i]; out.append(ch); i += 1
                if ch == "\\" and i < n:
                    out.append(code[i]); i += 1
                elif ch == q:
                    break
            prev_tok = q
        elif c == "`":
            out.append(c); i += 1
            while i < n:
                ch = code[i]
                if ch == "\\" and i + 1 < n:
                    out.append(ch); out.append(code[i + 1]); i += 2
                elif ch == "`":
                    out.append(ch); i += 1; break
                else:
                    out.append(ch); i += 1
            prev_tok = "`"
        elif c == "/" and i + 1 < n and code[i + 1] == "/":
            i += 2
            while i < n and code[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and code[i + 1] == "*":
            if i + 2 < n and code[i + 2] == "@":          # keep /*@LANG*/, /*@DICT*/, /*@DATA*/ markers
                end = code.find("*/", i + 3)
                if end >= 0:
                    out.append(code[i:end + 2]); i = end + 2; continue
            i += 2
            while i + 1 < n and not (code[i] == "*" and code[i + 1] == "/"):
                if code[i] == "\n":
                    out.append("\n")
                i += 1
            i += 2
        elif c == "/" and prev_tok in ("", "(", ",", "=", ":", "[", "!", "&", "|", "?", "{", "}", ";", "+", "-", "*", "%", "<", ">", "~", "^", "return", "typeof", "case", "in", "of", "void", "delete", "throw", "new"):
            # regex literal /.../flags
            out.append(c); i += 1
            in_cls = False
            while i < n:
                ch = code[i]
                if ch == "\n":
                    break
                out.append(ch); i += 1
                if ch == "\\" and i < n:
                    out.append(code[i]); i += 1
                elif ch == "[":
                    in_cls = True
                elif ch == "]":
                    in_cls = False
                elif ch == "/" and not in_cls:
                    break
            prev_tok = "/"
        else:
            out.append(c)
            if not c.isspace():
                if c.isalnum() or c in ("_", "$"):
                    if prev_tok and (prev_tok[-1].isalnum() or prev_tok[-1] in ("_", "$")):
                        prev_tok += c
                    else:
                        prev_tok = c
                else:
                    prev_tok = c
            i += 1
    return "".join(out)


def slim(code, kind):
    """Minify comments and redundant whitespace while keeping code 100% semantically identical."""
    if kind == "css":
        code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
        lines = [l.strip() for l in code.splitlines() if l.strip()]
        css = " ".join(lines)
        css = re.sub(r"\s*([{};:,>])\s*", r"\1", css)
        return css.replace(";}", "}")
    code = _strip_js_comments(code)
    out = []
    for line in code.splitlines():
        t = line.strip()
        if t:
            out.append(t)
    return "\n".join(out)


def build(lang="en"):
    with open(os.path.join(SRC, "index.html"), encoding="utf-8") as fh:
        html = fh.read()
    def inline(m):
        path = m.group(1)
        with open(os.path.join(SRC, path), encoding="utf-8") as fh:
            code = fh.read()
        return slim(code, "css" if path.endswith(".css") else "js")
    html = re.sub(r"/\*@INLINE ([^*]+?)\*/", inline, html)
    html = html.replace("/*@DATA*/", data_js())
    with open(os.path.join(HERE, "..", "..", "VERSION"), encoding="utf-8") as fh:
        ver = fh.read().strip()
    html = html.replace("@@VERSION@@", ver)                      # TENGEN version (VERSION file)
    # dictionary is always embedded; start language is 'en' (the page switches to the device language at once)
    assert "/*@LANG*/'en'" in html and "/*@DICT*/null" in html
    with open(os.path.join(DICT_DIR, "ru.json"), encoding="utf-8") as fh:
        d = json.dumps(json.load(fh), ensure_ascii=False, separators=(",", ":"))
    html = html.replace("/*@LANG*/'en'", "'%s'" % lang).replace("/*@DICT*/null", d)
    assert "@INLINE" not in html
    assert not re.search(r'(src|href)="https?://', html), "внешние ссылки запрещены: страница должна работать без интернета"
    return html


def header(html):
    gz = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)   # mtime=0: identical output on rebuild
    digest = hashlib.sha256(html.encode("utf-8")).hexdigest()
    lines = ["// АВТОМАТИЧЕСКИ СГЕНЕРИРОВАНО tools/webapp/build.py из tools/webapp/src — не править руками.",
             "// Приложение для планшета (один HTML, gzip). Отдаётся по /tablet.",
             "// html-sha256: " + digest,
             "#pragma once", "#include <Arduino.h>", "",
             "static const uint8_t TABLET_GZ[] PROGMEM = {"]
    for i in range(0, len(gz), 20):
        lines.append("  " + ",".join("0x%02x" % b for b in gz[i:i + 20]) + ",")
    lines += ["};", "static const size_t TABLET_GZ_LEN = %d;" % len(gz),
              "// версия страницы (sha256 несжатого HTML): по ней приложение планшета понимает, что пора обновиться",
              'static const char TABLET_HTML_SHA[] = "%s";' % digest, ""]
    return "\n".join(lines), digest, len(gz)


def main():
    arg = lambda k: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else None
    langs = [arg("--lang")] if arg("--lang") else list(LANGS)
    assert all(l in LANGS for l in langs), "язык: en или ru"
    if "--html" in sys.argv:
        html = build(langs[0])
        out = arg("--html")
        open(out, "w", encoding="utf-8").write(html)
        # page version next to it: the app compares it with TENGEN (/api/pagever)
        open(os.path.join(os.path.dirname(os.path.abspath(out)), "pagever.txt"), "w").write(hashlib.sha256(html.encode("utf-8")).hexdigest())
        if "--only-html" in sys.argv:                  # dev: leave the firmware alone
            print("tablet[%s]: %d КБ HTML" % (langs[0], len(html) // 1024)); return
    html = build("en")
    data, digest, gzlen = header(html)
    if "--check" in sys.argv:                          # CI: sources and firmware must match
        # compare the HTML hash, not gzip bytes: zlib versions compress slightly differently
        cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        m = re.search(r"html-sha256: ([0-9a-f]{64})", cur)
        if not m or m.group(1) != digest: sys.exit("устарел tablet_page.h: запусти python3 tools/webapp/build.py")
        print("tablet_page.h актуален"); return
    open(OUT, "w", encoding="utf-8").write(data)
    print("tablet: %d КБ HTML -> %d КБ gzip -> %s" % (len(html) // 1024, gzlen // 1024, os.path.relpath(OUT)))

if __name__ == "__main__":
    main()
