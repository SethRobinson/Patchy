# Drives a headless gnome-shell: pointer through org.gnome.Mutter.RemoteDesktop, captures
# through org.gnome.Shell.Screenshot (needs --unsafe-mode). Commands on stdin:
#   move X Y     absolute layout pixels (a big relative jump to the corner, then the offset)
#   rel DX DY    relative motion only (for drags: move re-anchors at the corner first)
#   press | release | click
#   wheel N      N discrete vertical wheel steps (positive scrolls down)
#   kdown SYM | kup SYM   X keysym number (0x.. accepted); hold modifiers with kdown, release with kup
#   sleep MS
#   shot NAME    writes <out>/NAME.png
import os, sys, time
SCREEN_H = int(os.environ.get("MON", "1400x900").split("x")[1])
import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

out = sys.argv[1]
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
rd = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None, "org.gnome.Mutter.RemoteDesktop",
                            "/org/gnome/Mutter/RemoteDesktop", "org.gnome.Mutter.RemoteDesktop", None)
session_path = rd.call_sync("CreateSession", None, Gio.DBusCallFlags.NONE, -1, None).unpack()[0]
session = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None, "org.gnome.Mutter.RemoteDesktop",
                                 session_path, "org.gnome.Mutter.RemoteDesktop.Session", None)
session.call_sync("Start", None, Gio.DBusCallFlags.NONE, -1, None)
shot = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None, "org.gnome.Shell.Screenshot",
                              "/org/gnome/Shell/Screenshot", "org.gnome.Shell.Screenshot", None)
cur = [0.0, 0.0]

def rel(dx, dy):
    session.call_sync("NotifyPointerMotionRelative", GLib.Variant("(dd)", (float(dx), float(dy))),
                      Gio.DBusCallFlags.NONE, -1, None)

def button(state):
    session.call_sync("NotifyPointerButton", GLib.Variant("(ib)", (272, state)), Gio.DBusCallFlags.NONE, -1, None)

def keysym(sym, state):
    # X keysym numbers: Control_L 0xffe3, Alt_L 0xffe9, Shift_L 0xffe1, Escape 0xff1b, Return 0xff0d, 'i' 0x69.
    session.call_sync("NotifyKeyboardKeysym", GLib.Variant("(ub)", (sym, state)), Gio.DBusCallFlags.NONE, -1, None)

for line in sys.stdin:
    parts = line.split()
    if not parts:
        continue
    if parts[0] == "move":
        x, y = float(parts[1]), float(parts[2])
        rel(-5000, 5000); time.sleep(0.05)  # bottom-left: top-left is the Activities hot corner
        rel(x, y - (SCREEN_H - 1)); cur = [x, y]
    elif parts[0] == "rel":
        rel(float(parts[1]), float(parts[2])); cur = [cur[0] + float(parts[1]), cur[1] + float(parts[2])]
    elif parts[0] == "press":
        button(True)
    elif parts[0] == "release":
        button(False)
    elif parts[0] == "click":
        button(True); time.sleep(0.06); button(False)
    elif parts[0] == "wheel":
        session.call_sync("NotifyPointerAxisDiscrete", GLib.Variant("(ui)", (0, int(parts[1]))),
                          Gio.DBusCallFlags.NONE, -1, None)
    elif parts[0] == "kdown":
        keysym(int(parts[1], 0), True)
    elif parts[0] == "kup":
        keysym(int(parts[1], 0), False)
    elif parts[0] == "sleep":
        time.sleep(int(parts[1]) / 1000.0)
    elif parts[0] == "shot":
        path = f"{out}/{parts[1]}.png"
        ok, used = shot.call_sync("Screenshot", GLib.Variant("(bbs)", (False, False, path)),
                                  Gio.DBusCallFlags.NONE, -1, None).unpack()
        print("shot:", ok, used, flush=True)
    time.sleep(0.02)
session.call_sync("Stop", None, Gio.DBusCallFlags.NONE, -1, None)
