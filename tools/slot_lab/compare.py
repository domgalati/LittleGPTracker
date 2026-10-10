#!/usr/bin/env python3
"""Compare the fork's dump or resave of a fixture with upstream's.

  compare.py dump <upstream.dump> <fork.dump>
  compare.py xml <upstream.resave.xml> <fork.resave.xml>

Prints a unified diff and exits 1 if they differ. Run by `make compare`.

Upstream fills every slot and the fork starts them empty, so upstream's dump
is first rewritten into the fork's form (design doc 6.1):
- "slots 90" becomes "slots FF".
- A sampleless Sample at defaults is what upstream has where the fork has an
  empty slot: dropped, or "Ixx --" if a phrase plays it.
- "out of range Ixx" (phrase bytes 90-FE) becomes "Ixx --".

Resaves are compared with top-level nodes sorted by name (their order varies
between saves) and without INSTRUMENTBANK's LAYOUT attribute.
"""
import difflib
import re
import sys
import xml.etree.ElementTree as ET


def lines_of(path):
    with open(path) as f:
        return [l.rstrip('\n') for l in f if not l.startswith('#')]


def upstream_dump_as_fork(lines):
    out = []
    skip = False
    for line in lines:
        if line.startswith('    '):
            if not skip:
                out.append(line)
            continue
        skip = False
        if line.startswith('slots '):
            out.append('slots FF')
            continue
        m = re.match(r'I([0-9A-F]{2}) Sample empty, defaults(, \d+ rows in phrases.*)?$', line)
        if m:
            skip = True
            if m.group(2):
                out.append('I%s --%s' % (m.group(1), m.group(2)))
            continue
        m = re.match(r'out of range I([0-9A-F]{2})(, .*)$', line)
        if m:
            out.append('I%s --%s' % (m.group(1), m.group(2)))
            continue
        out.append(line)
    return out


def canonical_xml(path):
    root = ET.parse(path).getroot()
    for bank in root.iter('INSTRUMENTBANK'):
        bank.attrib.pop('LAYOUT', None)
    root[:] = sorted(list(root), key=lambda e: e.tag)
    ET.indent(root)
    return ET.tostring(root, encoding='unicode').splitlines()


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ('dump', 'xml'):
        sys.stderr.write(__doc__)
        return 2
    kind, up, fork = sys.argv[1:]
    if kind == 'dump':
        a, b = upstream_dump_as_fork(lines_of(up)), lines_of(fork)
    else:
        a, b = canonical_xml(up), canonical_xml(fork)
    diff = list(difflib.unified_diff(a, b, up, fork, lineterm=''))
    for line in diff:
        print(line)
    return 1 if diff else 0


if __name__ == '__main__':
    sys.exit(main())
