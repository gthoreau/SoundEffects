#include "Import.h"

#include "SampleFormat.h"

#include <algorithm>
#include <cmath>

namespace se {

std::unique_ptr<SoundDocument> makeDocumentFromFloat (
    const std::vector<std::vector<float>>& channels,
    double rateHz,
    int bps,
    const std::string& name)
{
    DocumentInit init;
    init.name = name;
    init.rateHz = rateHz;
    init.bps = bps;
    init.numTracks = 0;            // we append tracks ourselves below
    auto doc = std::make_unique<SoundDocument> (init);

    const int frameSize = frameSizeFor (bps);
    const int bits = std::min (frameSize, 4) * 8;
    const double divisor = std::pow (2.0, bits - 1);
    const int32_t lo = -static_cast<int32_t> (divisor);          // -2^(bits-1)
    const int32_t hi =  static_cast<int32_t> (divisor) - 1;      //  2^(bits-1)-1

    for (const auto& src : channels)
    {
        Track& t = doc->addTrack();                              // append, silent
        t.data.assign (src.size() * static_cast<size_t> (frameSize), silentByte (bps));

        for (size_t i = 0; i < src.size(); ++i)
        {
            const double f = std::clamp (static_cast<double> (src[i]), -1.0, 1.0);
            const int32_t v = clampSample (static_cast<int32_t> (std::lround (f * divisor)), lo, hi);
            setSample (t.data.data(), i * static_cast<size_t> (frameSize), v, bps);
        }
    }

    if (doc->numTracks() > 0)
    {
        doc->firstSelTrack = 1;
        doc->lastSelTrack = doc->numTracks();
    }
    return doc;
}

} // namespace se
