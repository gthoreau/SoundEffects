#include "Export.h"

#include "Engine.h"
#include "Project.h"
#include "SampleFormat.h"
#include "Wav.h"

#include <algorithm>
#include <cmath>

namespace se {

Mixdown renderMixdown (const WindowState& sound)
{
    const int bps = sound.bps;
    const size_t fs = static_cast<size_t> (std::max (1, frameSizeFor (bps)));
    size_t frames = 0;
    for (const auto& t : sound.tracks) frames = std::max (frames, t.data.size() / fs);

    // A sample is read against its container's full scale, and written in steps of
    // the Sound's depth (a 12-bit Sound keeps its low four bits at zero).
    const int bits = std::min (static_cast<int> (fs), 4) * 8;
    const double full = static_cast<double> (uint64_t (1) << (bits - 1));
    const double step = static_cast<double> (uint64_t (1) << std::max (0, bits - std::max (1, bps)));

    const auto balance = Engine::balanceGains (sound.balance);
    const double gain = std::isfinite (sound.gain) ? std::max (0.0f, sound.gain) : 1.0f;

    Mixdown out;
    out.left.data.assign (frames * fs, silentByte (bps));
    out.right.data.assign (frames * fs, silentByte (bps));
    out.left.outRight = false;
    out.right.outLeft = false;

    const auto write = [&] (std::vector<uint8_t>& data, size_t frame, double v)
    {
        const double snapped = std::round (v * full / step) * step;
        const double clipped = std::clamp (snapped, -full, full - step);
        setSample (data.data(), frame * fs, static_cast<int32_t> (clipped), bps);
    };

    for (size_t i = 0; i < frames; ++i)
    {
        double left = 0.0, right = 0.0;
        for (const auto& t : sound.tracks)
        {
            if ((i + 1) * fs > t.data.size() || t.volume <= 0) continue;
            const double s = getSample (t.data.data(), i * fs, bps) / full
                             * (t.volume / static_cast<double> (VOLUME_UNITY));
            if (t.outLeft)  left += s;
            if (t.outRight) right += s;
        }
        write (out.left.data,  i, std::clamp (left,  -1.0, 1.0) * gain * balance.left);
        write (out.right.data, i, std::clamp (right, -1.0, 1.0) * gain * balance.right);
    }
    return out;
}

void encodeMixdownWav (const Mixdown& mix, double rateHz, int bps, std::vector<uint8_t>& out)
{
    encodeStereoWav (mix.left, mix.right, rateHz, bps, out);
}

std::vector<Stem> splitStems (const WindowState& sound)
{
    std::vector<Stem> out;
    out.reserve (sound.tracks.size());
    for (size_t t = 0; t < sound.tracks.size(); ++t)
    {
        Stem s;
        s.fileName = safeFileName (sound.name) + " - track " + std::to_string (t + 1) + ".wav";
        encodeTrackWav (sound.tracks[t], sound.rateHz, sound.bps, s.wav);
        out.push_back (std::move (s));
    }
    return out;
}

} // namespace se
