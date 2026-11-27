// SoundGroup — which Sounds are selected, and the multi-selection (0.6.0 step 7).
//
// A pure model, no JUCE and no Engine: Window ids and the rules for clicking them.
// The canvas owns one; everything that acts on "the selected Sound" reads the
// PRIMARY, and the functions the spec shares across a multi-selection ask targets().
//
//   plain click on a member       it becomes primary; the group stays
//   plain click on a non-member   the group ends; that Sound alone is selected
//   click on the empty canvas     nothing is selected
//   ⇧-click on a non-member       it joins and becomes primary
//   ⇧-click on a member           it leaves (the last one left stays selected: there
//                                 is no multi-selection to leave)
//   a member closed               it leaves
//
// The members are kept in the order they were last clicked, so the primary is
// always the last one, and when it leaves, the one clicked before it takes over.
// Never saved.

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace se {

class SoundGroup
{
public:
    /** The last clicked member: what the effects, the menus and the selection
        fields act on. 0 when nothing is selected. */
    uint64_t primary() const noexcept { return order.empty() ? 0 : order.back(); }
    bool contains (uint64_t id) const noexcept { return std::find (order.begin(), order.end(), id) != order.end(); }
    /** Two members or more: the multi-selection proper. */
    bool isGroup() const noexcept { return order.size() > 1; }
    /** Every selected Sound, the primary last. */
    const std::vector<uint64_t>& members() const noexcept { return order; }

    /** The Sounds a shared function used on `id` reaches, `id` first: the whole
        group when `id` is in one, else `id` alone. */
    std::vector<uint64_t> targets (uint64_t id) const
    {
        if (id == 0) return {};
        std::vector<uint64_t> out { id };
        if (isGroup() && contains (id))
            for (uint64_t m : order)
                if (m != id) out.push_back (m);
        return out;
    }

    // Each returns whether anything changed (the primary or the members).

    /** A plain click on Sound `id`, or on the empty canvas (0). */
    bool click (uint64_t id)
    {
        if (id == 0) return clear();
        if (primary() == id) return false;   // a member or alone: already as it should be
        if (contains (id)) { toBack (id); return true; }
        order.assign (1, id);
        return true;
    }

    /** ⇧-click on Sound `id`. */
    bool shiftClick (uint64_t id)
    {
        if (id == 0) return false;
        if (! contains (id)) { order.push_back (id); return true; }
        return isGroup() && erase (id);
    }

    /** Sound `id` is gone. */
    bool close (uint64_t id) { return erase (id); }

    bool clear()
    {
        if (order.empty()) return false;
        order.clear();
        return true;
    }

private:
    bool erase (uint64_t id)
    {
        const auto it = std::find (order.begin(), order.end(), id);
        if (it == order.end()) return false;
        order.erase (it);
        return true;
    }
    void toBack (uint64_t id) { erase (id); order.push_back (id); }

    std::vector<uint64_t> order;   // oldest click first
};

} // namespace se
