// Import — build a SoundDocument from decoded float audio (one vector per Track).
//
// This is the bridge for "load a sound into a Window": a host decodes a file to
// planar floats (e.g. via juce::AudioFormatManager) and hands the channels here;
// we pack them into the document's byte format at `bps`. No JUCE — pure engine,
// so it is testable without a file or a device.
//
// NB: for LOADING external files this is the pragmatic path. Byte-accurate
// AIFF/WAV parsing (offset-binary, extended-80 rate) from the TS reference is a
// separate concern for save/round-trip fidelity, ported later.

#pragma once

#include "SoundDocument.h"

#include <memory>
#include <string>
#include <vector>

namespace se {

/**
 * Pack planar float channels [-1, 1] into a new document at `bps`. Each input
 * vector becomes one Track (independent length preserved). Values are scaled by
 * 2^(bits-1) and clamped to the sample range, matching the Mixer's float divisor.
 */
std::unique_ptr<SoundDocument> makeDocumentFromFloat (
    const std::vector<std::vector<float>>& channels,
    double rateHz,
    int bps,
    const std::string& name);

} // namespace se
