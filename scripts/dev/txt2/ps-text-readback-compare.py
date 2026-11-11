"""Compares two ps-text-readback.ps1 results: an original file and a save of it (docs/txt2.md).

    python ps-text-readback-compare.py <original.json> <save.json> [--png]

Type layers are matched by their index path, then by name. For each one it prints what differs
of: the anchor (text click point, pixels), the anti-aliasing method, the kerning of each style
range, the old-text flag, textWarningLevel, the box first-baseline rule, and the layer's bounds
after Photoshop laid it out again. With --png it also counts the pixels of the two -relaid.png
renders that differ by more than 6 of 255 in any channel (Pillow and numpy needed). Exit code 1
when anything differs.
"""
import json
import sys
from pathlib import Path


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def near(a, b, tolerance):
    if a is None or b is None:
        return a is None and b is None
    return all(abs(x - y) <= tolerance for x, y in zip(a, b)) and len(a) == len(b)


def stated(kerning):
    # "(not stated)" and an explicit value are different spellings of what a file says; the
    # layout is compared through the relaid bounds and the render.
    return [entry.split(' ', 1)[1] if ' ' in entry else entry for entry in kerning]


def compare(original, save, png):
    differences = 0
    by_path = {layer.get('path'): layer for layer in save['layers']}
    by_name = {}
    for layer in save['layers']:
        by_name.setdefault(layer.get('name'), []).append(layer)
    print(f"{Path(original['file']).name}: {len(original['layers'])} type layer(s), save has {len(save['layers'])}")
    for layer in original['layers']:
        other = by_path.get(layer.get('path'))
        if other is None or other.get('name') != layer.get('name'):
            candidates = by_name.get(layer.get('name'), [])
            other = candidates[0] if candidates else None
        label = f"  [{layer.get('path')}] {layer.get('name')!r}"
        if other is None:
            print(label, 'MISSING in the save (not a type layer any more?)')
            differences += 1
            continue
        if 'error' in layer or 'error' in other:
            print(label, 'error:', layer.get('error'), '|', other.get('error'))
            differences += 1
            continue
        notes = []
        if not near(layer.get('click'), other.get('click'), 0.01):
            notes.append(f"anchor {layer.get('click')} -> {other.get('click')}")
        for key in ('antiAlias', 'oldText', 'warning', 'frameBaseline', 'shape', 'align'):
            if layer.get(key) != other.get(key):
                notes.append(f"{key} {layer.get(key)} -> {other.get(key)}")
        if stated(layer.get('kerning', [])) != stated(other.get('kerning', [])):
            notes.append(f"kerning {layer.get('kerning')} -> {other.get('kerning')}")
        if not near(layer.get('relaidBounds'), other.get('relaidBounds'), 0.01):
            notes.append(f"relaid bounds {layer.get('relaidBounds')} -> {other.get('relaidBounds')}")
        if notes:
            differences += 1
            print(label)
            for note in notes:
                print('      ' + note)
        else:
            print(label, 'same: anchor', layer.get('click'), layer.get('antiAlias'), stated(layer.get('kerning', [])),
                  'oldText' if layer.get('oldText') else '')
    if png:
        import numpy
        from PIL import Image
        a_path = Path(str(original['_json']).replace('.json', '-relaid.png'))
        b_path = Path(str(save['_json']).replace('.json', '-relaid.png'))
        a = numpy.asarray(Image.open(a_path).convert('RGB')).astype(int)
        b = numpy.asarray(Image.open(b_path).convert('RGB')).astype(int)
        if a.shape != b.shape:
            print('  renders differ in size', a.shape, b.shape)
            differences += 1
        else:
            changed = int((numpy.abs(a - b).max(axis=2) > 6).sum())
            print(f"  renders after Photoshop's re-layout: {changed} pixel(s) differ")
            if changed:
                differences += 1
    return differences


def main():
    arguments = [a for a in sys.argv[1:] if not a.startswith('--')]
    original = load(arguments[0])
    save = load(arguments[1])
    original['_json'] = arguments[0]
    save['_json'] = arguments[1]
    return 1 if compare(original, save, '--png' in sys.argv) else 0


if __name__ == '__main__':
    sys.exit(main())
