// ScrollTarget — where a scrolling column should sit after one of its trays opens.
//
// Pure and JUCE-free so the rule can be tested (native/tests/run_engine.cpp).
//
// The rule (0.5.0, "smart auto-scroll"): scroll just enough to show the group and its
// tray. If the two together do not fit the view but the tray alone does, show the
// whole tray (as much of the group as is left comes with it). If the tray alone is
// taller than the view, put the tray's top at the top of the view, right under the
// static header, so its title and first controls are what is seen.

#pragma once

#include <algorithm>

namespace se
{

struct ScrollSpan
{
    int viewTop = 0;        // the scroll position now
    int viewH = 0;          // the visible height
    int contentH = 0;       // the whole column's height
    int groupTop = 0;       // the group that holds the tray, in column coordinates
    int trayTop = 0;        // the tray (it starts where the group ends)
    int trayBottom = 0;
};

/** The scroll position to move to: unchanged when everything already shows. */
inline int scrollTargetForTray (const ScrollSpan& s)
{
    const int maxTop = std::max (0, s.contentH - s.viewH);
    const auto clampTop = [maxTop] (int t) { return std::max (0, std::min (t, maxTop)); };

    if (s.viewH <= 0 || s.trayBottom <= s.trayTop)
        return clampTop (s.viewTop);

    if (s.trayBottom - s.trayTop >= s.viewH)
        return clampTop (s.trayTop);

    const bool groupFits = s.trayBottom - s.groupTop <= s.viewH;
    const int top = groupFits ? s.groupTop : s.trayTop;

    int t = s.viewTop;
    if (top < t)                    t = top;
    else if (s.trayBottom > t + s.viewH) t = s.trayBottom - s.viewH;
    return clampTop (t);
}

} // namespace se
