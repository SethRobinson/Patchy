"""Probe variants for txt2tool.py `variant <name> <in.psd> <out.psd>`.

Object paths (photoshop-text-tracking.psd): /1/1 is the text-object list; object i has
/0 model (/0 text, /5 ParagraphRun, /6 StyleRun) and /1 view (/2 cached layout tree).
"""
import struct

def objects(root, T):
    return T.get(root, '1', '1')

def strip_layout(obj, T, mode):
    view = T.get(obj, '1')
    if mode == 'empty':
        T.set_item(view, '2', T.mklist([]))
    elif mode == 'remove':
        T.del_item(view, '2')
    elif mode == 'minimal':
        keep = [(k, v) for k, v in view.items if k in ('4', '0')]
        view.items = keep; view.kws = [b' '] * len(keep)

def set_text(obj, new_text, T):
    """Replace the object's text and rewrite the ParagraphRun/StyleRun lengths to the new length
    (one run each; the tracking fixture's runs are single-run). Text carries a trailing CR in
    Photoshop's model (`HHHHHHHHHH\r` = 11 chars)."""
    model = T.get(obj, '0')
    text = new_text + '\r'
    T.set_item(model, '0', T.ps_string(text))
    n = len(text)
    for run_key in ('5', '6'):
        runs = T.get(model, run_key, '0')  # list of run entries
        for entry in runs.items:
            T.set_item(entry, '1', T.scalar(n))

def patch_text_index(b, layer_ordinal, new_index):
    """Rewrite the TextIndex long in the TySh of the Nth TySh block in file order (0-based)."""
    pos = -1
    for _ in range(layer_ordinal + 1):
        pos = b.find(b'8BIMTySh', pos + 1)
        assert pos >= 0
    key = b'TextIndexlong'
    k = b.find(key, pos)
    assert k > 0
    b[k + len(key):k + len(key) + 4] = struct.pack('>i', new_index)
    return b

def apply(name, b, root, T):
    objs = objects(root, T)
    if name == 'v0':
        pass
    elif name == 'v1':
        strip_layout(objs.items[0], T, 'empty')
    elif name == 'v2':
        strip_layout(objs.items[0], T, 'remove')
    elif name == 'v3':
        strip_layout(objs.items[0], T, 'minimal')
    elif name == 'v4':
        set_text(objs.items[0], 'HHHHHHHHXX', T)
    elif name in ('v5', 'v6'):
        # append a copy of object 1 with new text, and point the second TySh (file order) at it
        new = T.clone(objs.items[1])
        set_text(new, 'APPENDED', T)
        if name == 'v6':
            strip_layout(new, T, 'remove')
        objs.items.append(new)
        b = patch_text_index(bytearray(b), 1, len(objs.items) - 1)
    elif name == 'v2all':
        for o in objs.items:
            strip_layout(o, T, 'remove')
    elif name == 'v4b':
        # both text changes and stripped cache on object 0
        set_text(objs.items[0], 'HHHHHHHHXX', T)
        strip_layout(objs.items[0], T, 'remove')
    else:
        raise SystemExit('unknown variant ' + name)
    return bytes(T.replace_txt2(bytearray(b), T.body_bytes(root)))
