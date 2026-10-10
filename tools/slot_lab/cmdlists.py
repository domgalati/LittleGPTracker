# cmdlists: check that each command list the command popup reads matches the
# switch it describes (design doc, sections 5.1 and 8). Usage:
#   python3 -I cmdlists.py <repo>/sources
# Prints one line per list and exits 1 if any list and its code differ.
#
# A list is the I_CMD_* names in an array definition up to I_CMD_NONE. Its
# code is the case labels of the named functions (from the signature to the
# first line that starts with '}'), plus any I_CMD_* the "also" functions
# compare with.
import re
import sys

src = sys.argv[1]

CHECKS = [
    # file, array, functions whose case labels count, functions that compare
    ("Application/Instruments/SampleInstrument.cpp", "SampleInstrument::Commands",
     ["SampleInstrument::ProcessCommand"], []),
    ("Application/Instruments/MidiInstrument.cpp", "MidiInstrument::Commands",
     ["MidiInstrument::ProcessCommand"], []),
    ("Application/Instruments/PigtailInstrument.cpp", "PigtailInstrument::Commands",
     ["PigtailInstrument::ProcessCommand"], []),
    ("Application/Player/Player.cpp", "Player::PhraseCommands",
     ["Player::ProcessChannelCommand"], ["Player::updatePhrasePos", "Player::getChannelHop"]),
    ("Application/Player/TablePlayback.cpp", "TablePlayback::TableCommands",
     ["TablePlayback::ProcessLocalCommand"], []),
]


def body(text, name):
    m = re.search(r"^\S[^\n]*\b" + re.escape(name) + r"\s*\(", text, re.M)
    if not m:
        sys.exit("cmdlists: no function %s" % name)
    end = re.search(r"^}", text[m.end():], re.M)
    return text[m.end():m.end() + end.start()]


def array(text, name):
    m = re.search(re.escape(name) + r"\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", text, re.S)
    if not m:
        sys.exit("cmdlists: no array %s" % name)
    names = re.findall(r"I_CMD_\w+", m.group(1))
    if not names or names[-1] != "I_CMD_NONE" or names.count("I_CMD_NONE") != 1:
        sys.exit("cmdlists: %s must end with I_CMD_NONE, once" % name)
    return names[:-1]


status = 0
for path, arr, cased, also in CHECKS:
    text = open(src + "/" + path).read()
    listed = array(text, arr)
    code = set()
    for f in cased:
        code |= set(re.findall(r"\bcase\s+(I_CMD_\w+)", body(text, f)))
    for f in also:
        code |= set(re.findall(r"==\s*(I_CMD_\w+)", body(text, f)))
    dup = sorted(set(c for c in listed if listed.count(c) > 1))
    if set(listed) == code and not dup:
        print("%s: %d commands, matches its code" % (arr, len(listed)))
    else:
        status = 1
        print("%s: DIFFERS from its code" % arr)
        for c in sorted(code - set(listed)):
            print("  handled but not listed: %s" % c)
        for c in sorted(set(listed) - code):
            print("  listed but not handled: %s" % c)
        for c in dup:
            print("  listed twice: %s" % c)
sys.exit(status)
