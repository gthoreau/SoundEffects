# Noise — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 (1394 bytes) from `Effects Folder/Noise`, disassembled with capstone (M68K). 68k only. Module v1.1, ©1994 Alberto Ricci. No settings (`kModHasNoSettings`, confirmed by `STR# 1000` #1: *"The Noise effect does not have any options to set."*).

**Verdict: Noise *replaces* the selection with the Macintosh Toolbox's own `Random()` trap output, blasted 16 bits at a time regardless of the sound's actual bit depth.** It does not add to the existing signal, and it does not respect `frameSize`/channel-sample boundaries while writing — it just fills the byte range with word-sized pseudorandom values, with single-byte fixups at the two ends if the range starts or ends on an odd address.

```
for each 16-bit-aligned position p in [start, end):
    *(short*)p = Random()          /* REPLACE, not add */
```

`Random()` is the classic Mac OS trap (`0xA861`) — the same additive/linear congruential 16-bit generator every other System 7-era app used for shuffles, sprite jitter, etc. Noise does not seed or otherwise touch it beyond calling it repeatedly; whatever `qd.randSeed` state the app is in at the time is what you get (the SDK does expose `ModInitRandSeed` in `GlueRec` at offset `0x46`, for modules that want a fresh seed — Noise's code never calls it).

## Behavior, precisely

1. `UseResFile`, `Get1Resource('STR ',128)` → progress string *"Makin' noise…"*, `ModSetupProgress`, `ReleaseResource`. Empty selection ⇒ whole sound (`ModMaxRelChSize`), same convention as every other module.
2. No settings struct touched at all — the message dispatcher's "Mrun" case for Noise calls a trivial stub that just clears a result field before falling into the real routine (there's nothing to load).
3. Per selected channel, independently: clamp `start`/`end` to the channel's length, frame-align both (this computed `frameSize` is used **only** for the progress-bar denominator, `GetBytesToProcess(mi)/frameSize` — it plays no role in the fill loop itself).
4. `HLock` the channel handle. Compute `endPtr = (chanBase+end) & ~1` — the destination range's upper bound rounded **down** to an even address.
5. **Leading byte fixup:** if `chanBase+start` is odd, write one `Random()`-derived byte there (low byte of a 16-bit `Random()` call) and advance to the next (even) address.
6. **Main loop:** step by 2 bytes; each iteration calls `Random()` and stores the full 16-bit result directly (no read-modify-add, no XOR-0x80 conversion, no sign handling — a raw word store). Progress is polled every sample via `ModShowProgress(processed, total, &t, false)` — **`canCancel` is hard-coded `false`**, same non-cancellable convention as Robotize and Echo/Delay.
7. **Trailing byte fixup:** after the loop, if the *true* unmasked end (`chanBase+end`) isn't even (i.e. the selection length parity left one byte unwritten), one more `Random()`-derived byte is written at `chanBase+end−1`.
8. `HUnlock`. Selection length and channel count are never changed — pure in-place overwrite, no `doExtend` interaction at all (there's nothing to extend).

## Why word-at-a-time regardless of bit depth

This is the one genuinely surprising find: every other module in this codebase (Robotize, Weird Tempo, Echo, Delay, and the SDK's own `Gain.c`) is careful to step by `frameSize` and use `GetSample8`/`GetSample16` so it respects the sound's actual sample format. Noise doesn't bother — because it doesn't need to. Random bytes are random bytes no matter how you group them into "samples" afterward; the only correctness requirement is not to write past the selection, which the odd-leading/odd-trailing byte fixups handle regardless of `bps`. Writing 16 bits at a time instead of 8 is a straightforward speed win (half the trap calls and loop iterations for the same byte count) with no audible or correctness cost, made possible entirely because Noise doesn't need per-sample semantics.

## Reconstructed C (Ricci-style, drop-in against the SDK)

```c
pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    Handle channelHand;
    Ptr chanBase, startPtr, endPtr;
    unsigned long start, end, channelSize;
    unsigned long samplesToProcess, processedSamples = 0;
    short frameSize, t, channel;
    OSErr progressResult = kModNoError, result = kModCancel;

    UseResFile(mi->refNum);
    progressString = (StringHandle)Get1Resource('STR ', 128);
    if (!progressString) return kModCouldntLoadMyRes;
    (*glue->ModSetupProgress)(mi, &t, progressString);
    ReleaseResource((Handle)progressString);

    if (mi->selSt == mi->selEnd)
        { mi->selSt = 0L; (*glue->ModMaxRelChSize)(&mi, &mi->selEnd); }

    frameSize = mi->bps/8 + (mi->bps%8 != 0);            /* only feeds the progress denominator */
    samplesToProcess = (*glue->GetBytesToProcess)(mi) / frameSize;

    for (channel = mi->firstSelCh-1; channel < mi->lastSelCh; channel++)
    {
        start = mi->selSt;  end = mi->selEnd;
        channelSize = (*mi->hands)[channel].size;
        if (start > channelSize) start = channelSize;
        if (end   > channelSize) end   = channelSize;
        start -= start % frameSize;  end -= end % frameSize;
        if (start == end) continue;

        channelHand = (*mi->hands)[channel].chan;
        HLock(channelHand);
        chanBase = *channelHand;
        startPtr = chanBase + start;
        endPtr   = (Ptr)((unsigned long)(chanBase + end) & ~1UL);

        if ((unsigned long)startPtr & 1)
        {
            *(unsigned char *)startPtr = (unsigned char)Random();
            startPtr++;
        }

        for (; startPtr < endPtr; startPtr += 2, processedSamples++)
        {
            *(short *)startPtr = Random();                 /* replace, 16 bits, ignores bps */
            if (t && (progressResult = (*glue->ModShowProgress)(processedSamples, samplesToProcess, &t, false)))
                break;
        }

        if (!progressResult && (unsigned long)(chanBase + end) != (unsigned long)endPtr)
            *(unsigned char *)(chanBase + end - 1) = (unsigned char)Random();

        HUnlock(channelHand);
        result = kModNoError;
        if (progressResult) break;
    }

    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points: `settings()` returns `kModHasNoSettings` with the string *"The Noise effect does not have any options to set."*; `about()` is `ModNoAbout.c`.

## Porting notes (web / Eurorack)

- Replace, don't add — Noise **overwrites** the selection; it is not a dither/hiss layer mixed onto existing audio (that's what Dither's percentage-based noise-add is for, per SPEC §8 item 6).
- The exact RNG doesn't matter for a faithful re-creation (any decent PRNG reproduces the character — full-range, uncorrelated 16-bit white noise, replacing rather than mixing), but a Eurorack build gets this essentially for free: white noise generation via LFSR or similar is trivial in hardware, no lookup or shaping needed.
- No parameters by design — for the Eurorack module this is a natural momentary/gate-triggered "NOISE" button on the destructive sampler, matching Robotize/Weird Tempo's one-button philosophy.
- Non-cancellable progress, consistent with Robotize and Echo/Delay.
