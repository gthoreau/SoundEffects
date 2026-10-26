// RecordConvert — turns what the audio thread captured (float, at the DEVICE's
// rate) into samples of the Sound it is recorded into (its declared rate and bit
// depth), as a stream: blocks in, blocks out, so a recording can be converted as
// it arrives. No JUCE.
//
//  - Raw   (the default): the 0.9.2 resamplers' arithmetic. Rate conversion is
//          zero-order hold with no filtering (output k is the input sample at
//          floor(k * inRate / outRate), exactly the index `resampleSoundRate`
//          picks), so aliasing is part of the sound; bit conversion is plain
//          truncation (floor: the low bits are dropped, as `resampleSoundBits`
//          drops bytes). No dither.
//  - Clean: band-limited. A windowed-sinc (Kaiser) interpolator low-passes at the
//          output's Nyquist while it resamples, then TPDF dither of +-1 LSB at the
//          target depth is added before rounding.
//
// Output samples are in the Sound's own representation: signed, left-aligned in
// the frame the depth occupies (a 12-bit Sound is stored in 2 bytes with the low
// 4 bits zero), ready for `setSample`.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace se {

enum class ConvertMode : uint8_t { raw, clean };

class RecordConverter
{
public:
    RecordConverter (double inRate, double outRate, int outBps, ConvertMode mode, uint32_t seed = 1u);

    /** Convert `n` input frames; appends the samples that became ready to outL/outR
        (equal counts). Clean holds back about half its window: see flush(). */
    void process (const float* l, const float* r, size_t n,
                  std::vector<int32_t>& outL, std::vector<int32_t>& outR);

    /** The input has ended: emit what the held-back window still owes (zero-padded)
        so the output covers the whole input. The stream then starts afresh. */
    void flush (std::vector<int32_t>& outL, std::vector<int32_t>& outR);

    /** Output frames the input seen so far has produced, once flushed. */
    static size_t outputLength (size_t inFrames, double inRate, double outRate) noexcept;

    double inputRate() const noexcept { return inRate; }
    double outputRate() const noexcept { return outRate; }
    int    depth() const noexcept { return bps; }

private:
    int32_t quantise (double x, bool dither) noexcept;
    double  tpdf() noexcept;
    void    emit (double l, double r, std::vector<int32_t>& outL, std::vector<int32_t>& outR);
    void    runClean (std::vector<int32_t>& outL, std::vector<int32_t>& outR, bool final);
    void    resetStream();

    double inRate, outRate, ratio;   // ratio = input frames per output frame
    int    bps, frameBits;
    ConvertMode mode;
    uint32_t seed0, rng;

    double next = 0.0;               // the next output's position, in absolute input frames
    size_t outIdx = 0;               // outputs emitted: next = outIdx * ratio, with no drift
    size_t consumed = 0;             // input frames seen (absolute index of the next one)

    // Clean: the kernel as a table over [-half, +half] at 1/kRes steps, and the
    // history of input still reachable by the next output.
    static constexpr int kRes = 128;
    double half = 0.0;
    std::vector<float> table;
    std::vector<float> histL, histR;
    size_t histBase = 0;             // absolute index of histL[0]
};

} // namespace se
