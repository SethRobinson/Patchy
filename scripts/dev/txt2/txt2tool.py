"""Txt2 probe tooling (docs/txt2.md): PSD section walker, EngineData tokenizer/AST/serializer (byte-exact),
Txt2 block extraction and replacement.

Usage:
  python txt2tool.py dump <file.psd> [maxdepth]        pretty-print the Txt2 tree
  python txt2tool.py roundtrip <file.psd>              parse + serialize, report byte equality
  python txt2tool.py extract <file.psd> <out.txt>      write the raw Txt2 body
  python txt2tool.py diff <a.psd> <b.psd>              structural diff of the two Txt2 trees
  python txt2tool.py variant <name> <in.psd> <out.psd> write a probe variant (see VARIANTS)
"""
import sys, os, struct, json

# ---------------------------------------------------------------- PSD sections

def read_psd(path):
    with open(path, 'rb') as f:
        return bytearray(f.read())

def psd_sections(b):
    """Return dict with offsets: header, color_mode, image_resources, layer_mask (start, len_off, len_size)."""
    assert b[:4] == b'8BPS'
    version = struct.unpack('>H', b[4:6])[0]
    psb = version == 2
    pos = 26
    cm_len = struct.unpack('>I', b[pos:pos+4])[0]; pos += 4 + cm_len
    ir_len = struct.unpack('>I', b[pos:pos+4])[0]; ir_start = pos + 4; pos += 4 + ir_len
    lm_len_off = pos
    if psb:
        lm_len = struct.unpack('>Q', b[pos:pos+8])[0]; lm_start = pos + 8
    else:
        lm_len = struct.unpack('>I', b[pos:pos+4])[0]; lm_start = pos + 4
    return {'psb': psb, 'ir_start': ir_start, 'ir_len': ir_len,
            'lm_len_off': lm_len_off, 'lm_start': lm_start, 'lm_len': lm_len}

def global_blocks(b, sec):
    """Yield (key, payload_start, payload_len, block_start, block_end) for the tagged blocks after the
    global layer mask info inside the layer-and-mask section."""
    psb = sec['psb']; pos = sec['lm_start']; end = sec['lm_start'] + sec['lm_len']
    # layer info
    if psb:
        li_len = struct.unpack('>Q', b[pos:pos+8])[0]; pos += 8 + li_len
    else:
        li_len = struct.unpack('>I', b[pos:pos+4])[0]; pos += 4 + li_len
    # global layer mask info
    glm_len = struct.unpack('>I', b[pos:pos+4])[0]; pos += 4 + glm_len
    out = []
    long_keys = {b'LMsk', b'Lr16', b'Lr32', b'Layr', b'Mt16', b'Mt32', b'Mtrn', b'Alph', b'FMsk', b'lnk2', b'FEid', b'FXid', b'PxSD', b'cinf'}
    while pos + 12 <= end:
        block_start = pos
        sig = b[pos:pos+4]
        if sig not in (b'8BIM', b'8B64'):
            break
        key = bytes(b[pos+4:pos+8])
        if psb and key in long_keys:
            length = struct.unpack('>Q', b[pos+8:pos+16])[0]; pstart = pos + 16
        else:
            length = struct.unpack('>I', b[pos+8:pos+12])[0]; pstart = pos + 12
        pos = pstart + length
        # Photoshop pads global blocks to 4 in its own files but Patchy-written and some PS files
        # pad differently; resynchronize on the next signature within 4 bytes.
        for extra in range(0, 4):
            if pos + extra + 8 <= end and b[pos+extra:pos+extra+4] in (b'8BIM', b'8B64'):
                pos += extra; break
        else:
            pos = pstart + ((length + 3) & ~3)
        out.append((key, pstart, length, block_start, pos))
    return out

def find_txt2(b):
    sec = psd_sections(b)
    for key, pstart, length, bstart, bend in global_blocks(b, sec):
        if key == b'Txt2':
            return sec, (pstart, length, bstart, bend)
    return sec, None

def replace_txt2(b, new_body):
    sec, blk = find_txt2(b)
    assert blk is not None, 'no Txt2 block'
    pstart, length, bstart, bend = blk
    padded = (len(new_body) + 3) & ~3
    new_block = b'8BIM' + b'Txt2' + struct.pack('>I', len(new_body)) + new_body + b'\0' * (padded - len(new_body))
    out = bytearray(b[:bstart]) + new_block + b[bend:]
    delta = len(new_block) - (bend - bstart)
    # fix the layer-and-mask length
    if sec['psb']:
        out[sec['lm_len_off']:sec['lm_len_off']+8] = struct.pack('>Q', sec['lm_len'] + delta)
    else:
        out[sec['lm_len_off']:sec['lm_len_off']+4] = struct.pack('>I', sec['lm_len'] + delta)
    return out

# ---------------------------------------------------------------- EngineData AST

