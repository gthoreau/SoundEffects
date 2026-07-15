# Disassembly tooling

Reusable helpers for reverse-engineering the main **SoundEffects 0.9.2** application
binary (as opposed to the self-contained `EFCT` effect modules, which are disassembled
ad hoc). Built while recovering the easter-egg trigger dates
(`../disassembly/Easter-Eggs.md`) and reused for the remaining app-level open items
(Meters/Feet constant, Select Silence/Peak, Spectrum/Mixer).

## Requirements

- Python 3
- [`capstone`](https://www.capstone-engine.org/) (`pip install capstone`) — M68K/040 mode
- The extracted `SoundEffects 0.9.2 ƒ/` folder next to the repo root, with its HFS
  resource fork intact (it is gitignored; regenerate from `soundeffects-092.sit`).
  Point `SFX_APP_RSRC` at the app's `..namedfork/rsrc` if it lives elsewhere.

## `rez.py` — resource-fork reader

Parses a classic-Mac resource fork into `Resource(type, id, name, data)` objects.

```python
import rez
rs = rez.read_resources()            # defaults to the app; pass a path for a module
t  = rez.by_type(rs)                 # {b'CODE': {0: Resource, 1: Resource, ...}, ...}
strs = rez.parse_strs(t[b'STR#'][138].data)   # -> ['Merry Christmas!', ...]
```

Run directly (`python3 rez.py`) to list every resource type and its IDs.

## `code.py` — CODE-segment disassembler + jump-table resolver

The app is a 13-segment binary (`CODE 0` jump table + `CODE 1…12`). Cross-segment
calls compile to `jsr xxx(a5)` through the `CODE 0` jump table; `parse_jt()` decodes
that table (358 entries) so those calls resolve to `(segment, in-segment offset)`.

```python
import code as C
t  = C.load()                        # by_type of the app resources
jt = C.parse_jt(t[b'CODE'][0].data)  # {'entries': [{'jt_index','a5_off','seg','seg_off',...}]}
d  = t[b'CODE'][7].data
for insn in C.md.disasm(d[4:], 4):   # numbered CODE segs: 4-byte header, code at +4
    ...
```

A jump-table entry at `A5 + (jtOffset + index*8)` is called as `jsr (a5_off + 2)(a5)`
(the `+2` skips the entry's offset word onto its `move.w #segID` / patched `JMP`).
The in-segment offset stored in the JT is relative to code start, so the file offset
within the `CODE` resource is `seg_off + 4`.
