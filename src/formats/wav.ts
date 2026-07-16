/**
 * WAV — SPEC §10's "Web prototype should also import WAV for convenience".
 *
 * The mirror image of AIFF: little-endian and, at 8 bits, **unsigned**. So 8-bit
 * WAV *is* the model's offset-binary and drops in without a touch, while wide
 * samples need a byte swap. Each format is free in the half the other isn't.
 *
 * Structure: `RIFF` container, `fmt ` (format tag, channels, rate, bit depth),
 * `data`.
 *
 * Handles `WAVE_FORMAT_EXTENSIBLE` (0xFFFE) by reading the real format tag out of
 * its sub-format GUID — modern encoders emit it routinely for anything above
 * stereo or 16-bit, so refusing it would reject a lot of ordinary files.
 */

import type { SoundDocument } from "../document.js";
import { frameSizeFor } from "../samples.js";
import { fixedToDouble } from "../units.js";
import {
  channelsToPcm,
  documentFromChannels,
  floatToIntegerPcm,
  pcmToChannels,
  type DecodedSound,
} from "./pcm.js";

/** `fmt ` format tags. */
export const WaveFormat = {
  pcm: 0x0001,
  ieeeFloat: 0x0003,
  extensible: 0xfffe,
} as const;

export interface WavInfo {
  formatTag: number;
  channels: number;
  sampleRate: number;
  bitsPerSample: number;
  data: Uint8Array;
}

function fourCC(bytes: Uint8Array, offset: number): string {
  return String.fromCharCode(
    bytes[offset]!,
    bytes[offset + 1]!,
    bytes[offset + 2]!,
    bytes[offset + 3]!,
  );
}

function readU32LE(bytes: Uint8Array, offset: number): number {
  return (
    ((bytes[offset]! | (bytes[offset + 1]! << 8) | (bytes[offset + 2]! << 16)) >>> 0) +
    bytes[offset + 3]! * 0x1000000
  );
}

function readU16LE(bytes: Uint8Array, offset: number): number {
  return bytes[offset]! | (bytes[offset + 1]! << 8);
}

function writeU32LE(bytes: Uint8Array, offset: number, v: number): void {
  bytes[offset] = v & 0xff;
  bytes[offset + 1] = (v >>> 8) & 0xff;
  bytes[offset + 2] = (v >>> 16) & 0xff;
  bytes[offset + 3] = (v >>> 24) & 0xff;
}

function writeU16LE(bytes: Uint8Array, offset: number, v: number): void {
  bytes[offset] = v & 0xff;
  bytes[offset + 1] = (v >>> 8) & 0xff;
}

function writeFourCC(bytes: Uint8Array, offset: number, cc: string): void {
  for (let i = 0; i < 4; i++) bytes[offset + i] = cc.charCodeAt(i);
}

export function isWav(bytes: Uint8Array): boolean {
  return bytes.length >= 12 && fourCC(bytes, 0) === "RIFF" && fourCC(bytes, 8) === "WAVE";
}

export function parseWav(bytes: Uint8Array): WavInfo {
  if (!isWav(bytes)) throw new Error("not a WAV file");

  let formatTag = 0;
  let channels = 0;
  let sampleRate = 0;
  let bitsPerSample = 0;
  let data: Uint8Array | null = null;

  let p = 12;
  while (p + 8 <= bytes.length) {
    const id = fourCC(bytes, p);
    const size = readU32LE(bytes, p + 4);
    const body = p + 8;

    if (id === "fmt ") {
      formatTag = readU16LE(bytes, body);
      channels = readU16LE(bytes, body + 2);
      sampleRate = readU32LE(bytes, body + 4);
      bitsPerSample = readU16LE(bytes, body + 14);
      // WAVE_FORMAT_EXTENSIBLE hides the real tag in the first two bytes of its
      // sub-format GUID.
      if (formatTag === WaveFormat.extensible && size >= 40) {
        formatTag = readU16LE(bytes, body + 24);
      }
    } else if (id === "data") {
      data = bytes.subarray(body, Math.min(body + size, bytes.length));
    }

    p = body + size + (size % 2); // word-aligned, like RIFF's ancestor
  }

  if (channels === 0 || bitsPerSample === 0) throw new Error("WAV has no fmt chunk");
  if (!data) throw new Error("WAV has no data chunk");
  return { formatTag, channels, sampleRate, bitsPerSample, data };
}

