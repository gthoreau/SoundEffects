// TapePopups — "Send to Canvas" (1447:9158), on the Os8Popup frame.
//
//   Send:
//   ( ) Whole tape (05:10:01 s)
//   (•) Individual takes
//   [ ] Select all
//   | [ ] Take 1                       02:05:64 s |
//   | [ ] Take 2               sent    01:02:29 s |
//   [x] Clean conversion               checked at every opening
//                                  Cancel  [ Send ]
//
// The takes start unticked, and the popup on the whole tape. Ticking a take picks
// "Individual takes"; Send is greyed while that is picked with none ticked.

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

/** "01:12:40 s": minutes, seconds, hundredths. */
juce::String durationText (size_t frames, double rate);

} // namespace tapepopups
