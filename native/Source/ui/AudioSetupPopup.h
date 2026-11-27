// AudioSetupPopup — "Audio/Midi Setup" (1447:9160), on the Os8Popup frame, in the
// place of JUCE's device selector. Standalone only: a host owns its own devices.
//
//   Feedback Loop:    [ ] Mute audio input
//   Output:           [device        v] [Test]
//   Input:            [device        v] |||||||     (the input's level)
//   Output channels:  | [x] Output 1 + 2 ... |      one pair at a time
//   Input channels:   | [x] Input 1 + 2  ... |      one pair at a time
//   Sample rate:      [48000 Hz      v]
//   Buffer Size:      [512 samples (10.7 ms) v]
//   Midi inputs:      | [x] each device ... |
//                                                   [ OK ]
//
// Every control acts at once on the juce::AudioDeviceManager the standalone already
// saves to its settings file, so what was chosen in 0.5.0 is what shows here. A
// setting the device refuses is said in an alert and the controls go back to what is
// in force. In a window too short for the design the three lists give up height.

#pragma once

#include "Os8Popup.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>

namespace audiosetup {

/** `muteInput` is the standalone holder's "Mute audio input". `availableHeight` is the
    window's, for the lists' height. `closed` is called when OK (or a quit) takes the
    popup away: the caller saves the device state. The popup is built, not shown. */
std::unique_ptr<Os8Popup> build (juce::AudioDeviceManager&, juce::Value muteInput,
                                 int availableHeight, std::function<void()> closed);

} // namespace audiosetup