/**
 * Decode a WAV's samples. Stops short of a document because the rate may not be
 * representable — see `DecodedSound`.
 *
 * **32/64-bit float is converted to integer here** — the model is integer-only
 * and that is the identity, not a limitation to work around (§2.2: the rounding
 * "is part of the sound"). Float files land at 32-bit integer, the widest the
 * model holds.
 */
export function decodeWav(bytes: Uint8Array): DecodedSound {
  const info = parseWav(bytes);

  if (info.formatTag === WaveFormat.ieeeFloat) {
    if (info.bitsPerSample !== 32 && info.bitsPerSample !== 64) {
      throw new Error(`unsupported float width ${info.bitsPerSample}`);
    }
    const targetBps = 32;
    const pcm = floatToIntegerPcm(
      info.data,
      info.bitsPerSample as 32 | 64,
      targetBps,
      info.channels,
      true,
    );
    // floatToIntegerPcm already emits big-endian signed — the model's layout.
    const channels = pcmToChannels(pcm, {
      bitsPerSample: targetBps,
      channels: info.channels,
      littleEndian: false,
      eightBitUnsigned: false,
    });
    return { channels, bps: targetBps, rateHz: info.sampleRate };
  }

  if (info.formatTag !== WaveFormat.pcm) {
    throw new Error(`unsupported WAV format tag 0x${info.formatTag.toString(16)}`);
  }

  const channels = pcmToChannels(info.data, {
    bitsPerSample: info.bitsPerSample,
    channels: info.channels,
    littleEndian: true,
    eightBitUnsigned: true, // WAV 8-bit is unsigned — already offset-binary
  });
  return { channels, bps: info.bitsPerSample, rateHz: info.sampleRate };
}

/**
 * Read a WAV into the model. Throws if the rate is beyond a Fixed 16.16 —
 * `readSound` is the entry point that offers to resample instead.
 */
export function readWav(bytes: Uint8Array, name = "untitled sound"): SoundDocument {
  return documentFromChannels(decodeWav(bytes), name);
}

/**
 * Write the model as WAV.
 *
 * The sample rate goes out as an **integer** Hz — WAV's `fmt ` has no room for a
 * fraction. So a document at the Mac's 22254.5454 Hz exports as 22254, and its
 * pitch shifts by 24 parts per million on the way out. AIFF's 80-bit extended has
 * no such problem, which is why §10 makes AIFF the canonical interchange and WAV
 * "for convenience".
 */
export function writeWav(doc: SoundDocument): Uint8Array {
  const frameSize = frameSizeFor(doc.bps);
  const pcm = channelsToPcm(doc, { littleEndian: true, eightBitUnsigned: true });
  const rate = Math.round(fixedToDouble(doc.rate));
  const blockAlign = frameSize * doc.numChans;

  const fmtSize = 16;
  const dataPad = pcm.length % 2;
  const riffSize = 4 + (8 + fmtSize) + (8 + pcm.length + dataPad);
  const out = new Uint8Array(8 + riffSize);

  writeFourCC(out, 0, "RIFF");
  writeU32LE(out, 4, riffSize);
  writeFourCC(out, 8, "WAVE");

  writeFourCC(out, 12, "fmt ");
  writeU32LE(out, 16, fmtSize);
  writeU16LE(out, 20, WaveFormat.pcm);
  writeU16LE(out, 22, doc.numChans);
  writeU32LE(out, 24, rate);
  writeU32LE(out, 28, rate * blockAlign); // byte rate
  writeU16LE(out, 32, blockAlign);
  // WAV has no concept of odd bit depths: a 12-bit document declares 16, which is
  // true of its container even though the samples only use 12 bits of it (§2.2).
  writeU16LE(out, 34, Math.min(frameSize, 4) * 8);

  writeFourCC(out, 36, "data");
  writeU32LE(out, 40, pcm.length);
  out.set(pcm, 44);

  return out;
}

/** Does exporting to WAV lose the rate's fraction? */
export function wavRateLoss(doc: SoundDocument): number {
  const exact = fixedToDouble(doc.rate);
  return Math.abs(exact - Math.round(exact));
}
