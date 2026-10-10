# gui_lab: drive lgpt.x64 on a private X display with synthetic key presses
# and save screenshots, so the agent can check what a screen shows. Not part
# of the default build. Needs Xvfb, libX11 and libXtst, and Pillow with XCB.
#
# Run the app on its own display, silent, from a run directory as CLAUDE.md
# describes (config.xml and mapping.xml next to the binary; a last_project
# file holding a project path auto-loads it):
#   Xvfb :77 -screen 0 800x600x24 -nolisten tcp &
#   DISPLAY=:77 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 ./lgpt.x64 &
#   python3 -I drive.py :77 <outdir> "<steps>"
# SDL's dummy audio driver still runs the audio thread, so the song plays
# (silently) and the mixer lock is exercised for real. For an ASan app build,
# pass sanitizer flags on the make command line in a scratch worktree:
#   make PLATFORM=X64 OPT_FLAGS="-g -O1 -fsanitize=address,undefined
#     -fno-sanitize=signed-integer-overflow" LDFLAGS="-fsanitize=address,undefined"
# UBSan reports go to the app's stderr.
#
# Steps, separated by whitespace. Button names are the app's (mapping.xml:
# A=s, B=a, L=q, R=w, START=space, arrows):
#   key:NAME            press and release NAME
#   hold:M+N:X,Y        hold M and N, press X then Y, release M and N
#   shot:NAME           save <outdir>/NAME.png (the 640x480 window, which
#                       sits centred on the 800x600 display at SCREENMULT=2)
#   wait:SECONDS
# Example, song screen to the instrument screen's type field:
#   "hold:R:RIGHT hold:R:RIGHT hold:R:RIGHT key:UP shot:type"
import ctypes
import sys
import time

from PIL import ImageGrab

disp, outdir, script = sys.argv[1], sys.argv[2], sys.argv[3]
X11 = ctypes.cdll.LoadLibrary("libX11.so.6")
Xtst = ctypes.cdll.LoadLibrary("libXtst.so.6")
X11.XOpenDisplay.restype = ctypes.c_void_p
X11.XOpenDisplay.argtypes = [ctypes.c_char_p]
X11.XStringToKeysym.restype = ctypes.c_ulong
X11.XStringToKeysym.argtypes = [ctypes.c_char_p]
X11.XKeysymToKeycode.restype = ctypes.c_ubyte
X11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
X11.XFlush.argtypes = [ctypes.c_void_p]
Xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
Xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]

dpy = X11.XOpenDisplay(disp.encode())
if not dpy:
    sys.exit("cannot open display " + disp)
# Pointer over the window, so it has the keyboard without a window manager
Xtst.XTestFakeMotionEvent(dpy, -1, 400, 300, 0)
X11.XFlush(dpy)

KEYS = {"A": "s", "B": "a", "L": "q", "R": "w", "START": "space",
        "UP": "Up", "DOWN": "Down", "LEFT": "Left", "RIGHT": "Right"}


# Short enough that the app's key repeat doesn't start
def key(name, down):
    kc = X11.XKeysymToKeycode(dpy, X11.XStringToKeysym(KEYS[name].encode()))
    Xtst.XTestFakeKeyEvent(dpy, kc, 1 if down else 0, 0)
    X11.XFlush(dpy)
    time.sleep(0.12)


for step in script.split():
    kind, _, arg = step.partition(":")
    if kind == "key":
        key(arg, True)
        key(arg, False)
    elif kind == "hold":
        mods, _, keys = arg.rpartition(":")
        mods = mods.split("+")
        for m in mods:
            key(m, True)
        for k in keys.split(","):
            key(k, True)
            key(k, False)
        for m in reversed(mods):
            key(m, False)
    elif kind == "shot":
        time.sleep(0.4)  # let the app redraw
        ImageGrab.grab(xdisplay=disp).crop((80, 60, 720, 540)).save("%s/%s.png" % (outdir, arg))
    elif kind == "wait":
        time.sleep(float(arg))
    else:
        sys.exit("bad step " + step)
