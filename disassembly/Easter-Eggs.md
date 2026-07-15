# Easter-egg trigger dates — full recovery (68k disassembly)

**Source:** `CODE` segments of the main application (`SoundEffects 0.9.2`, creator `√SFx`), resource fork 397 636 bytes, disassembled with capstone (M68K/040). Unlike the effect modules, the app is a 13-segment binary (`CODE 0` jump table + `CODE 1…12`); the `CODE 0` jump table (358 entries, `jtOffset=0x20`) was parsed so that cross-segment `jsr xxx(a5)` calls resolve to `(segment, offset)`. Tooling: `scratchpad/rez.py` (resource reader) + `scratchpad/code.py` (segment disassembler + JT resolver).

**Verdict — the trigger dates, answered directly:**

- **Christmas:** fires when **month == 12 and day == 25** → shows *STR# 138 #1* = `"Merry Christmas!"`.
- **Alberto Ricci's birthday:** fires when **month == 9 (September) and day == 17** → shows *STR# 138 #2* = `"Today is Alberto Ricci’s birthday!"`.
- **No year comparison** — both fire every year on those calendar dates. There is no separate "was it already shown today" guard in this routine (contrast the Welcome alert's `shown-once` preference); it runs once per launch and will re-appear on every launch during the trigger day.

There is exactly **one** date path in the whole application (a single `SecondsToDate` trap, `$A9C6`, in `CODE 7 @ 0x0884`; no `ReadDateTime`/`DateToSeconds` anywhere), so these two comparisons are the complete easter-egg date logic — nothing else is date-gated.

## Location and call site

- **Routine:** `CODE 7`, file offset **0x086c** (jump-table index 242, `A5+0x7b0`; JT entry raw `08 68 3f 3c 00 07 a9 f0` → seg 7, in-segment offset 0x868 = file 0x86c, `_LoadSeg` segID 7).
- **Called from:** `CODE 2 @ 0x28b0` (`jsr $7b2(a5)`) — its **only** caller. That call sits inside the app's linear **startup/init routine**, immediately after the menu-bar/`InsertMenu` setup block and before the rest of launch init (`0x28ac jsr …(pc)` → `0x28b0` easter-egg → `0x28b4 jsr …(pc)` → …). So the check happens **once at application launch**.

## The code, annotated (`CODE 7`)

```
0086c  link.w   a6,#$fef0
00870  move.l   d7,-(a7)
00872  clr.b   -$10f(a6)          ; a zero-length Str255 (empty string) for later ParamText
00876  move.w  -$56a(a5),-(a7)    ; app resource-file refNum (global)
0087a  _UseResFile ($A998)        ; make sure STR#/ALRT come from the app file
0087c  lea.l   -$10e(a6),a0       ; a0 = &DateTimeRec
00880  move.l  $20c.w,d0          ; d0 = low-mem global Time (seconds since 1904-01-01)
00884  _SecondsToDate ($A9C6)     ; DateTimeRec(-$10e) <- SecondsToDate(d0)
                                   ;   -$10e year  -$10c MONTH  -$10a DAY  -$108 hour ...
00886  cmpi.w  #$0c,-$10c(a6)     ; month == 12 (December)?
0088c  bne.b   $89a
0088e  cmpi.w  #$19,-$10a(a6)     ; day == 25?
00894  bne.b   $89a
00896  moveq   #1,d7              ; -> STR# 138 index 1  "Merry Christmas!"
00898  bra.b   $8ac
0089a  cmpi.w  #$09,-$10c(a6)     ; month == 9 (September)?
008a0  bne.b   $8d8               ; no match -> return (skip alert)
008a2  cmpi.w  #$11,-$10a(a6)     ; day == 17?
008a8  bne.b   $8d8
008aa  moveq   #2,d7              ; -> STR# 138 index 2  "Today is Alberto Ricci's birthday!"
008ac  pea.l   -$100(a6)          ; &Str255 out buffer
008b0  move.w  #$8a,-(a7)         ; strListID = 138  (STR# 138)
008b4  move.w  d7,-(a7)           ; index = 1 or 2
008b6  jsr     $342(a5)           ; GetIndString glue  ->  buffer = the greeting
008ba  pea.l   -$100(a6)          ; ^0 = greeting
008be  pea.l   -$10f(a6)          ; ^1 = "" (the byte cleared at 0x872)
008c2  pea.l   -$10f(a6)          ; ^2 = ""
008c6  pea.l   -$10f(a6)          ; ^3 = ""
008ca  _ParamText ($A98B)
008cc  clr.w   -(a7)
008ce  move.w  #$92,-(a7)         ; ALRT 146
008d2  pea.l   $5e2(a5)           ; filterProc (shared standard filter)
008d6  _NoteAlert ($A987)         ; show the greeting as a Note alert (OK button)
008d8  move.l  -$114(a6),d7       ; (no-match path lands here too)
008dc  unlk    a6
008de  rts
```

### Why the field offsets are certain

`SecondsToDate` fills a `DateTimeRec` (7 words: `year, month, day, hour, minute, second, dayOfWeek`) at the address in `A0`, which is `-$10e(a6)`. Word offsets from there: `month = -$10c`, `day = -$10a` — exactly the two locations the two `cmpi.w` pairs test. The immediates are unambiguous: `$0c`=12, `$19`=25, `$09`=9, `$11`=17.

### The alert

- **STR# 138** (whole list): `[1] "Merry Christmas!"`, `[2] "Today is Alberto Ricci’s birthday!"` — only these two items.
- **ALRT 146** (`$92`) → **DITL 146**: item 1 = OK button (`type 4`), item 2 = static text `"^0\r^1^2^3"` (`type 8`). ParamText loads the greeting into `^0`; `^1/^2/^3` are empty. So the alert is just the one greeting line above an OK button. Rect `(80,50,178,464)`, 4 identical stages.

## Reproduction (web prototype)

At app launch, read the local date; if `(month,day)` is `(12,25)` show "Merry Christmas!", else if `(9,17)` show "Today is Alberto Ricci’s birthday!", each in a Note-style alert with a single OK button. No year check, no once-per-day suppression — it appears on every launch during the trigger day. Uses the **local** system date (low-mem `Time` is local civil time on classic Mac OS), not UTC.

## Provenance / confidence

- **CONFIRMED.** Single, self-contained routine; both immediates and both string indices read directly; the string list and alert resources match the disassembled IDs (138, 146) exactly. Only one date routine exists in the binary, so there is no risk of a second, unrecovered trigger. The birthday date (September 17) is asserted only by the code — not independently verifiable — but the code path is unambiguous.