class Node:
    """kind: 'dict' (items: list of (key, Node)), 'list' (items: list of Node), 'scalar' (raw bytes token).
    ws: whitespace bytes preceding the opening token / scalar. For dict entries, the key token carries
    its own ws (kws) and the value its own ws."""
    __slots__ = ('kind', 'items', 'raw', 'ws', 'close_ws', 'kws')
    def __init__(self, kind, ws=b''):
        self.kind = kind; self.items = []; self.raw = b''; self.ws = ws; self.close_ws = b''; self.kws = []

WS = b' \t\r\n'
BS = 0x5c

def tokenize(s):
    """Yield (ws, token) pairs."""
    n = len(s); pos = 0
    while pos < n:
        w0 = pos
        while pos < n and s[pos] in WS:
            pos += 1
        ws = bytes(s[w0:pos])
        if pos >= n:
            yield ws, b''
            return
        c = s[pos]
        if s[pos:pos+2] in (b'<<', b'>>'):
            yield ws, bytes(s[pos:pos+2]); pos += 2; continue
        if c in b'[]':
            yield ws, bytes(s[pos:pos+1]); pos += 1; continue
        if c == 0x28:  # (
            j = pos + 1
            while True:
                if s[j] == BS:
                    j += 2; continue
                if s[j] == 0x29:  # )
                    break
                j += 1
            yield ws, bytes(s[pos:j+1]); pos = j + 1; continue
        j = pos + 1
        while j < n and s[j] not in WS and s[j] not in b'<>[]()' and not (s[j] == 0x2f):
            j += 1
        yield ws, bytes(s[pos:j]); pos = j
    yield b'', b''

class Parser:
    def __init__(self, s):
        self.toks = list(tokenize(s)); self.i = 0
    def next(self):
        t = self.toks[self.i]; self.i += 1; return t
    def peek(self):
        return self.toks[self.i]
    def parse_value(self, ws, tok):
        if tok == b'<<':
            d = Node('dict', ws)
            while True:
                kws, k = self.next()
                if k == b'>>':
                    d.close_ws = kws; return d
                assert k.startswith(b'/'), (kws, k)
                vws, v = self.next()
                d.items.append((k[1:].decode(), self.parse_value(vws, v)))
                d.kws.append(kws)
        if tok == b'[':
            l = Node('list', ws)
            while True:
                vws, v = self.next()
                if v == b']':
                    l.close_ws = vws; return l
                l.items.append(self.parse_value(vws, v))
            return l
        n = Node('scalar', ws); n.raw = tok; return n

def parse_body(body):
    """Txt2 body is a bare sequence of key/value pairs (no outer << >>). Returns a dict Node with
    close_ws = trailing whitespace."""
    p = Parser(body)
    d = Node('dict', b'')
    while True:
        kws, k = p.next()
        if k == b'':
            d.close_ws = kws; return d
        assert k.startswith(b'/'), (kws, k)
        vws, v = p.next()
        d.items.append((k[1:].decode(), p.parse_value(vws, v)))
        d.kws.append(kws)

def serialize(node, out, bare=False):
    if node.kind == 'dict':
        if not bare:
            out += node.ws + b'<<'
        for (k, v), kws in zip(node.items, node.kws):
            out += kws + b'/' + k.encode()
            serialize(v, out)
        out += node.close_ws
        if not bare:
            out += b'>>'
    elif node.kind == 'list':
        out += node.ws + b'['
        for v in node.items:
            serialize(v, out)
        out += node.close_ws + b']'
    else:
        out += node.ws + node.raw
    return out

def body_bytes(root):
    return bytes(serialize(root, bytearray(), bare=True))

# canonical authoring helpers (single-space separators, Photoshop number spelling)

def ps_number(v):
    if isinstance(v, bool):
        return b'true' if v else b'false'
    if isinstance(v, int):
        return str(v).encode()
    s = ('%.5f' % v).rstrip('0')
    if s.endswith('.'):
        s += '0'
    if s.startswith('0.'):
        s = s[1:]
    elif s.startswith('-0.'):
        s = '-' + s[2:]
    return s.encode()

def scalar(raw, ws=b' '):
    n = Node('scalar', ws); n.raw = raw if isinstance(raw, bytes) else ps_number(raw); return n

def ps_string(text, ws=b' '):
    data = b'\xfe\xff' + text.encode('utf-16-be')
    esc = bytearray()
    for c in data:
        if c in (0x28, 0x29, 0x5c):
            esc.append(BS)
        esc.append(c)
    return scalar(b'(' + bytes(esc) + b')', ws)

def mkdict(pairs, ws=b' '):
    d = Node('dict', ws)
    for k, v in pairs:
        d.items.append((str(k), v)); d.kws.append(b' ')
    d.close_ws = b' '
    return d

def mklist(values, ws=b' '):
    l = Node('list', ws); l.items = list(values); l.close_ws = b' '; return l

def get(node, *path):
    for p in path:
        if node.kind == 'dict':
            node = dict(node.items)[str(p)]
        else:
            node = node.items[int(p)]
    return node

