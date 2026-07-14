# Weird Tempo — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 (1964 bytes) from `Effects Folder/Weird Tempo`, disassembled with capstone (M68K). An `EFCP` PowerPC build also exists (2688 bytes) for cross-checking if ever needed. Module: v1.1, ©1994 Alberto Ricci.

**Verdict: Weird Tempo is a zero-crossing-aligned period doubler** — the most naive possible time-stretch: chop the selection into "periods" at falling zero-crossings and write each period **twice**, in place, with no windowing, no interpolation, no overlap-add. Output ≈ 2× the selection. The doubling is exact for periodic signals; on real audio the pseudo-periods are irregular, which is precisely the "really weird" judder.

## Behavior, precisely

1. **On every run**, before doing anything, it shows module error string #3 with a Cancel button: *"Warning: this module is a short hack which may introduce noise. You may try do a Smooth after this to eliminate some noise."* Cancel → `kModCancel`, nothing happens. (Via `ModShowError(modInfo, 3, withCancel=true)`.)
2. Empty selection → whole sound (`selSt=0`, `selEnd=ModMaxRelChSize`), per SDK convention.
3. Per selected channel (independently — so stereo channels segment differently and smear):
   - Clamp selection to channel length, snap to frame boundaries.
   - Grow the channel handle by `selBytes`; shift the tail right by `selBytes` (workspace = exactly 2× selection). Out of memory → error 2 for that channel, continue with the next.
   - Walk the selection sample by sample, tracking only the **sign** (zero counts as positive; 8-bit via `XOR 0x80`, 9–32-bit via the standard big-endian sign-extended read).
   - **Segmentation state machine**: two booleans, `wantPos` and `phase`, both start true. A *hit* fires when the sample's sign matches what's sought (`wantPos` ⇒ sample ≥ 0, else sample < 0); each hit toggles `wantPos` and alternates `phase`. Every **second** hit — i.e. at each sample where the signal goes negative after having been non-negative — ends a segment.
   - At each segment end: `BlockMove` the rest of the workspace right by the segment length, then `BlockMove` the segment into the gap — **the segment now appears twice** — and resume scanning *after* the copy (the copy is never rescanned).
   - The trailing partial segment (no falling crossing after it) is written **once**.
   - Shrink the handle to the true output size (`oldSize − selBytes + written`), update the channel's independent length, and extend `modInfo->selEnd` to cover the output.
4. Progress is checked **every sample** with `canCancel=true`. Cancelling mid-channel still runs the full channel finalization (gap closed, size fixed) — the half-processed result is kept *and made structurally consistent*, then remaining channels are skipped, returning `noErr`. The destructive philosophy, engineered.
5. `doExtend` is **ignored entirely** — Weird Tempo always lengthens the sound, whatever the host preference says.

## Emergent quirks (preserve all of these)

- Segments are delimited by *falling* zero-crossings, with 0 counting as positive; the first segment runs from the selection start to the first strictly negative sample after a non-negative one.
- **Silence, all-positive, or all-negative selections pass through unchanged** (no falling crossing ever fires) — Weird Tempo does nothing to unipolar material.
- Alternating-sign material (noise, high frequencies) produces 1–2-sample segments, each doubled — micro-repeats ≈ the "noise" the warning apologizes for.
- Output length is content-dependent: `2×(sum of complete segments) + leftover`, so "twice as long" is only approximately true.
- Channels are segmented independently → multichannel phase/duration smear.

## Reconstructed C (Ricci-style, drop-in against the SDK)

