#!/usr/bin/env python3
"""Minimal UI-testing helper for driving the cockatrice client under Xvfb :99.

Usage:
    python3 uitest.py shot <outfile.png>
    python3 uitest.py click <x> <y>
    python3 uitest.py move <x> <y>
    python3 uitest.py key <keysym-name>          # e.g. Return, Escape, Tab
    python3 uitest.py type <text>
"""
import sys
import time

from Xlib import display, X
from Xlib.ext import xtest
from Xlib import XK
from PIL import Image

DISPLAY = ":99"


def get_display():
    return display.Display(DISPLAY)


def shot(outfile):
    d = get_display()
    root = d.screen().root
    geom = root.get_geometry()
    w, h = geom.width, geom.height
    raw = root.get_image(0, 0, w, h, X.ZPixmap, 0xFFFFFFFF)
    img = Image.frombytes("RGB", (w, h), raw.data, "raw", "BGRX")
    img.save(outfile)
    print(f"saved {outfile} ({w}x{h})")


def click(x, y, button=1):
    d = get_display()
    xtest.fake_input(d, X.MotionNotify, x=x, y=y)
    d.sync()
    time.sleep(0.05)
    xtest.fake_input(d, X.ButtonPress, button)
    d.sync()
    time.sleep(0.05)
    xtest.fake_input(d, X.ButtonRelease, button)
    d.sync()
    time.sleep(0.15)


def pixel(x, y):
    """Returns the (r, g, b) color at a single screen coordinate -- a much cheaper way for a
    scenario to tell two on-screen things apart (e.g. which hand card is which, by its distinct
    card-frame color) than saving a full shot() and re-opening it just to sample one point."""
    d = get_display()
    root = d.screen().root
    raw = root.get_image(x, y, 1, 1, X.ZPixmap, 0xFFFFFFFF)
    b, g, r = raw.data[0], raw.data[1], raw.data[2]
    return (r, g, b)


def move(x, y):
    d = get_display()
    xtest.fake_input(d, X.MotionNotify, x=x, y=y)
    d.sync()


def drag(x1, y1, x2, y2, button=1, steps=10):
    d = get_display()
    xtest.fake_input(d, X.MotionNotify, x=x1, y=y1)
    d.sync()
    time.sleep(0.1)
    xtest.fake_input(d, X.ButtonPress, button)
    d.sync()
    time.sleep(0.1)
    for i in range(1, steps + 1):
        ix = x1 + (x2 - x1) * i // steps
        iy = y1 + (y2 - y1) * i // steps
        xtest.fake_input(d, X.MotionNotify, x=ix, y=iy)
        d.sync()
        time.sleep(0.03)
    time.sleep(0.1)
    xtest.fake_input(d, X.ButtonRelease, button)
    d.sync()
    time.sleep(0.15)


def key(keysym_name):
    d = get_display()
    keysym = XK.string_to_keysym(keysym_name)
    keycode = d.keysym_to_keycode(keysym)
    xtest.fake_input(d, X.KeyPress, keycode)
    d.sync()
    time.sleep(0.05)
    xtest.fake_input(d, X.KeyRelease, keycode)
    d.sync()
    time.sleep(0.1)


_MODIFIER_KEYSYM_NAMES = {
    "ctrl": "Control_L",
    "control": "Control_L",
    "shift": "Shift_L",
    "alt": "Alt_L",
}


def key_combo(modifier_name, keysym_name):
    """Press modifier+key together, e.g. key_combo('ctrl', 'a')."""
    d = get_display()
    mod_keysym_name = _MODIFIER_KEYSYM_NAMES.get(modifier_name.lower(), modifier_name)
    mod_keysym = XK.string_to_keysym(mod_keysym_name)
    mod_keycode = d.keysym_to_keycode(mod_keysym)
    keysym = XK.string_to_keysym(keysym_name)
    keycode = d.keysym_to_keycode(keysym)
    xtest.fake_input(d, X.KeyPress, mod_keycode)
    d.sync()
    time.sleep(0.03)
    xtest.fake_input(d, X.KeyPress, keycode)
    d.sync()
    time.sleep(0.03)
    xtest.fake_input(d, X.KeyRelease, keycode)
    d.sync()
    xtest.fake_input(d, X.KeyRelease, mod_keycode)
    d.sync()
    time.sleep(0.1)


_PUNCT_KEYSYM_NAMES = {
    " ": "space",
    ".": "period",
    ",": "comma",
    "-": "minus",
    "_": "underscore",
    "/": "slash",
    ":": "colon",
    "@": "at",
    "'": "apostrophe",
    "\n": "Return",
}


def type_text(text):
    d = get_display()
    for ch in text:
        keysym = XK.string_to_keysym(ch)
        if keysym == 0 and ch in _PUNCT_KEYSYM_NAMES:
            keysym = XK.string_to_keysym(_PUNCT_KEYSYM_NAMES[ch])
        keycode = d.keysym_to_keycode(keysym)
        if keycode == 0:
            continue
        xtest.fake_input(d, X.KeyPress, keycode)
        d.sync()
        xtest.fake_input(d, X.KeyRelease, keycode)
        d.sync()
        time.sleep(0.02)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "shot":
        shot(sys.argv[2])
    elif cmd == "click":
        btn = int(sys.argv[4]) if len(sys.argv) > 4 else 1
        click(int(sys.argv[2]), int(sys.argv[3]), btn)
    elif cmd == "move":
        move(int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == "drag":
        drag(int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]))
    elif cmd == "key":
        key(sys.argv[2])
    elif cmd == "keycombo":
        key_combo(sys.argv[2], sys.argv[3])
    elif cmd == "type":
        type_text(" ".join(sys.argv[2:]))
    else:
        print("unknown command", cmd)
        sys.exit(1)
