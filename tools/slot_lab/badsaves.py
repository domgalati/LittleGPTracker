#!/usr/bin/env python3
"""Variants of a project's lgptsav.dat for the loader's XML/LZ decision.

Fork only (upstream crashes on several), so they aren't fixtures: `make play`
writes them to a scratch directory and loads each under ASan. Prints one
"<name> <expect>" line per variant: `same` loads as the source project,
`refused` falls back to defaults with an error in the log.

usage: badsaves.py <source project> <lz project> <out dir>
"""
import os
import shutil
import struct
import sys


def main():
    src, lz, out = sys.argv[1:4]
    with open(os.path.join(src, "lgptsav.dat"), "rb") as f:
        xml = f.read()
    with open(os.path.join(lz, "lgptsav.dat"), "rb") as f:
        packed = f.read()
    bare = xml.rstrip(b"\r\n")
    if bare == xml or not bare.endswith(b">"):
        sys.exit("badsaves: %s doesn't end with '>' and a newline" % src)
    variants = [
        # Well-formed, ending right after the last '>' (Parse() returns 0)
        ("nonl", "same", bare),
        ("bom-nonl", "same", b"\xef\xbb\xbf" + bare),
        ("crlf-nonl", "same", xml.replace(b"\n", b"\r\n").rstrip(b"\r\n")),
        # Neither XML nor LZ
        ("empty", "refused", b""),
        ("short", "refused", b"<a"),
        ("truncated", "refused", xml[: len(xml) // 2]),
        ("badtag", "refused", xml.replace(b"</SONG>", b"</SONGX>", 1)),
        ("lz-huge", "refused", struct.pack("<i", 0x7FFFFFFF) + packed[4:]),
        ("lz-negative", "refused", struct.pack("<i", -1) + packed[4:]),
    ]
    for name, expect, data in variants:
        d = os.path.join(out, name)
        shutil.rmtree(d, ignore_errors=True)
        shutil.copytree(src, d)
        with open(os.path.join(d, "lgptsav.dat"), "wb") as f:
            f.write(data)
        print(name, expect)


if __name__ == "__main__":
    main()
