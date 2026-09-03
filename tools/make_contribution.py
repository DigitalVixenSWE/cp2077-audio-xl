"""Slice records out of a full WolvenKit audio-metadata export into a small AudioXL contribution.

Produces a valid standalone `audioCookedMetadataResource` JSON containing only the chosen entries,
ready for `WolvenKit.CLI convert deserialize` -> which produces a small `.audio_metadata` which a
mod can ship at its own path.

  python make_contribution.py --source <export.json> --out <small.json>
        [--name-contains evelyn] [--type audioFootwearVsMaterialMetadata]
        [--set footwearType=evelyn]

`--set` rewrites a top-level CName property on every emitted entry.
"""
import argparse, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from merge_audio_metadata import read_doc, key_and_hash, renumber

def set_cname(entry_text, prop, value):
    obj = json.loads(entry_text)
    data = obj['Data']
    if prop not in data:
        return entry_text, False
    node = data[prop]
    if not (isinstance(node, dict) and node.get('$type') == 'CName'):
        return entry_text, False
    node['$value'] = value
    body = json.dumps(obj, indent=2)
    return '\n'.join(' ' * 8 + line for line in body.split('\n')), True

EVENT_PROPS = ('defaultFootstep', 'skidEvent', 'onEnterSound', 'onExitSound')

def force_events(entry_text, event):
    obj = json.loads(entry_text)
    n = 0

    def walk(node, key=None):
        nonlocal n
        if isinstance(node, dict):
            if node.get('$type') == 'CName' and key in EVENT_PROPS + ('value',):
                if node.get('$value') not in (None, 'None'):
                    node['$value'] = event
                    n += 1
                return
            for k, v in node.items():
                walk(v, k)
        elif isinstance(node, list):
            for v in node:
                walk(v, key)

    walk(obj['Data'])
    body = json.dumps(obj, indent=2)
    return '\n'.join(' ' * 8 + line for line in body.split('\n')), n

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--name-contains')
    ap.add_argument('--type')
    ap.add_argument('--set', action='append', default=[],
                    help='prop=value, rewrites a top-level CName on every entry')
    ap.add_argument('--append', action='append', default=[],
                    help='another contribution JSON whose entries are appended to the output')
    ap.add_argument('--force-events',
                    help='rewrite every event CName in the entry - defaultFootstep, skidEvent, and '
                         'every locomotionStates / customActionEvents value - to this one event. '
                         'Makes a set sound identical on every surface.')
    a = ap.parse_args()

    prefix, entries, suffix = read_doc(a.source)
    print(f"source: {len(entries)} entries")

    picked = []
    for txt in entries:
        (etype, ename), _ = key_and_hash(txt)
        if a.type and etype != a.type:
            continue
        if a.name_contains and a.name_contains not in str(ename):
            continue
        picked.append((etype, ename, txt))

    if not picked:
        print("no entries matched - nothing written")
        return 1

    edits = [s.split('=', 1) for s in a.set]
    out_entries = []
    for etype, ename, txt in picked:
        for prop, value in edits:
            txt, ok = set_cname(txt, prop, value)
            if not ok:
                print(f"  WARN {ename}: no top-level CName '{prop}'")
        if a.force_events:
            txt, n = force_events(txt, a.force_events)
            print(f"  + {etype} {ename}  ({n} events -> {a.force_events})")
            out_entries.append(txt)
            continue
        out_entries.append(txt)
        print(f"  + {etype} {ename}")

    for extra in a.append:
        _, extra_entries, _ = read_doc(extra)
        for txt in extra_entries:
            (etype, ename), _ = key_and_hash(txt)
            out_entries.append(txt)
            print(f"  + [from {os.path.basename(extra)}] {etype} {ename}")

    counter = iter(range(10 ** 9))
    body = ',\n'.join(renumber(t, counter) for t in out_entries)
    with open(a.out, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(prefix) + '\n')
        fh.write(body + '\n')
        fh.write('\n'.join(suffix) + '\n')
    print(f"\nwrote {a.out}  ({len(out_entries)} entries, {os.path.getsize(a.out):,} bytes)")
    return 0

if __name__ == '__main__':
    sys.exit(main())
