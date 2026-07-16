/**
 * PCM conversion between interchange formats and the document model — SPEC §2.2.
 *
 * ## The symmetry that makes this short
 *
 * The model stores samples **packed big-endian, signed, right-aligned**, except
 * at 8 bits and under where it stores **offset-binary** (`signed = stored ^ 0x80`).
 * That is a Mac convention, and it lands in a funny place relative to the two
 * formats:
 *
 * | | 8-bit | 9–32-bit |
 * |---|---|---|
 * | **AIFF** (big-endian) | **signed** → needs `XOR 0x80` | **signed BE** → *straight copy* |
 * | **WAV** (little-endian) | **unsigned** → *straight copy* | signed LE → needs a byte swap |
 *
 * So each format is free in exactly the half the other one isn't. AIFF's wide
 * samples are already in the model's layout byte-for-byte — which is precisely
 * why §10 calls AIFF the canonical interchange — while WAV's 8-bit unsigned *is*
 * offset-binary and drops straight in.
 *
 * ## What cannot round-trip
 *
 * Both formats **interleave** channels and give them one shared length. The model's
 * channels are independent buffers with **their own lengths** (§2.1) — the core
 * weirdness of the whole app. Export therefore pads to the longest channel, and a
 * document whose tracks have drifted apart comes back from disk aligned. See
 * `exportWouldPad`.
 */

import { SoundDocument } from "../document.js";
import { frameSizeFor, silentByte } from "../samples.js";
import { doubleToFixed, MAX_FIXED_RATE_HZ } from "../units.js";

/** How samples are laid out in a file. */
export interface PcmLayout {
  bitsPerSample: number;
  channels: number;
  littleEndian: boolean;
  /** 8-bit is unsigned in WAV, signed in AIFF. Ignored above 8 bits. */
  eightBitUnsigned: boolean;
}

/**
 * De-interleave a file's PCM into the model's per-channel packed buffers.
 *
 * @param data the raw sample bytes, interleaved
 */
export function pcmToChannels(data: Uint8Array, layout: PcmLayout): Uint8Array[] {
  const frameSize = frameSizeFor(layout.bitsPerSample);
  const stride = frameSize * layout.channels;
  const frames = Math.floor(data.length / stride);
  const out: Uint8Array[] = [];

  for (let c = 0; c < layout.channels; c++) {
    const buf = new Uint8Array(frames * frameSize);
    for (let f = 0; f < frames; f++) {
      const src = f * stride + c * frameSize;
      const dst = f * frameSize;
      if (frameSize === 1) {
        // 8-bit. The model wants offset-binary.
        const b = data[src]!;
        buf[dst] = layout.eightBitUnsigned ? b : (b ^ 0x80) & 0xff;
      } else if (layout.littleEndian) {
        // Reverse into big-endian.
        for (let b = 0; b < frameSize; b++) buf[dst + b] = data[src + frameSize - 1 - b]!;
      } else {
        for (let b = 0; b < frameSize; b++) buf[dst + b] = data[src + b]!;
      }
    }
    out.push(buf);
  }
  return out;
}

/**
 * Interleave the model's channels back into a file's PCM.
 *
 * **Pads every channel to the longest with silence.** This is where the app's
 * unequal-length channels die: the format has one length and that is that.
 */
export function channelsToPcm(
  doc: SoundDocument,
  layout: Omit<PcmLayout, "channels" | "bitsPerSample">,
): Uint8Array {
  const frameSize = frameSizeFor(doc.bps);
  const channels = doc.numChans;
  const stride = frameSize * channels;
  const frames = Math.floor(doc.maxChSize() / frameSize);
  const out = new Uint8Array(frames * stride);
  const silent = silentByte(doc.bps);

  for (let c = 0; c < channels; c++) {
    const src = doc.channels[c]!.data;
    for (let f = 0; f < frames; f++) {
      const s = f * frameSize;
      const d = f * stride + c * frameSize;
      // Past this channel's end: silence. The drift is lost here, unavoidably.
      const have = s + frameSize <= src.length;
      if (frameSize === 1) {
        const b = have ? src[s]! : silent;
        out[d] = layout.eightBitUnsigned ? b : (b ^ 0x80) & 0xff;
      } else if (layout.littleEndian) {
        for (let b = 0; b < frameSize; b++) {
          out[d + b] = have ? src[s + frameSize - 1 - b]! : 0;
        }
      } else {
        for (let b = 0; b < frameSize; b++) out[d + b] = have ? src[s + b]! : 0;
      }
    }
  }
  return out;
}

