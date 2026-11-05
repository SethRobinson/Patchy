# Captures KWin's screen through org.kde.KWin.ScreenShot2 (inside the rig container) as raw
# pixels; prints "width height stride format" for the host to convert.
#   kwin-shot.py <out.raw> [cursor]
import os, sys, threading
import dbus

out = sys.argv[1]
include_cursor = len(sys.argv) > 2 and sys.argv[2] == "cursor"
bus = dbus.SessionBus()
shot = dbus.Interface(bus.get_object("org.kde.KWin", "/org/kde/KWin/ScreenShot2"), "org.kde.KWin.ScreenShot2")
read_fd, write_fd = os.pipe()
chunks = []


def drain():
    with os.fdopen(read_fd, "rb") as pipe:
        while True:
            data = pipe.read(1 << 20)
            if not data:
                break
            chunks.append(data)


reader = threading.Thread(target=drain)
reader.start()
options = dbus.Dictionary({"include-cursor": dbus.Boolean(include_cursor), "native-resolution": dbus.Boolean(True)},
                          signature="sv")
result = shot.CaptureWorkspace(options, dbus.types.UnixFd(write_fd))
os.close(write_fd)
reader.join()
with open(out, "wb") as f:
    f.write(b"".join(chunks))
print(int(result["width"]), int(result["height"]), int(result["stride"]), int(result["format"]))
