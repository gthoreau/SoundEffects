// ShiftClickLayer — ⇧-click on a Sound, wherever on it (0.6.0 step 7.2).
//
// A see-through layer over the canvas and over the Mixer's strips that claims a
// press only while ⇧ and the left button are down on a Sound: the click adds that
// Sound to the multi-selection or takes it out, and what is under the pointer (a
// transport button, a waveform, a fader) never hears it. Every other event falls
// through to what is below.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdint>
#include <functional>

class ShiftClickLayer : public juce::Component
{
public:
    ShiftClickLayer() { setMouseClickGrabsKeyboardFocus (false); }

    /** The Sound under this point (the layer's coordinates), 0 for none. */
    std::function<uint64_t (juce::Point<int>)> soundAt;
    std::function<void (uint64_t)> onShiftClick;

    /** A text field being typed in keeps its ⇧-click (it extends the text selection). */
    static bool inFocusedTextField (juce::Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (dynamic_cast<juce::TextEditor*> (c) != nullptr)
                return c->hasKeyboardFocus (true);
        return false;
    }

    bool hitTest (int x, int y) override
    {
        const auto mods = juce::ModifierKeys::currentModifiers;
        return mods.isShiftDown() && mods.isLeftButtonDown() && ! mods.isPopupMenu()
            && soundAt != nullptr && soundAt ({ x, y }) != 0;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (soundAt == nullptr || onShiftClick == nullptr) return;
        if (const auto id = soundAt (e.getPosition()); id != 0) onShiftClick (id);
    }
};