def set_item(node, key, value):
    key = str(key)
    for i, (k, v) in enumerate(node.items):
        if k == key:
            node.items[i] = (k, value); return
    node.items.append((key, value)); node.kws.append(b' ')

def del_item(node, key):
    key = str(key)
    for i, (k, v) in enumerate(node.items):
        if k == key:
            del node.items[i]; del node.kws[i]; return True
    return False

def clone(node):
    n = Node(node.kind, node.ws); n.raw = node.raw; n.close_ws = node.close_ws; n.kws = list(node.kws)
    if node.kind == 'dict':
        n.items = [(k, clone(v)) for k, v in node.items]
    elif node.kind == 'list':
        n.items = [clone(v) for v in node.items]
    return n

# ---------------------------------------------------------------- display

def scalar_text(raw):
    if raw.startswith(b'(') and raw[1:3] == b'\xfe\xff':
        inner = raw[3:-1].replace(b'\\(', b'(').replace(b'\\)', b')').replace(b'\\\\', b'\\')
        try:
            return 'S:' + inner.decode('utf-16-be', 'replace')
        except Exception:
            return 'S?' + repr(raw[:40])
    if raw.startswith(b'('):
        return 'A:' + raw[1:-1].decode('latin-1')
    return raw.decode('latin-1')

def to_py(node):
    if node.kind == 'dict':
        return {k: to_py(v) for k, v in node.items}
    if node.kind == 'list':
        return [to_py(v) for v in node.items]
    return scalar_text(node.raw)

def dump(node, ind='', depth=0, maxd=99, listmax=3):
    if node.kind == 'dict':
        for k, v in node.items:
            if v.kind != 'scalar':
                print(f"{ind}/{k} {v.kind}[{len(v.items)}]")
                if depth < maxd:
                    dump(v, ind + '  ', depth + 1, maxd, listmax)
            else:
                print(f"{ind}/{k} = {scalar_text(v.raw)[:80]}")
    elif node.kind == 'list':
        for j, v in enumerate(node.items[:listmax]):
            if v.kind == 'scalar':
                print(f"{ind}[{j}] = {scalar_text(v.raw)[:80]}")
            else:
                print(f"{ind}[{j}] {v.kind}[{len(v.items)}]")
                if depth < maxd:
                    dump(v, ind + '  ', depth + 1, maxd, listmax)
        if len(node.items) > listmax:
            print(f"{ind}... {len(node.items) - listmax} more")

def flatten(py, prefix='', out=None):
    if out is None:
        out = {}
    if isinstance(py, dict):
        for k, v in py.items():
            flatten(v, f"{prefix}/{k}", out)
    elif isinstance(py, list):
        out[prefix + '#len'] = len(py)
        for i, v in enumerate(py):
            flatten(v, f"{prefix}[{i}]", out)
    else:
        out[prefix] = py
    return out

def diff_trees(a, b):
    fa, fb = flatten(to_py(a)), flatten(to_py(b))
    keys = sorted(set(fa) | set(fb))
    for k in keys:
        va, vb = fa.get(k, '<absent>'), fb.get(k, '<absent>')
        if va != vb:
            print(f"{k}: {str(va)[:60]}  ->  {str(vb)[:60]}")

def load_root(path):
    b = read_psd(path)
    sec, blk = find_txt2(b)
    if blk is None:
        return b, None, None
    pstart, length, bstart, bend = blk
    body = bytes(b[pstart:pstart+length])
    return b, body, parse_body(body)

# ---------------------------------------------------------------- CLI

def main(argv):
    cmd = argv[1]
    if cmd == 'dump':
        b, body, root = load_root(argv[2])
        if root is None:
            print('no Txt2'); return
        maxd = int(argv[3]) if len(argv) > 3 else 99
        print(f"Txt2 body {len(body)} bytes")
        dump(root, maxd=maxd)
    elif cmd == 'roundtrip':
        b, body, root = load_root(argv[2])
        out = body_bytes(root)
        print('byte-exact' if out == body else f'MISMATCH at {next(i for i,(x,y) in enumerate(zip(out, body)) if x!=y) if len(out)==len(body) else "len %d vs %d" % (len(out), len(body))}')
        # also round trip through replace_txt2 and compare whole file
        nb = replace_txt2(b, out)
        print('file byte-exact' if bytes(nb) == bytes(b) else 'FILE MISMATCH')
    elif cmd == 'extract':
        b, body, root = load_root(argv[2])
        open(argv[3], 'wb').write(body)
        print(len(body))
    elif cmd == 'diff':
        _, _, ra = load_root(argv[2]); _, _, rb = load_root(argv[3])
        diff_trees(ra, rb)
    elif cmd == 'variant':
        name, src, dst = argv[2], argv[3], argv[4]
        import variants
        b, body, root = load_root(src)
        nb = variants.apply(name, b, root, sys.modules[__name__])
        open(dst, 'wb').write(nb)
        print(f'wrote {dst} ({len(nb)} bytes)')
    else:
        print(__doc__)

if __name__ == '__main__':
    main(sys.argv)
