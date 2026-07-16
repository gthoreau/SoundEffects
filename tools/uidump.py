#!/usr/bin/env python3
"""Dump the app's UI resources to JSON — the data behind SPEC §15 step 7.

Steps 2–6 ported DSP out of C source and disassembly. The UI chrome needs no
porting: menus, windows, dialogs, controls and the whole first-person string
corpus are *already declarative data* sitting in the resource fork. This reads
them out verbatim into `src/ui/resources/*.json`, which is what the shell then
renders. Nothing here interprets or prettifies — a bevel is a rendering choice,
a rect is a fact.

The fork is gitignored (regenerate from `soundeffects-092.sit`), so the JSON is
the committed artifact. Re-run after any re-extraction:

    python3 tools/uidump.py            # writes src/ui/resources/
    python3 tools/uidump.py --check    # exits nonzero if the JSON is stale
"""
import json
import os
import struct
import sys

import rez

_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(_REPO, "src", "ui", "resources")


def _pstr(d, p):
    """Read a pascal string at p -> (text, next_pos)."""
    n = d[p]
    return d[p + 1 : p + 1 + n].decode("mac_roman"), p + 1 + n


def _rect(d, p):
    top, left, bottom, right = struct.unpack(">hhhh", d[p : p + 8])
    return {"top": top, "left": left, "bottom": bottom, "right": right}, p + 8


# ---------------------------------------------------------------- WIND / DLOG / ALRT

def parse_wind(d):
    """WIND: rect, procID, visible, goAway, refCon, title, [positioning]."""
    r, p = _rect(d, 0)
    proc_id, visible, _f1, go_away, _f2, ref_con = struct.unpack(">hBBBBi", d[p : p + 10])
    p += 10
    title, p = _pstr(d, p)
    out = {
        "rect": r,
        "procID": proc_id,
        "visible": bool(visible),
        "goAway": bool(go_away),
        "refCon": ref_con,
        "title": title,
    }
    if p % 2:
        p += 1
    if p + 2 <= len(d):
        out["positioning"] = struct.unpack(">H", d[p : p + 2])[0]
    return out


def parse_dlog(d):
    """DLOG: WIND's header + an itemsID pointing at the DITL."""
    r, p = _rect(d, 0)
    proc_id, visible, _f1, go_away, _f2, ref_con, items_id = struct.unpack(
        ">hBBBBih", d[p : p + 12]
    )
    p += 12
    title, p = _pstr(d, p)
    out = {
        "rect": r,
        "procID": proc_id,
        "visible": bool(visible),
        "goAway": bool(go_away),
        "refCon": ref_con,
        "itemsID": items_id,
        "title": title,
    }
    if p % 2:
        p += 1
    if p + 2 <= len(d):
        out["positioning"] = struct.unpack(">H", d[p : p + 2])[0]
    return out


def parse_alrt(d):
    """ALRT: rect, itemsID, stages word, [positioning].

    The stages word packs four 4-bit stage descriptors (sound level, drawn item,
    visible) — kept raw; only `itemsID` and the rect drive rendering.
    """
    r, p = _rect(d, 0)
    items_id, stages = struct.unpack(">hH", d[p : p + 4])
    p += 4
    out = {"rect": r, "itemsID": items_id, "stages": stages}
    if p + 2 <= len(d):
        out["positioning"] = struct.unpack(">H", d[p : p + 2])[0]
    return out


# ---------------------------------------------------------------- DITL

# Inside Macintosh: Toolbox Essentials, "Item Types in an Item List".
_DITL_KINDS = {
    0: "userItem",
    1: "helpItem",
    4: "button",
    5: "checkBox",
    6: "radioButton",
    7: "resControl",
    8: "staticText",
    16: "editText",
    32: "icon",
    64: "picture",
}


