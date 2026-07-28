#include "FixtureFile.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace se::fx {

namespace {

/**
 * A cursor over the fixture bytes. Every read is bounds-checked: a truncated
 * fixture must fail loudly at the point of truncation, not produce a plausible
 * half-document that the comparison then blames on the DSP.
 */
class Reader
{
public:
    Reader (const std::vector<uint8_t>& bytes, std::string name)
        : data (bytes), fileName (std::move (name)) {}

    void need (size_t n) const
    {
        if (pos + n > data.size())
            fail ("unexpected end of file");
    }

    uint8_t u8()
    {
        need (1);
        return data[pos++];
    }

    uint32_t u32()
    {
        need (4);
        uint32_t v = 0;
        std::memcpy (&v, data.data() + pos, 4);   // little-endian, as written
        pos += 4;
        return v;
    }

    int32_t i32()
    {
        int32_t v = 0;
        const uint32_t raw = u32();
        std::memcpy (&v, &raw, 4);
        return v;
    }

    int64_t i64()
    {
        need (8);
        int64_t v = 0;
        std::memcpy (&v, data.data() + pos, 8);
        pos += 8;
        return v;
    }

    double f64()
    {
        need (8);
        double v = 0.0;
        std::memcpy (&v, data.data() + pos, 8);
        pos += 8;
        return v;
    }

    std::string str()
    {
        const uint32_t len = u32();
        need (len);
        std::string s (reinterpret_cast<const char*> (data.data() + pos), len);
        pos += len;
        return s;
    }

    void bytes (std::vector<uint8_t>& dest, size_t n)
    {
        need (n);
        dest.assign (data.begin() + static_cast<long> (pos),
                     data.begin() + static_cast<long> (pos + n));
        pos += n;
    }

    void expectMagic()
    {
        need (4);
        if (std::memcmp (data.data(), "SEFX", 4) != 0)
            fail ("not a fixture file (bad magic)");
        pos += 4;
    }

    [[noreturn]] void fail (const std::string& why) const
    {
        throw std::runtime_error (fileName + ": " + why);
    }

    bool atEnd() const noexcept { return pos == data.size(); }
    size_t offset() const noexcept { return pos; }

private:
    const std::vector<uint8_t>& data;
    std::string fileName;
    size_t pos = 0;
};

SoundDocument readDoc (Reader& r)
{
    DocumentInit init;
    init.numTracks = 0;
    SoundDocument doc (init);

    doc.name = r.str();

    // The reference keeps the rate as a 16.16 Fixed (Hz × 65536); the engine
    // keeps a double. The division is exact, so this round-trips — but anything
    // that RE-derives a Fixed from this double must round, not truncate.
    doc.rateHz = static_cast<double> (r.i64()) / 65536.0;

    doc.bps      = r.i32();
    doc.baseFreq = r.i32();
    doc.selSt    = r.u32();
    doc.selEnd   = r.u32();
    doc.firstSelTrack = r.i32();
    doc.lastSelTrack  = r.i32();
    doc.loopSt   = r.u32();
    doc.loopEnd  = r.u32();

    const uint32_t numTracks = r.u32();
    doc.tracks.clear();
    doc.tracks.reserve (numTracks);

    for (uint32_t t = 0; t < numTracks; ++t)
    {
        Track track;
        const uint32_t size = r.u32();
        track.outLeft  = r.u8() != 0;
        track.outRight = r.u8() != 0;
        track.volume   = r.i32();
        r.bytes (track.data, size);
        doc.tracks.push_back (std::move (track));
    }

    return doc;
}

Settings readParams (Reader& r)
{
    Settings out;
    const uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i)
    {
        const std::string key = r.str();
        Value v;
        v.kind = static_cast<Value::Kind> (r.u8());
        switch (v.kind)
        {
            case Value::Kind::Int:  v.i = r.i64(); break;
            case Value::Kind::Real: v.d = r.f64(); break;
            case Value::Kind::Str:  v.s = r.str(); break;
            default: r.fail ("unknown settings value kind");
        }
        out.emplace (key, std::move (v));
    }
    return out;
}

} // namespace

