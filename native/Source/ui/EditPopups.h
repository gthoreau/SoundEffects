// EditPopups — the Edit and Selection menus' dialogs, on the Os8Popup frame
// ("Popups-settings" 543:7841: Leave Silence 543:8056, Shift Sound 543:9335).
//
// Each builder lays out its Figma variant in the popup's rows, opens with the values
// 0.9.2's dialog would (disassembly/Edit-Selection-Commands.md) and, when the primary
// button is pressed, hands the answer to `apply` already converted to BYTES at the
// sound's own rate and depth. The popup is built, not shown: the caller places it
// (Os8Popup::showOver) and then focuses `field`, as the original's SelIText did.
//
// Leave Silence, Shift Sound and Select Exactly share ONE remembered duration, as they
// did in 0.9.2 (a single app global, kept in seconds so it means the same at any rate):
// the field opens on whatever was last applied in any of them, 1000 ms the first time.

#pragma once

#include "Os8Popup.h"
#include "engine/SoundDocument.h"

#include <functional>
#include <memory>

namespace editpopups {

struct Built
{
    std::unique_ptr<Os8Popup> popup;
    Os8TextField* field = nullptr;   // to focus once the popup is on screen
};

/** "Leave [N units] of silence at the beginning of (• the sound / • the selected
    channel(s))". With every track selected, the second choice is greyed and the first
    one picked, as 0.9.2 did. */
Built leaveSilence (const se::SoundDocument&, std::function<void (size_t bytes, bool wholeSound)> apply);

/** "Shift [the scope] by: [N units] (• Shift Right / • Shift Left)"; the scope is the
    one se::shiftScope reads from the selection. Shift Right is picked. */
Built shiftSound (const se::SoundDocument&, std::function<void (size_t bytes, bool right)> apply);

/** Forget the remembered duration and units (the snapshot renders a fresh app). */
void resetMemory();

} // namespace editpopups
