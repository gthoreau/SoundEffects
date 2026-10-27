#include "Project.h"

#include "Wav.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace se {

namespace {

constexpr const char* kAssets = "Assets";

bool readAll (const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream f (p, std::ios::binary);
    if (! f) return false;
    f.seekg (0, std::ios::end);
    const auto n = f.tellg();
    if (n < 0) return false;
    f.seekg (0);
    out.resize ((size_t) n);
    if (n > 0) f.read (reinterpret_cast<char*> (out.data()), n);
    return (bool) f;
}

/** Write beside the target, then rename over it: a crash or a full disk leaves the
    old file whole. */
bool writeAtomic (const fs::path& p, const std::vector<uint8_t>& bytes)
{
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
        if (! f) return false;
        if (! bytes.empty()) f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
        f.flush();
        if (! f) { std::error_code ec; fs::remove (tmp, ec); return false; }
    }
    std::error_code ec;
    fs::rename (tmp, p, ec);
    if (ec) { fs::remove (tmp, ec); return false; }
    return true;
}

/** An asset path from a file we did not necessarily write: relative, inside
    Assets/, no way out. */
bool safeAssetPath (const std::string& rel)
{
    const std::string prefix = std::string (kAssets) + "/";
    if (rel.size() <= prefix.size() || rel.compare (0, prefix.size(), prefix) != 0) return false;
    if (rel.find ('\\') != std::string::npos || rel.find ('\0') != std::string::npos) return false;
    if (rel.find ("..") != std::string::npos) return false;
    if (rel.find ("//") != std::string::npos) return false;
    return true;
}

} // namespace

std::string safeFileName (const std::string& s)
{
    std::string o;
    for (unsigned char c : s)
        o += (c < 32 || c == '/' || c == ':' || c == '\\' || c == 127) ? '-' : (char) c;
    // No leading dot (hidden) and no trailing space/dot; never empty.
    while (! o.empty() && (o.front() == '.' || o.front() == ' ')) o.erase (o.begin());
    while (! o.empty() && (o.back() == '.' || o.back() == ' ')) o.pop_back();
    if (o.size() > 80) o.resize (80);
    return o.empty() ? std::string ("sound") : o;
}

bool isProjectFile (const fs::path& file)
{
    std::ifstream f (file, std::ios::binary);
    uint8_t h[9] {};
    if (! f.read (reinterpret_cast<char*> (h), 9)) return false;
    return std::memcmp (h, "SEFX", 4) == 0 && h[4] >= 3 && h[5] == 0 && h[6] == 0 && h[7] == 0 && h[8] == 1;
}

bool readSefx (const fs::path& file, EngineState& out, std::string& err)
{
    std::vector<uint8_t> blob;
    if (! readAll (file, blob))
    {
        err = file.filename().string() + " could not be read.";
        return false;
    }

    EngineState st;
    if (! parseEngineState (blob.data(), blob.size(), st))
    {
        err = file.filename().string() + " is damaged, or was written by a newer version of SoundEffects than this one.";
        return false;
    }

    if (st.externalAudio)
    {
        const fs::path base = file.parent_path();
        for (auto& w : st.windows)
        {
            if (w.assetPaths.size() != w.tracks.size() || w.assetBytes.size() != w.tracks.size())
            {
                err = file.filename().string() + " is damaged.";
                return false;
            }
            for (size_t t = 0; t < w.tracks.size(); ++t)
            {
                const std::string& rel = w.assetPaths[t];
                std::vector<uint8_t> wav;
                if (! safeAssetPath (rel) || ! readAll (base / rel, wav))
                {
                    err = "The audio file \"" + rel + "\" is missing from beside " + file.filename().string() + ".";
                    return false;
                }
                if (! decodeTrackWav (wav.data(), wav.size(), w.bps, w.assetBytes[t], w.tracks[t].data))
                {
                    err = "The audio file \"" + rel + "\" does not match " + file.filename().string() + " (changed or damaged).";
                    return false;
                }
            }
        }
    }

    out = std::move (st);
    out.externalAudio = false;   // loaded: the audio is in the Tracks now
    return true;
}

bool writeProject (const fs::path& dir, const std::string& name, const EngineState& state, std::string& err)
{
    std::error_code ec;
    fs::create_directories (dir / kAssets, ec);
    if (ec) { err = "The folder " + dir.string() + " could not be created."; return false; }

    const fs::path sefx = dir / (name + kEngineStateFileSuffix);

    // What the last save of this project pointed at, so stale stems can go after.
    std::set<std::string> oldAssets;
    {
        std::vector<uint8_t> blob;
        EngineState old;
        if (readAll (sefx, blob) && parseEngineState (blob.data(), blob.size(), old) && old.externalAudio)
            for (const auto& w : old.windows)
                for (const auto& p : w.assetPaths)
                    if (safeAssetPath (p)) oldAssets.insert (p);
    }

    EngineState out = state;
    out.externalAudio = true;
    std::set<std::string> fresh;
    std::vector<uint8_t> wav;

    for (size_t i = 0; i < out.windows.size(); ++i)
    {
        auto& w = out.windows[i];
        w.assetPaths.clear();
        w.assetBytes.clear();
        char num[16];
        std::snprintf (num, sizeof (num), "%02zu ", i + 1);

        for (size_t t = 0; t < w.tracks.size(); ++t)
        {
            const std::string rel = std::string (kAssets) + "/" + num + safeFileName (w.name)
                                  + " - track " + std::to_string (t + 1) + ".wav";
            encodeTrackWav (w.tracks[t], w.rateHz, w.bps, wav);
            if (! writeAtomic (dir / rel, wav))
            {
                err = "Nothing could be written to " + rel + ". Check that the disk is not full and that the folder is not locked.";
                return false;
            }
            w.assetPaths.push_back (rel);
            w.assetBytes.push_back (w.tracks[t].data.size());
            fresh.insert (rel);
        }
        // The blob does not carry the audio: free the copy before it is serialised.
    }

    if (! writeAtomic (sefx, serializeState (out)))
    {
        err = "Nothing could be written to " + sefx.filename().string() + ".";
        return false;
    }

    for (const auto& p : oldAssets)
        if (fresh.count (p) == 0)
            fs::remove (dir / p, ec);

    return true;
}

bool writeSoundExport (const fs::path& file, const WindowState& sound, std::string& err)
{
    EngineState st;
    st.windows.push_back (sound);
    auto& w = st.windows.back();
    w.slot = Window::kNoSlot;
    w.soloed = false;
    w.x = w.y = 0.0f;
    w.gridCol = Window::kNoCell;
    w.gridRow = 0;
    w.assetPaths.clear();
    w.assetBytes.clear();

    if (! writeAtomic (file, serializeState (st)))
    {
        err = "Nothing could be written to " + file.filename().string() + ". Check that the disk is not full and that the file is not locked.";
        return false;
    }
    return true;
}

} // namespace se
