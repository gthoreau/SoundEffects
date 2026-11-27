// TapePopups — the Magnetophon's Send to canvas popup (0.5.0 step 11.3), on the
// Os8Popup frame. Not in the design (the spec asks for "a pop-up to choose what to
// send"), so it is built from the popups around it: the radios and checkboxes of
// Select Audio Source, the list of Import Sound.
//
//   Send the tape to the canvas:
//   (•) The whole tape, as one Sound          01:12.40 s
//   ( ) Takes, one Sound each:
//       [x] Take 1                           0:12.40 s
//       [ ] Take 2                    sent   0:03.10 s
//   [x] Clean conversion (dithered)      checked at every opening
//                                  Cancel  [ Send ]
//
// Ticking a take picks "Takes"; Send is greyed while Takes has none ticked. The
// unsent takes start ticked (all of them when every take has been sent).

#pragma once

#include "Os8Popup.h"
#include "engine/Tape.h"

#include <functional>
#include <memory>
#include <vector>

namespace tapepopups {

struct SendChoice
{
    bool whole = true;
    std::vector<int> takes;   // 0-based, in tape order (when not whole)
    bool clean = true;
};

std::unique_ptr<Os8Popup> sendToCanvas (const se::Tape& tape,
                                        std::function<void (const SendChoice&)> send);

/** "1:12.40 s" */
juce::String durationText (size_t frames, double rate);

} // namespace tapepopups
