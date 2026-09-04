#!/usr/bin/env python3
"""Strip editor cruft from SVGs. Lossless as far as rendering goes."""
import re, sys, os
import xml.etree.ElementTree as ET

RE_COMMENT  = re.compile(rb"<!--.*?-->", re.S)
RE_METADATA = re.compile(rb"<metadata\b.*?</metadata>", re.S)
# A namedview may be self-closing OR carry children (inkscape:page, and others).
# The old pattern was `.*?(?:/>|</sodipodi:namedview>)`, which on the second kind
# stopped at the FIRST `/>` -- a child's own self-closing tag -- and left
# </sodipodi:namedview> behind with nothing opening it. That is a mismatched tag,
# Qt's SVG reader refuses the whole file, and the icon renders as nothing at all.
# Six icons in the theme shipped broken that way, including preferences-system
# and go-previous, so System Settings and Dolphin's toolbar had blank icons.
# The alternation is ordered: [^>]* cannot cross a '>', so it only matches a
# genuinely self-closing tag, and anything else falls through to the closing tag.
RE_SODIPODI = re.compile(rb"<sodipodi:namedview\b(?:[^>]*/>|.*?</sodipodi:namedview>)", re.S)
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
        # Never write a file the trim has broken. Cheaper than trusting the
        # regexes, and it is the check that would have caught the above before
        # it reached two machines.
        if len(n) < len(d):
            try:
                ET.fromstring(n)
            except ET.ParseError as e:
                print(f"  ! {os.path.basename(path)}: trim would break it ({e}) -- left alone",
                      file=sys.stderr)
                after += len(d) - len(n)
                continue
            open(path, "wb").write(n)
    print(f"{before/1048576:.1f} MB -> {after/1048576:.1f} MB")
