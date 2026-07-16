/**
 * AIFF — SPEC §10. The canonical interchange, and for good reason: it is
 * big-endian and signed, which is the model's own layout, so wide samples cross
 * the boundary as a straight copy. It also carries its sample rate as an 80-bit
 * extended, so it "round-trips arbitrary rates" — arbitrary, at least, up to what
 * a Fixed 16.16 can hold once it arrives (§14 item 23).
 *
 * Structure: `FORM` container, `COMM` (channels, frames, bit depth, rate),
 * `SSND` (offset, blockSize, then the interleaved samples).
 *
 * **AIFC is deliberately not supported.** It is AIFF with a compression type, and
 * this app has no decoders — the honest answer is the unknown-format alert rather
 * than a half-read file.
 */

import type { SoundDocument } from "../document.js";
import { frameSizeFor } from "../samples.js";
import { fixedToDouble } from "../units.js";
import { readExtended80, writeExtended80 } from "./extended80.js";
import {
  channelsToPcm,
  documentFromChannels,
  pcmToChannels,
  type DecodedSound,
} from "./pcm.js";

export interface AiffInfo {
  channels: number;
  frames: number;
  bitsPerSample: number;
  /** Hz, as stored in the 80-bit extended. */
  rateHz: number;
  /** Raw interleaved sample bytes. */
  data: Uint8Array;
  /** True for AIFC with a compression type other than `NONE`. */
  compressed: boolean;
  compressionType?: string;
}

function fourCC(bytes: Uint8Array, offset: number): string {
  return String.fromCharCode(
    bytes[offset]!,
    bytes[offset + 1]!,
    bytes[offset + 2]!,
    bytes[offset + 3]!,
  );
}

function readU32BE(bytes: Uint8Array, offset: number): number {
  return (
    ((bytes[offset]! << 24) |
      (bytes[offset + 1]! << 16) |
      (bytes[offset + 2]! << 8) |
      bytes[offset + 3]!) >>>
    0
  );
}

function readU16BE(bytes: Uint8Array, offset: number): number {
  return (bytes[offset]! << 8) | bytes[offset + 1]!;
}

function writeU32BE(bytes: Uint8Array, offset: number, v: number): void {
  bytes[offset] = (v >>> 24) & 0xff;
  bytes[offset + 1] = (v >>> 16) & 0xff;
  bytes[offset + 2] = (v >>> 8) & 0xff;
  bytes[offset + 3] = v & 0xff;
}

function writeU16BE(bytes: Uint8Array, offset: number, v: number): void {
  bytes[offset] = (v >>> 8) & 0xff;
  bytes[offset + 1] = v & 0xff;
}

function writeFourCC(bytes: Uint8Array, offset: number, cc: string): void {
  for (let i = 0; i < 4; i++) bytes[offset + i] = cc.charCodeAt(i);
}

/** Is this an AIFF (or AIFC) file? */
export function isAiff(bytes: Uint8Array): boolean {
  return (
    bytes.length >= 12 &&
    fourCC(bytes, 0) === "FORM" &&
    (fourCC(bytes, 8) === "AIFF" || fourCC(bytes, 8) === "AIFC")
  );
}

/** Parse the chunks. Throws on a malformed file. */
export function parseAiff(bytes: Uint8Array): AiffInfo {
  if (!isAiff(bytes)) throw new Error("not an AIFF file");
  const isAifc = fourCC(bytes, 8) === "AIFC";

  let channels = 0;
  let frames = 0;
  let bitsPerSample = 0;
  let rateHz = 0;
  let data: Uint8Array | null = null;
  let compressionType: string | undefined;

  let p = 12;
  while (p + 8 <= bytes.length) {
    const id = fourCC(bytes, p);
    const size = readU32BE(bytes, p + 4);
    const body = p + 8;

    if (id === "COMM") {
      channels = readU16BE(bytes, body);
      frames = readU32BE(bytes, body + 2);
      bitsPerSample = readU16BE(bytes, body + 6);
      rateHz = readExtended80(bytes, body + 8);
      // AIFC appends a compression type after the 18-byte COMM.
      if (isAifc && size >= 22) compressionType = fourCC(bytes, body + 18);
    } else if (id === "SSND") {
      const offset = readU32BE(bytes, body);
      // blockSize at body+4 is for alignment and is almost always 0.
      const start = body + 8 + offset;
      data = bytes.subarray(start, body + size);
    }

    // Chunks are word-aligned: an odd size is followed by a pad byte.
    p = body + size + (size % 2);
  }

  if (channels === 0 || bitsPerSample === 0) throw new Error("AIFF has no COMM chunk");
  if (!data) throw new Error("AIFF has no SSND chunk");

  const compressed = compressionType !== undefined && compressionType !== "NONE";
  return { channels, frames, bitsPerSample, rateHz, data, compressed, compressionType };
}

/**
 * Decode an AIFF's samples. Stops short of a document because the rate may not
 * be representable — see `DecodedSound`.
 *
 * 8-bit AIFF is **signed**, so it needs the `XOR 0x80` into offset-binary. Wide
 * samples are already big-endian signed — the model's exact layout — so they are
 * copied, not converted.
 */
export function decodeAiff(bytes: Uint8Array): DecodedSound {
  const info = parseAiff(bytes);
  if (info.compressed) {
    throw new Error(`AIFC compression '${info.compressionType}' is not supported`);
  }
  const channels = pcmToChannels(info.data, {
    bitsPerSample: info.bitsPerSample,
    channels: info.channels,
    littleEndian: false,
    eightBitUnsigned: false, // AIFF 8-bit is signed
  });
  return { channels, bps: info.bitsPerSample, rateHz: info.rateHz };
}

/**
 * Read an AIFF into the model. Throws if the rate is beyond a Fixed 16.16 —
 * `readSound` is the entry point that offers to resample instead.
 */
export function readAiff(bytes: Uint8Array, name = "untitled sound"): SoundDocument {
  return documentFromChannels(decodeAiff(bytes), name);
}

/**
 * Write the model as AIFF.
 *
 * Pads unequal channels to the longest — the format has one frame count and no
 * way to express the drift. Callers should have warned first (`exportWouldPad`).
 */
export function writeAiff(doc: SoundDocument): Uint8Array {
  const frameSize = frameSizeFor(doc.bps);
  const pcm = channelsToPcm(doc, { littleEndian: false, eightBitUnsigned: false });
  const frames = Math.floor(doc.maxChSize() / frameSize);

  const commSize = 18;
  const ssndSize = 8 + pcm.length;
  const ssndPad = ssndSize % 2;
  const formSize = 4 + (8 + commSize) + (8 + ssndSize + ssndPad);
  const out = new Uint8Array(8 + formSize);

  writeFourCC(out, 0, "FORM");
  writeU32BE(out, 4, formSize);
  writeFourCC(out, 8, "AIFF");

  writeFourCC(out, 12, "COMM");
  writeU32BE(out, 16, commSize);
  writeU16BE(out, 20, doc.numChans);
  writeU32BE(out, 22, frames);
  writeU16BE(out, 26, doc.bps);
  // The rate goes back out as an 80-bit extended — wider than the Fixed we hold
  // it in, so this direction never loses anything.
  writeExtended80(fixedToDouble(doc.rate), out, 28);

  const ssndAt = 38;
  writeFourCC(out, ssndAt, "SSND");
  writeU32BE(out, ssndAt + 4, ssndSize);
  writeU32BE(out, ssndAt + 8, 0); // offset
  writeU32BE(out, ssndAt + 12, 0); // blockSize
  out.set(pcm, ssndAt + 16);

  return out;
}