def parse_ditl(d):
    """DITL: count−1, then per item: 4-byte handle placeholder, rect, type, len, data."""
    count = struct.unpack(">h", d[0:2])[0] + 1
    items = []
    p = 2
    for i in range(count):
        p += 4  # placeholder for the runtime handle/proc pointer — zero on disk
        r, p = _rect(d, p)
        raw = d[p]
        p += 1
        n = d[p]
        p += 1
        payload = d[p : p + n]
        p += n
        if p % 2:
            p += 1  # items are word-aligned

        # High bit clear = "disabled" in the resource's sense: the item does not
        # report clicks. Static text and icons are routinely stored disabled.
        enabled = bool(raw & 0x80)
        kind_code = raw & 0x7F
        item = {
            "id": i + 1,  # DITL items are 1-based everywhere in the Toolbox
            "kind": _DITL_KINDS.get(kind_code, f"unknown{kind_code}"),
            "enabled": enabled,
            "rect": r,
        }
        if kind_code in (4, 5, 6, 8, 16):
            item["text"] = payload.decode("mac_roman")
        elif kind_code in (7, 32, 64) and n >= 2:
            item["resID"] = struct.unpack(">h", payload[0:2])[0]
        elif n:
            item["data"] = list(payload)
        items.append(item)
    return items


# ---------------------------------------------------------------- MENU

def parse_menu(d):
    """MENU: id, width, height, procID, filler, enableFlags, title, items, 0 terminator.

    `enableFlags` bit 0 is the menu itself, bits 1..31 are items 1..31 — which is
    why a menu with more than 31 items cannot disable the overflow. The Effects
    menu is populated at launch, so it ships nearly empty.
    """
    menu_id, width, height, proc_id, _filler, enable_flags = struct.unpack(
        ">hhhhhI", d[0:14]
    )
    p = 14
    title, p = _pstr(d, p)
    items = []
    while p < len(d) and d[p] != 0:
        text, p = _pstr(d, p)
        icon, key, mark, style = struct.unpack(">BBBB", d[p : p + 4])
        p += 4
        idx = len(items) + 1
        items.append(
            {
                "index": idx,
                "text": text,
                "icon": icon,
                "key": chr(key) if key >= 0x20 else key,
                "mark": chr(mark) if mark >= 0x20 else mark,
                "style": style,
                "enabled": bool(enable_flags >> idx & 1) if idx <= 31 else True,
            }
        )
    return {
        "menuID": menu_id,
        "width": width,
        "height": height,
        "procID": proc_id,
        "enableFlags": enable_flags,
        "enabled": bool(enable_flags & 1),
        "title": title,
        "items": items,
    }


def parse_mbar(d):
    n = struct.unpack(">h", d[0:2])[0]
    return [struct.unpack(">h", d[2 + 2 * i : 4 + 2 * i])[0] for i in range(n)]


# ---------------------------------------------------------------- CNTL

# ---------------------------------------------------------------- icons / rect lists

def parse_icon_suite(d, size):
    """`ICN#` (32×32) / `ics#` (16×16): a 1-bit image followed by a 1-bit mask.

    Emitted as rows of "01" strings — verbose, but it diffs readably when the
    fork is re-extracted, and the shell turns them into ImageData anyway.
    """
    row_bytes = size // 8
    plane = size * row_bytes

    def rows(off):
        out = []
        for y in range(size):
            bits = ""
            for b in d[off + y * row_bytes : off + (y + 1) * row_bytes]:
                bits += format(b, "08b")
            out.append(bits)
        return out

    icon = {"size": size, "data": rows(0)}
    # A suite without its mask is legal and a few here ship that way.
    if len(d) >= 2 * plane:
        icon["mask"] = rows(plane)
    return icon


def parse_btns(d):
    """`BTNs` — a bare list of rects, per the app's own `TMPL 128`.

    Not a standard Toolbox type: Ricci defined it to hold a window's button
    hit-rects. `BTNs 135` ("MixWind") is **zero bytes long** — the Mixer's
    button list is empty, which is §3's "UNFINISHED in 0.9.2" stated in the
    resource fork rather than inferred from the disassembly.
    """
    return [_rect(d, p)[0] for p in range(0, len(d) - 7, 8)]