/**
 * Would exporting this document lose channel-length information?
 *
 * True when the channels have drifted apart — which, in this app, is not an edge
 * case but the expected result of clearing on one channel (§5). Worth telling the
 * user before the file is written, because re-opening it will not give the drift
 * back.
 */
export function exportWouldPad(doc: SoundDocument): boolean {
  if (doc.numChans < 2) return false;
  const first = doc.channels[0]!.data.length;
  return doc.channels.some((c) => c.data.length !== first);
}

/**
 * Convert IEEE float samples (32- or 64-bit) to integer at the requested depth.
 *
 * **Not a convenience — an identity constraint.** Every effect in this app is
 * integer (`s * gain / 100`, truncating divides, saturating clamps); §2.2 calls
 * that rounding "part of the sound". A float sample has nowhere to live in the
 * model, so a float file becomes integer at the door or not at all.
 *
 * Truncating, like everything else here.
 */
export function floatToIntegerPcm(
  data: Uint8Array,
  floatBits: 32 | 64,
  targetBps: number,
  channels: number,
  littleEndian: boolean,
): Uint8Array {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const srcSize = floatBits / 8;
  const frames = Math.floor(data.length / (srcSize * channels));
  const frameSize = frameSizeFor(targetBps);
  const divisor = Math.pow(2, Math.min(frameSize, 4) * 8 - 1);
  const max = divisor - 1;
  const min = -divisor;
  const out = new Uint8Array(frames * channels * frameSize);

  for (let i = 0; i < frames * channels; i++) {
    const f =
      floatBits === 32
        ? view.getFloat32(i * srcSize, littleEndian)
        : view.getFloat64(i * srcSize, littleEndian);
    let v = Math.trunc(f * divisor);
    if (v < min) v = min;
    else if (v > max) v = max;
    // Write big-endian signed, right-aligned — the model's own layout.
    const d = i * frameSize;
    if (frameSize === 1) {
      out[d] = (v ^ 0x80) & 0xff;
    } else {
      for (let b = 0; b < frameSize; b++) out[d + b] = (v >>> ((frameSize - 1 - b) * 8)) & 0xff;
    }
  }
  return out;
}

/**
 * A decoded file, before any policy has been applied to it.
 *
 * The decoders stop here rather than handing back a `SoundDocument` so that
 * policy — naming, optional resampling, the rate ceiling — lives in one place
 * (`formats/index.ts`) instead of being duplicated per format.
 *
 * (This split originally existed because the rate ceiling was 65.5 kHz and a
 * 96 kHz file had no valid document to become. Widening the rate in `units.ts`
 * removed that reason; the separation earns its keep on its own merits.)
 */
export interface DecodedSound {
  /** De-interleaved, already in the model's packed layout. */
  channels: Uint8Array[];
  bps: number;
  /** Hz, exactly as the file states it — possibly beyond what a Fixed can hold. */
  rateHz: number;
}

/**
 * Build a document from decoded channels.
 *
 * `rateHz` must already be within the Fixed 16.16 ceiling; callers are expected
 * to have resolved that. Throws rather than wrapping silently, because a wrapped
 * rate is indistinguishable from a deliberate header lie (§5) and would be a
 * nightmare to trace back to an import.
 */
export function documentFromChannels(
  decoded: DecodedSound,
  name: string,
): SoundDocument {
  if (decoded.rateHz > MAX_FIXED_RATE_HZ) {
    throw new Error(
      `${decoded.rateHz} Hz cannot be held in a Fixed 16.16 rate (max ${MAX_FIXED_RATE_HZ})`,
    );
  }
  const doc = new SoundDocument({
    name,
    bps: decoded.bps,
    rate: doubleToFixed(decoded.rateHz),
    numChans: 0,
  });
  for (const data of decoded.channels) {
    doc.addChannel();
    doc.channels[doc.numChans - 1]!.data = data;
  }
  if (doc.numChans === 0) doc.addChannel();
  doc.firstSelCh = 1;
  doc.lastSelCh = doc.numChans;
  return doc;
}