FixtureFile load (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw std::runtime_error (path + ": cannot open");

    std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)),
                                 std::istreambuf_iterator<char>());

    Reader r (bytes, std::filesystem::path (path).filename().string());
    r.expectMagic();

    const uint32_t version = r.u32();
    if (version != kFixtureVersion)
        r.fail ("fixture version " + std::to_string (version) + ", expected "
                + std::to_string (kFixtureVersion) + " — re-run `npm run fixtures`");

    FixtureFile file;
    file.moduleName = r.str();
    file.signature  = r.str();

    const uint32_t caseCount = r.u32();
    file.cases.reserve (caseCount);

    for (uint32_t c = 0; c < caseCount; ++c)
    {
        Case cs;
        cs.name         = r.str();
        cs.randSeed     = r.u32();
        cs.doExtend     = static_cast<ExtendPref> (r.u8());
        cs.extendAnswer = r.u8() != 0;
        cs.params       = readParams (r);
        cs.input        = readDoc (r);
        cs.result       = r.i32();
        cs.expected     = readDoc (r);

        const uint32_t spawnCount = r.u32();
        cs.spawned.reserve (spawnCount);
        for (uint32_t s = 0; s < spawnCount; ++s)
            cs.spawned.push_back (readDoc (r));

        file.cases.push_back (std::move (cs));
    }

    // Trailing bytes mean the writer and this reader disagree about the layout,
    // even though every field so far parsed. Catch it here rather than as a
    // baffling mismatch later.
    if (! r.atEnd())
        r.fail ("trailing bytes after " + std::to_string (caseCount) + " cases"
                " (at offset " + std::to_string (r.offset()) + ")");

    return file;
}

std::vector<std::string> listFixtures (const std::string& dir)
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;

    if (! fs::is_directory (dir))
        throw std::runtime_error (dir + ": not a directory — run `npm run fixtures`");

    for (const auto& entry : fs::directory_iterator (dir))
        if (entry.is_regular_file() && entry.path().extension() == ".fx")
            out.push_back (entry.path().string());

    std::sort (out.begin(), out.end());
    return out;
}

std::string diff (const SoundDocument& expected, const SoundDocument& actual)
{
    std::ostringstream o;

    if (expected.bps != actual.bps)
    {
        o << "bps " << actual.bps << ", expected " << expected.bps;
        return o.str();
    }
    // The rate is a double here but a quantised Fixed at heart; compare at the
    // reference's resolution rather than demanding bit-equal doubles.
    if (std::llround (expected.rateHz * 65536.0) != std::llround (actual.rateHz * 65536.0))
    {
        o << "rate " << actual.rateHz << " Hz, expected " << expected.rateHz;
        return o.str();
    }
    if (expected.baseFreq != actual.baseFreq)
    {
        o << "baseFreq " << actual.baseFreq << ", expected " << expected.baseFreq;
        return o.str();
    }
    if (expected.numTracks() != actual.numTracks())
    {
        o << actual.numTracks() << " tracks, expected " << expected.numTracks();
        return o.str();
    }
    if (expected.selSt != actual.selSt || expected.selEnd != actual.selEnd)
    {
        o << "selection [" << actual.selSt << ", " << actual.selEnd << "), expected ["
          << expected.selSt << ", " << expected.selEnd << ")";
        return o.str();
    }
    if (expected.firstSelTrack != actual.firstSelTrack || expected.lastSelTrack != actual.lastSelTrack)
    {
        o << "selected tracks " << actual.firstSelTrack << ".." << actual.lastSelTrack
          << ", expected " << expected.firstSelTrack << ".." << expected.lastSelTrack;
        return o.str();
    }

    for (size_t t = 0; t < expected.tracks.size(); ++t)
    {
        const auto& e = expected.tracks[t];
        const auto& a = actual.tracks[t];

        if (e.data.size() != a.data.size())
        {
            o << "track " << (t + 1) << " is " << a.data.size() << " bytes, expected "
              << e.data.size();
            return o.str();
        }

        for (size_t p = 0; p < e.data.size(); ++p)
        {
            if (e.data[p] != a.data[p])
            {
                size_t differing = 0;
                for (size_t q = p; q < e.data.size(); ++q)
                    if (e.data[q] != a.data[q]) ++differing;

                o << "track " << (t + 1) << " byte " << p << ": got 0x" << std::hex
                  << static_cast<int> (a.data[p]) << ", expected 0x"
                  << static_cast<int> (e.data[p]) << std::dec
                  << " (" << differing << " of " << e.data.size() << " bytes differ)";
                return o.str();
            }
        }

        if (e.outLeft != a.outLeft || e.outRight != a.outRight)
        {
            o << "track " << (t + 1) << " output channel differs";
            return o.str();
        }
    }

    return {};
}

} // namespace se::fx