def parse_nrct(d):
    """`nrct` — a counted rect list (count word, then rects)."""
    n = struct.unpack(">H", d[0:2])[0]
    return [_rect(d, 2 + 8 * i)[0] for i in range(n)]


def parse_cntl(d):
    """CNTL: rect, value, visible, max, min, procID, refCon, title."""
    r, p = _rect(d, 0)
    value, visible, _f, maxv, minv, proc_id, ref_con = struct.unpack(
        ">hBBhhhi", d[p : p + 14]
    )
    p += 14
    title, p = _pstr(d, p)
    return {
        "rect": r,
        "value": value,
        "visible": bool(visible),
        "max": maxv,
        "min": minv,
        "procID": proc_id,
        "refCon": ref_con,
        "title": title,
    }


# ---------------------------------------------------------------- driver

def build(t):
    def each(typ, fn):
        return {str(i): fn(t[typ][i].data) for i in sorted(t[typ])}

    def named(typ):
        return {
            str(i): t[typ][i].name for i in sorted(t[typ]) if t[typ][i].name
        }

    out = {
        "WIND": each(b"WIND", parse_wind),
        "DLOG": each(b"DLOG", parse_dlog),
        "ALRT": each(b"ALRT", parse_alrt),
        "DITL": each(b"DITL", parse_ditl),
        "MENU": each(b"MENU", parse_menu),
        "CNTL": each(b"CNTL", parse_cntl),
        "MBAR": each(b"MBAR", parse_mbar),
        "STR#": each(b"STR#", rez.parse_strs),
        "STR": {
            str(i): _pstr(t[b"STR "][i].data, 0)[0] for i in sorted(t[b"STR "])
        },
        "ICN#": {
            str(i): parse_icon_suite(t[b"ICN#"][i].data, 32) for i in sorted(t[b"ICN#"])
        },
        "ics#": {
            str(i): parse_icon_suite(t[b"ics#"][i].data, 16) for i in sorted(t[b"ics#"])
        },
        "BTNs": each(b"BTNs", parse_btns),
        "nrct": each(b"nrct", parse_nrct),
    }
    # Resource names are documentation the fork carries for free — WIND 128 is
    # named, and the names say what each template is for.
    out["names"] = {
        typ: named(typ.encode("mac_roman") if typ != "STR" else b"STR ")
        for typ in ("WIND", "DLOG", "ALRT", "DITL", "MENU", "CNTL", "STR#", "STR",
                    "ICN#", "ics#", "BTNs", "nrct")
    }
    return out


FILES = {
    "windows.json": ("WIND", "DLOG", "ALRT"),
    "dialogs.json": ("DITL",),
    "menus.json": ("MENU", "MBAR"),
    "controls.json": ("CNTL", "BTNs", "nrct"),
    "strings.json": ("STR#", "STR"),
    "icons.json": ("ICN#", "ics#"),
}


def main():
    check = "--check" in sys.argv
    t = rez.by_type(rez.read_resources())
    all_ = build(t)

    os.makedirs(OUT_DIR, exist_ok=True)
    stale = []
    for fname, types in FILES.items():
        doc = {k: all_[k] for k in types}
        doc["names"] = {k: all_["names"][k] for k in types if k in all_["names"]}
        text = json.dumps(doc, indent=1, ensure_ascii=False) + "\n"
        path = os.path.join(OUT_DIR, fname)
        if check:
            existing = open(path, encoding="utf-8").read() if os.path.exists(path) else None
            if existing != text:
                stale.append(fname)
        else:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            print(f"wrote {os.path.relpath(path, _REPO)} ({len(text)} bytes)")

    if check:
        if stale:
            print("stale (re-run tools/uidump.py): " + ", ".join(stale))
            return 1
        print("resource JSON is up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
