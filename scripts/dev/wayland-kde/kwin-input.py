# Drives a virtual KWin through org_kde_kwin_fake_input (needs KWIN_WAYLAND_NO_PERMISSION_CHECKS=1).
# A raw Wayland client: no libraries. Commands on stdin, one per line:
#   move X Y | rel DX DY | press | release | rpress | rrelease | kcdown CODE | kcup CODE | sleep MS
# Everything held is released when the client disconnects, so keep one run per gesture.
import os, socket, struct, sys, time

path = sys.argv[1]
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(path)
buf = b""


def send(obj, opcode, payload=b""):
    size = 8 + len(payload)
    sock.sendall(struct.pack("<II", obj, (size << 16) | opcode) + payload)


def string(text):
    data = text.encode() + b"\0"
    pad = (4 - len(data) % 4) % 4
    return struct.pack("<I", len(data)) + data + b"\0" * pad


def fixed(value):
    return struct.pack("<i", int(round(value * 256)))


def read_events(until_obj):
    """Reads events until the wl_callback `until_obj` fires; returns registry globals."""
    global buf
    globals_ = {}
    while True:
        while len(buf) < 8:
            data = sock.recv(65536)
            if not data:
                sys.exit("kwin-input: compositor closed the connection")
            buf += data
        obj, word = struct.unpack("<II", buf[:8])
        size, opcode = word >> 16, word & 0xFFFF
        while len(buf) < size:
            buf += sock.recv(65536)
        body, buf = buf[8:size], buf[size:]
        if obj == 1 and opcode == 0:
            code = struct.unpack("<II", body[:8])
            sys.exit(f"kwin-input: protocol error on object {code[0]} code {code[1]}: {body[12:].split(bytes(1))[0].decode()}")
        if obj == 2 and opcode == 0:
            name, length = struct.unpack("<II", body[:8])
            iface = body[8:8 + length - 1].decode()
            off = 8 + length + (4 - length % 4) % 4
            version = struct.unpack("<I", body[off:off + 4])[0]
            globals_[iface] = (name, version)
        if obj == until_obj and opcode == 0:
            return globals_


send(1, 1, struct.pack("<I", 2))  # wl_display.get_registry -> 2
send(1, 0, struct.pack("<I", 3))  # wl_display.sync -> 3
globals_ = read_events(3)
name, version = globals_["org_kde_kwin_fake_input"]
version = min(version, 4)
print("fake_input version", version, file=sys.stderr, flush=True)
send(2, 0, struct.pack("<I", name) + string("org_kde_kwin_fake_input") + struct.pack("<II", version, 4))
send(4, 0, string("patchy-637-rig") + string("pointer lock check"))  # authenticate

next_id = 4
for line in sys.stdin:
    parts = line.split()
    if not parts:
        continue
    cmd = parts[0]
    if cmd == "move":
        send(4, 9, fixed(float(parts[1])) + fixed(float(parts[2])))  # pointer_motion_absolute
    elif cmd == "rel":
        send(4, 1, fixed(float(parts[1])) + fixed(float(parts[2])))  # pointer_motion
    elif cmd in ("press", "release", "rpress", "rrelease"):
        button = 273 if cmd.startswith("r") and cmd != "release" else 272
        send(4, 2, struct.pack("<II", button, 1 if cmd in ("press", "rpress") else 0))
    elif cmd in ("kcdown", "kcup"):
        send(4, 10, struct.pack("<II", int(parts[1], 0), 1 if cmd == "kcdown" else 0))  # keyboard_key
    elif cmd == "sleep":
        time.sleep(int(parts[1]) / 1000.0)
        continue
    elif cmd == "echo":
        print(" ".join(parts[1:]), flush=True)
        continue
    # A round trip after each command, so the order against other clients is kept. New ids
    # must come in sequence: the server rejects a gap.
    next_id += 1
    send(1, 0, struct.pack("<I", next_id))
    read_events(next_id)
    time.sleep(0.02)
sock.close()
