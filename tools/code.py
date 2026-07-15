#!/usr/bin/env python3
"""Disassemble the SoundEffects app CODE segments and resolve the CODE-0
jump table, so cross-segment jsr/jmp through A5 can be named.

Classic Mac 'CODE 0' layout (Segment Loader):
  +0  aboveA5   (long)  size of app params + jump table above A5
  +4  belowA5   (long)  size of app globals below A5
  +8  jtSize    (long)  size of jump table in bytes
  +12 jtOffset  (long)  offset of jump table from A5
  +16 jump table entries (8 bytes each)

Unloaded 8-byte JT entry:
  +0 offset-in-segment (word)
  +2 0x3F3C  (move.w #segID,-(sp))
  +4 segID   (word)
  +6 0xA9F0  (_LoadSeg)

Numbered CODE segment (1..N) header (first 4 bytes):
  +0 first JT entry offset (word)
  +2 number of JT entries (word)
  +4 code begins
"""
import struct
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_040
import rez

md = Cs(CS_ARCH_M68K, CS_MODE_M68K_040)
md.detail = False

def load():
    rs = rez.read_resources()
    return rez.by_type(rs)

def parse_jt(code0):
    aboveA5, belowA5, jtSize, jtOffset = struct.unpack(">IIII", code0[:16])
    entries = []
    p = 16
    idx = 0
    while p + 8 <= len(code0):
        off_in_seg, ld_instr, seg_id, trap = struct.unpack(">HHHH", code0[p:p+8])
        entries.append({
            "jt_index": idx, "a5_off": jtOffset + idx*8,
            "seg": seg_id, "seg_off": off_in_seg,
            "loadseg": (ld_instr == 0x3F3C and trap == 0xA9F0),
        })
        p += 8; idx += 1
    return {"aboveA5": aboveA5, "belowA5": belowA5, "jtSize": jtSize,
            "jtOffset": jtOffset, "entries": entries}

def seg_code_start(seg_data):
    # numbered CODE segment: 4-byte header then code
    return 4

def disasm_seg(seg_data, base=0):
    """Yield (addr, mnemonic, op_str, size, raw_bytes). addr is file offset within resource."""
    start = seg_code_start(seg_data)
    code = seg_data[start:]
    for insn in md.disasm(code, start):
        yield insn

if __name__ == "__main__":
    t = load()
    jt = parse_jt(t[b"CODE"][0].data)
    print(f"aboveA5={jt['aboveA5']} belowA5={jt['belowA5']} jtSize={jt['jtSize']} jtOffset={jt['jtOffset']}")
    print(f"jump table entries: {len(jt['entries'])}")
    from collections import Counter
    c = Counter(e["seg"] for e in jt["entries"] if e["loadseg"])
    print("entries per segment:", dict(sorted(c.items())))
