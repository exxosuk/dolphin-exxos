#!/usr/bin/env python3
"""Strip editor cruft from SVGs. Lossless as far as rendering goes."""
import re, sys, os

RE_COMMENT  = re.compile(rb"<!--.*?-->", re.S)
RE_METADATA = re.compile(rb"<metadata\b.*?</metadata>", re.S)
RE_SODIPODI = re.compile(rb"<sodipodi:namedview\b.*?(?:/>|</sodipodi:namedview>)", re.S)
RE_EDITATTR = re.compile(rb'\s(?:inkscape|sodipodi):[\w-]+\s*=\s*"[^"]*"')
RE_WS       = re.compile(rb">\s+<")

def trim(data):
    for r in (RE_COMMENT, RE_METADATA, RE_SODIPODI):
        data = r.sub(b"", data)
    data = RE_EDITATTR.sub(b"", data)
    data = RE_WS.sub(b"><", data)
    return data

if __name__ == "__main__":
    before = after = 0
    for path in sys.argv[1:]:
        try:
            d = open(path, "rb").read()
        except OSError:
            continue
        before += len(d)
        n = trim(d)
        after += len(n)
        if len(n) < len(d):
            open(path, "wb").write(n)
    print(f"{before/1048576:.1f} MB -> {after/1048576:.1f} MB")
