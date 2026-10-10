#!/usr/bin/env python3
"""Derive slot_lab's hand-made fixtures from fixtures/up-basic.

Text edits on upstream's TinyXML output, so each file differs from up-basic
only where fixtures/README.md says. Run through `make fixtures`.
"""
import os
import re
import shutil
import sys


def edit(pattern, repl, text, count=1, flags=0):
    """re.subn that fails unless it matched exactly `count` times."""
    new, n = re.subn(pattern, repl, text, flags=flags)
    if n != count:
        sys.exit("handmade: %r matched %d times, expected %d" % (pattern, n, count))
    return new


def write(base, name, text):
    d = os.path.join(base, name)
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(os.path.join(base, "up-basic", "samples"), os.path.join(d, "samples"))
    with open(os.path.join(d, "lgptsav.dat"), "w", newline="") as f:
        f.write(text)


def set_instrument_bytes(text, changes):
    """Set phrase instrument bytes; changes maps (phrase, row) to (old, new)."""
    m = re.search(r"<INSTRUMENTS>\s*<DATA>([0-9A-F]{128})</DATA>", text)
    if not m:
        sys.exit("handmade: first INSTRUMENTS chunk isn't a plain hex chunk")
    hexs = list(m.group(1))
    for (phrase, row), (old, new) in changes.items():
        k = 2 * (16 * phrase + row)
        if k >= len(hexs):
            sys.exit("handmade: phrase %02X row %d isn't in the first chunk" % (phrase, row))
        if "".join(hexs[k:k + 2]) != old:
            sys.exit("handmade: phrase %02X row %d isn't %s" % (phrase, row, old))
        hexs[k:k + 2] = new
    return text[:m.start(1)] + "".join(hexs) + text[m.end(1):]


def main():
    base = sys.argv[1]
    with open(os.path.join(base, "up-basic", "lgptsav.dat"), newline="") as f:
        basic = f.read()

    # Older than 2014: no TYPE, MIDI entries 81-8F missing (85 and 8F are
    # referenced), filter mode saved under its old name
    t = edit(r'(<INSTRUMENT ID="[0-9A-F]{2}") TYPE="(?:Sample|Midi)"', r"\1", basic, count=21)
    t = edit(r'\n\s*<INSTRUMENT ID="8[1-9A-F]">.*?</INSTRUMENT>', "", t, count=15, flags=re.S)
    for slot, value in (("00", "none"), ("01", "scream")):
        t = edit(r'(<INSTRUMENT ID="%s">(?:(?!</INSTRUMENT>).)*?)'
                 r'<PARAM NAME="filter mode" VALUE="original" />' % slot,
                 r'\1<PARAM NAME="filter dist" VALUE="%s" />' % value, t, flags=re.S)
    write(base, "hand-notype", t)

    # VERSION below 38: Sample volume and crushdrive swap when crush isn't 16
    write(base, "hand-v037", edit(r'<PROJECT VERSION="1">', '<PROJECT VERSION="0.37">', basic))

    # No VERSION at all: version 32, so table ratio 2 and the swap above
    write(base, "hand-noversion", edit(r'<PROJECT VERSION="1">', "<PROJECT>", basic))

    # A Sample entry naming a file that isn't in samples/
    write(base, "hand-missing-sample", edit(
        r'(<INSTRUMENT ID="7F" TYPE="Sample">\s*)<PARAM NAME="sample" VALUE="c_tone.wav" />',
        r'\1<PARAM NAME="sample" VALUE="gone.wav" />', basic))

    # Phrase instrument bytes past upstream's last slot (8F)
    write(base, "hand-phrase90", set_instrument_bytes(basic, {
        (0x02, 0): ("03", "90"),
        (0x02, 2): ("FF", "A0"),
        (0x02, 8): ("7E", "FE"),
    }))


if __name__ == "__main__":
    main()
