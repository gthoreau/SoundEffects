// MixerComponent — the main mixer: one HORIZONTAL channel strip per Window,
// feeding the engine's global additive sum. This is the "true mixer" that
// replaces the original's dead "Level" widget.
//
// The panel is a fixed column down the left-hand side, at most kMaxWidth wide,
// with the New / Open buttons sitting at its top and the strips stacked below in
// a scrolling list. Everything is hand-painted in the same faux-System 7 idiom as
// the Windows on the canvas: 1px black frames on the grey face, no gradients,
// inverted fill for the "on" state.
//
// A strip is one row: NAME · MUTE · a combined fader/VU control. Fader and meter
// share ONE dB scale in ONE trough — the fill is what the channel is putting out,
// the cap is where its gain is set, and the tick between them is 0 dB — so a
// glance reads both without the panel growing a second column of meters.
//
// Writes go to the per-Window atomics (gain / muted), so it is safe to drive
// while audio runs; the meter reads Window::meter, published by the audio thread.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <vector>

/** A System 7 push button: rounded 1px black outline, inverted while held. */
class Sys7Button : public juce::Button
{
public:
    explicit Sys7Button (const juce::String& text) : juce::Button (text) { setButtonText (text); }

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;
};

/** Only what the panel actually borrows from JUCE: the list's scrollbar. */
class Sys7LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
};

/** One horizontal mixer channel: name, mute, and the combined fader/VU trough. */
class MixerStrip : public juce::Component
{
public:
    MixerStrip (se::Engine& engine, uint64_t windowId);

    uint64_t windowId() const noexcept { return id; }

    /** Sample the engine's meter atomic; repaints only when the bar actually moved. */
    void refreshMeter();

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kHeight = 26;

    // The one dB scale the fader and the meter share. Position runs 0..1 across
    // the trough; 0 is silence, not -60 dB, so the fader shuts the channel fully.
    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;
    static float posToDb (float pos) noexcept;
    static float dbToPos (float db) noexcept;

private:
    juce::Rectangle<int> nameRect() const;
    juce::Rectangle<int> muteRect() const;
    juce::Rectangle<int> troughRect() const;

    se::Window* window() const noexcept { return engine.windowById (id); }
    float gainPos() const noexcept;
    void setGainFromX (int x);

    se::Engine& engine;
    uint64_t id = 0;

    float meterLevel = 0.0f;     // last value sampled from the engine, 0..1 linear
    bool  draggingFader = false; // while true the name cell shows the dB readout

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

class MixerComponent : public juce::Component
{
public:
    explicit MixerComponent (se::Engine& engine);
    ~MixerComponent() override;

    /** Sync strips to the current set of Windows, and tick the meters. Editor timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The panel never draws wider than this. */
    static constexpr int kMaxWidth = 250;

    std::function<void()> onNew, onOpen;

private:
    juce::Rectangle<int> listArea() const;

    se::Engine& engine;

    Sys7LookAndFeel lookAndFeel;   // declared first: outlives every child that uses it

    Sys7Button newButton  { "New" };
    Sys7Button openButton { juce::String::fromUTF8 ("Open\xE2\x80\xA6") };

    juce::Viewport  viewport;
    juce::Component stripHolder;   // the viewport's content: just holds the strips
    std::vector<std::unique_ptr<MixerStrip>> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
