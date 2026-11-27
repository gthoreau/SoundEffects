// MidiMapPanel — the MIDI Mapping side panel's content ("channel-midi-mapping"
// 1451:10974, its twelve note-pads 1451:10840).
//
// Twelve pads, C0..B0, each showing the mixer slot it plays and wearing that
// Sound's theme colour (the design's macos8-gray, as it stays with the Color Theme
// hidden); the selected pad's
// Sound and what happens when its note starts and ends are set below. It edits the
// canvas's map (Engine::midiMap) directly. A pad lights while its note comes in
// ([NATIVE]: the design draws no such state).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Os8Widgets.h"
#include "engine/Engine.h"
#include "engine/MidiMapRouter.h"

#include <array>

class MidiMapPanel : public juce::Component
{
public:
    MidiMapPanel (se::Engine&, se::MidiMapRouter&);

    /** Follow the map, the Mixer's slots and names, and the notes. Editor timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** The design's 261 px panel less its title bar and frame. */
    static constexpr int kContentHeight = 238;

private:
    static juce::Rectangle<int> padBounds (int i) { return { 9 + 42 * (i % 6), 28 + 42 * (i / 6), 40, 40 }; }
    void select (int pad);
    /** The dropdown and the radios, from the selected pad. */
    void showSelected();

    se::Engine& engine;
    se::MidiMapRouter& router;
    int selected = 0;

    Os8Dropdown sound;
    // Start: Play, Stop, Pause. End: Stop, Pause, Play (the design's order).
    Os8Toggle startPlay { "Play", Os8Toggle::Shape::radio }, startStop { "Stop", Os8Toggle::Shape::radio },
              startPause { "Pause", Os8Toggle::Shape::radio }, endStop { "Stop", Os8Toggle::Shape::radio },
              endPause { "Pause", Os8Toggle::Shape::radio }, endPlay { "Play", Os8Toggle::Shape::radio };

    // What was last drawn, so refresh() repaints only on a change.
    std::array<se::MapPad, se::MidiMap::kPads> shown {};
    std::array<bool, se::MidiMap::kPads> lit {};
    std::array<juce::Colour, se::MidiMap::kPads> faces {};   // of the assigned pads
    std::array<uint32_t, se::MidiMap::kPads> seenStrikes {}, flashFrom {};
    juce::StringArray items;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMapPanel)
};
