// KeyboardAim — which Sounds the Keyboard plays, given the selection (0.6.0 fix).
//
// The keys follow the selection, except that a Sound whose Record is armed or on is
// never played: striking it would re-pitch the take. With nothing else selected they
// stay on what they played before, so a sequence carries on while another Sound
// records it. A pure model, no JUCE and no Engine.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace se {

class KeyboardAim
{
public:
    /** `selected` is the selection, the primary first; `free` says a Sound exists and
        is not recording. Returns what the keys should play, the primary first. */
    template <typename Free>
    std::vector<uint64_t> choose (const std::vector<uint64_t>& selected, Free&& free)
    {
        if (selected.empty()) return {};
        const auto usable = [&free] (const std::vector<uint64_t>& ids)
        {
            std::vector<uint64_t> out;
            for (const uint64_t id : ids)
                if (free (id)) out.push_back (id);
            return out;
        };
        if (auto now = usable (selected); ! now.empty())
        {
            earlier.erase (std::remove (earlier.begin(), earlier.end(), now), earlier.end());
            earlier.push_back (now);
            if (earlier.size() > kKept) earlier.erase (earlier.begin());
            return now;
        }
        for (size_t i = earlier.size(); i-- > 0;)
            if (auto before = usable (earlier[i]); ! before.empty()) return before;
        return {};
    }

private:
    static constexpr size_t kKept = 8;
    std::vector<std::vector<uint64_t>> earlier;   // oldest first
};

} // namespace se
