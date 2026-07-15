#!/usr/bin/env python3
"""Minimal classic-Mac resource-fork reader for the SoundEffects app.

Reusable across the remaining disassembly items. Exposes:
  Resource(type, id, name, data)
  read_resources(path) -> list[Resource]
  by_type(resources) -> dict[type -> {id: Resource}]
"""
import struct, sys, os, collections

# The extracted app lives next to the repo root (gitignored — its data is in the
# HFS resource fork). Resolve relative to this file so the tools work from any cwd;
# override with the SFX_APP_RSRC env var if the extracted folder is elsewhere.
_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP_RSRC = os.environ.get(
    "SFX_APP_RSRC",
    os.path.join(_REPO, "SoundEffects 0.9.2 ƒ",
                 "SoundEffects 0.9.2", "..namedfork", "rsrc"),
)

class Resource:
    __slots__ = ("type", "id", "name", "data")
    def __init__(self, type, id, name, data):
        self.type, self.id, self.name, self.data = type, id, name, data
    def __repr__(self):
        return f"<{self.type.decode('mac_roman')} {self.id} {self.name!r} {len(self.data)}B>"

def read_resources(path=APP_RSRC):
    d = open(path, "rb").read()
    data_off, map_off, data_len, map_len = struct.unpack(">IIII", d[0:16])
    m = d[map_off:map_off+map_len]
    type_list_off, name_list_off = struct.unpack(">HH", m[24:28])
    num_types = struct.unpack(">H", m[type_list_off:type_list_off+2])[0]
    out = []
    pos = type_list_off + 2
    for _ in range(num_types + 1):
        typ = m[pos:pos+4]
        cnt, ref_off = struct.unpack(">HH", m[pos+4:pos+8])
        pos += 8
        rl = type_list_off + ref_off
        for j in range(cnt + 1):
            rid, name_off = struct.unpack(">hH", m[rl:rl+4])
            attrs = m[rl+4]
            doff = struct.unpack(">I", b"\x00" + m[rl+5:rl+8])[0]
            rl += 12
            name = b""
            if name_off != 0xFFFF:
                np = name_list_off + name_off
                nlen = m[np]
                name = m[np+1:np+1+nlen]
            abs_off = data_off + doff
            rlen = struct.unpack(">I", d[abs_off:abs_off+4])[0]
            rdata = d[abs_off+4:abs_off+4+rlen]
            out.append(Resource(typ, rid, name.decode("mac_roman"), rdata))
    return out

def by_type(resources):
    t = collections.defaultdict(dict)
    for r in resources:
        t[r.type][r.id] = r
    return t

def parse_strs(data):
    """Parse a STR# resource -> list of pascal strings."""
    n = struct.unpack(">H", data[0:2])[0]
    out = []
    p = 2
    for _ in range(n):
        ln = data[p]; p += 1
        out.append(data[p:p+ln].decode("mac_roman")); p += ln
    return out

if __name__ == "__main__":
    rs = read_resources()
    t = by_type(rs)
    for typ in sorted(t, key=lambda x: x):
        print(typ.decode("mac_roman"), "ids:", sorted(t[typ]))