```c
pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    Handle h;
    Ptr p, endP, segStart;
    unsigned long start, end, size, selBytes, written, segLen, newSize;
    unsigned long samplesToProcess, processed = 0;
    signed long s;
    short frameSize, t, channel;
    Boolean wantPos, phase, hit;
    OSErr pr = kModNoError, result = kModCancel;

    UseResFile(mi->refNum);
    if ((*glue->ModShowError)(mi, 3, true) == kModCancel)   /* "short hack" warning */
        return kModCancel;

    progressString = (StringHandle)Get1Resource('STR ', 128);
    if (!progressString) return kModCouldntLoadMyRes;
    (*glue->ModSetupProgress)(mi, &t, progressString);
    ReleaseResource((Handle)progressString);

    if (mi->selSt == mi->selEnd)
        { mi->selSt = 0L; (*glue->ModMaxRelChSize)(&mi, &mi->selEnd); }

    frameSize = mi->bps/8 + (mi->bps%8 != 0);
    samplesToProcess = (*glue->GetBytesToProcess)(mi) / frameSize;

    for (channel = mi->firstSelCh-1; channel < mi->lastSelCh; channel++)
    {
        start = mi->selSt;  end = mi->selEnd;
        size = (*mi->hands)[channel].size;
        if (start > size) start = size;
        if (end   > size) end   = size;
        start -= start % frameSize;  end -= end % frameSize;
        if (start == end) continue;

        h = (*mi->hands)[channel].chan;
        selBytes = end - start;
        SetHandleSize(h, size + selBytes);              /* workspace = 2x selection */
        if (MemError()) { result = kModNotEnoughMemory; continue; }
        BlockMove(*h + end, *h + end + selBytes, size - end);   /* park the tail */
        HLock(h);

        p = *h + start;  endP = *h + end;  segStart = p;
        written = 0;  wantPos = phase = true;

        for (; p < endP; p += frameSize, processed++)
        {
            s = (frameSize == 1) ? GetSample8(p) : GetSample16(p, mi->bps);
            hit = wantPos ? (s >= 0) : (s < 0);
            if (hit)
            {
                wantPos = !wantPos;
                if (phase) phase = false;
                else                                    /* falling crossing: seg ends */
                {
                    phase = true;
                    segLen = p - segStart;
                    written += 2*segLen;
                    BlockMove(p, p + segLen, 2*selBytes - written);  /* open gap  */
                    BlockMove(segStart, p, segLen);                  /* dup seg   */
                    p += segLen;  endP += segLen;  segStart = p;     /* skip copy */
                }
            }
            if (t && (pr = (*glue->ModShowProgress)(processed, samplesToProcess, &t, true)))
                break;
        }
        written += endP - segStart;                     /* leftover, played once */
        HUnlock(h);

        if (2*selBytes > written)                        /* pull the tail back in */
            BlockMove(*h + end + selBytes, *h + start + written, 2*selBytes - written);
        newSize = size - selBytes + written;
        SetHandleSize(h, newSize);
        (*mi->hands)[channel].size = newSize;           /* channels drift apart  */
        if (start + written > mi->selEnd) mi->selEnd = start + written;
        result = kModNoError;
        if (pr) break;                                   /* cancel: keep the damage */
    }
    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points: `settings()` returns `kModHasNoSettings`; `GetModSettings` returns nil prefs; `about()` is `ModNoAbout.c` (host shows the standard about window from `TEXT`/`STR `/`vers` resources).

## Verification

A Python transcription of the byte-level BlockMove choreography (`weird_tempo_ref.py`) was checked against an independent "each segment twice, leftover once" model: identical output on sine, irregular multi-period bursts, and alternating-sign signals. Sanity results: 10×20-sample sine periods → 391 samples out (first segment 11, nine full periods doubled, 9-sample tail once — exactly as the state machine predicts); unipolar and silent inputs unchanged. Bit-exactness against the original would additionally need an emulator run as oracle, but the control flow and pointer arithmetic close perfectly (every BlockMove lands exactly at the workspace boundary).

## Porting notes (web / Eurorack)

- Trivially portable: one pass, two memmoves per segment, integer-only, no settings UI.
- Keep: the every-run warning dialog with Cancel; sign convention (0 = positive); falling-crossing boundaries; leftover-once; per-channel independence; selEnd extension; cancel-keeps-partial.
- For the Eurorack module this is a natural one-button effect ("×2 WEIRD"): no parameters by design.
