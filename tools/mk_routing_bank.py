"""Build AudioXL's routing bank: Audio Input events parented to the games own mixers.

The vanilla `mod.bnk` gives REDmod nine Audio Input events, all under SFX-side parents, so every
custom sound follows the SFX slider. This bank clones its Sound/FxCustom/Action/Event objects with
new ids and re-parents them onto the vanilla dialogue and music actor-mixers, which makes a row
typed `axl_voice_2d` follow the Dialogue slider (and VO ducking) and `axl_music_2d` the Music
slider. Audio Input sounds carry no media.

  python mk_routing_bank.py --mod-bnk <vanilla mod.bnk> --out audioxl_routing.bnk
"""
import argparse
import struct

def fnv1_32(s: str) -> int:
    h = 0x811C9DC5
    for c in s.lower().encode():
        h = ((h * 0x01000193) & 0xFFFFFFFF) ^ c
    return h

EVENTS = {
    "axl_voice_2d":  (0x5B770C24, "custom_sound_test"),
    "axl_music_2d":  (0xEDF036D6, "custom_sound_test"),
    "axl_radio_2d":  (0x20E9A1B9, "custom_sound_test"),
    "axl_radioport_2d": (None, "radio.bnk:0da4f800"),
    "axl_sfx_2d":    (0x17705D3E, "custom_sound_test"),
    "axl_master_2d": (0xE2B7BC37, "custom_sound_test"),
    "axl_radio_3d":     (None, "radio.bnk:09a3a81e"),
    "axl_radio_veh3d":  (None, "radio.bnk:222a783c"),
}
TEMPLATE_EVENT_IDS = {"custom_sound_test": 0x4BA03A92, "mod_sfx_2d": 0x045845C4}

SND_ID, SND_SOURCE, SND_BUS, SND_PARENT = 0, 9, 22, 26
ACT_ID, ACT_TARGET = 0, 6
EVT_ID, EVT_ACTION = 0, 5

def read_bank(path):
    d = open(path, "rb").read()
    chunks = []
    i = 0
    while i < len(d):
        tag = d[i:i + 4]
        sz = struct.unpack_from("<I", d, i + 4)[0]
        chunks.append((tag, d[i + 8:i + 8 + sz]))
        i += 8 + sz
    return chunks

def parse_hirc(body):
    n = struct.unpack_from("<I", body, 0)[0]
    p = 4
    objs = []
    for _ in range(n):
        t = body[p]
        s = struct.unpack_from("<I", body, p + 1)[0]
        objs.append((t, bytearray(body[p + 5:p + 5 + s])))
        p += 5 + s
    return objs

def patch32(b, off, v):
    struct.pack_into("<I", b, off, v)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod-bnk", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bank-name", default="audioxl_routing")
    ap.add_argument("--banks-dir", default="", help="folder with vanilla banks for bank:hexid templates (default: the mod.bnk folder)")
    ap.add_argument("--only", default="", help="comma list of event names to emit (default all)")
    ap.add_argument("--parent", default="", help="override every parent id (hex) - probe use")
    a = ap.parse_args()
    events = EVENTS
    if a.only:
        events = {k: v for k, v in EVENTS.items() if k in a.only.split(",")}
    if a.parent:
        events = {k: (int(a.parent, 16), v[1]) for k, v in events.items()}

    chunks = read_bank(a.mod_bnk)
    bkhd = bytearray(dict(chunks)[b"BKHD"])
    objs = parse_hirc(dict(chunks)[b"HIRC"])
    by_id = {struct.unpack_from("<I", b, 0)[0]: (t, b) for t, b in objs}

    templates = {}
    for name, eid in TEMPLATE_EVENT_IDS.items():
        t, ev = by_id[eid]
        act_id = struct.unpack_from("<I", ev, EVT_ACTION)[0]
        _, act = by_id[act_id]
        snd_id = struct.unpack_from("<I", act, ACT_TARGET)[0]
        _, snd = by_id[snd_id]
        src_id = struct.unpack_from("<I", snd, SND_SOURCE)[0]
        _, src = by_id[src_id]
        templates[name] = (src, snd, act, ev)

    import os
    banks_dir = a.banks_dir or os.path.dirname(a.mod_bnk)
    base_src, base_snd, base_act, base_ev = templates["custom_sound_test"]

    def foreign_sound(spec):
        bank, hexid = spec.split(":")
        objs2 = parse_hirc(dict(read_bank(os.path.join(banks_dir, bank)))[b"HIRC"])
        by2 = {struct.unpack_from("<I", b, 0)[0]: (t, b) for t, b in objs2}
        t, body = by2[int(hexid, 16)]
        assert t == 2, "template must be a Sound"
        snd = bytearray(body)
        snd[4:18] = base_snd[4:18]
        return snd

    out_objs = []
    for name, (parent, tname) in events.items():
        if ":" in tname:
            src, act, ev = (bytearray(x) for x in (base_src, base_act, base_ev))
            snd = foreign_sound(tname)
        else:
            src, snd, act, ev = (bytearray(x) for x in templates[tname])
        src_id = fnv1_32(name + "_src")
        snd_id = fnv1_32(name + "_snd")
        act_id = fnv1_32(name + "_act")
        evt_id = fnv1_32(name)
        patch32(src, 0, src_id)
        patch32(snd, SND_ID, snd_id)
        patch32(snd, SND_SOURCE, src_id)
        if parent is not None:
            patch32(snd, SND_BUS, 0)
            patch32(snd, SND_PARENT, parent)
        else:
            parent = struct.unpack_from("<I", snd, SND_PARENT)[0]
        patch32(act, ACT_ID, act_id)
        patch32(act, ACT_TARGET, snd_id)
        patch32(ev, EVT_ID, evt_id)
        patch32(ev, EVT_ACTION, act_id)
        out_objs += [(17, src), (2, snd), (3, act), (4, ev)]
        print(f"  {name:14s} event={evt_id:08x} sound={snd_id:08x} parent={parent:08x}")

    hirc = struct.pack("<I", len(out_objs))
    for t, b in out_objs:
        hirc += struct.pack("<BI", t, len(b)) + bytes(b)
    patch32(bkhd, 4, fnv1_32(a.bank_name))
    out = b"BKHD" + struct.pack("<I", len(bkhd)) + bytes(bkhd)
    out += b"HIRC" + struct.pack("<I", len(hirc)) + hirc
    open(a.out, "wb").write(out)
    print(f"wrote {a.out}: {len(out)} bytes, {len(out_objs)} objects, bank id {fnv1_32(a.bank_name):08x}")

if __name__ == "__main__":
    main()
