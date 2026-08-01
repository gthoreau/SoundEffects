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
// A strip is one row: NAME · MUTE · SOLO · a combined fader/VU control. Fader and
// meter share ONE dB scale in ONE trough — the fill is what the channel is putting
// out, the cap is where its gain is set, and the tick between them is 0 dB — so a
// glance reads both without the panel growing a second column of meters.
//
// A strip is painted in its Window's tint, and clicking anywhere on it focuses that
// Window on the canvas: the panel is a list OF the Windows, not a parallel set of
// nameplates that happen to match.
//
// Writes go to the per-Window atomics (gain / muted / soloed), so it is safe to
// drive while audio runs; the meter reads Window::meter, published by the audio
// thread.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Sys7.h"
#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <vector>

/** One horizontal mixer channel: name, mute, and the combined fader/VU trough. */
class MixerStrip : public juce::Component
{
public:
    MixerStrip (se::Engine& engine, uint64_t windowId);

    uint64_t windowId() const noexcept { return id; }

    /** Sample what this channel looks like now — meter, the name (edited over on the
        Window, with nothing else to announce it), the tint the canvas assigned, and
        whether the desktop-wide solo filter is on, which decides whether THIS
        channel is being silenced by someone else's solo. Repaints only the part
        that actually changed. */
    void refreshState (bool soloFilterOn);

    /** The canvas's selected Window wears a heavier frame here too, so a click on
        one side of the app is legible on the other. */
    void setSelected (bool shouldBeSelected);

    /** Clicked anywhere — the panel asks the canvas to focus this Window. */
    std::function<void (uint64_t)> onFocusRequested;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kHeight = 26;

    // The dB scale the fader and the meter share. Position runs 0..1 across the
    // trough; 0 is silence, not -60 dB, so the fader shuts the channel fully.
    //
    // The travel is NOT divided evenly over that range — see the taper table in the
    // .cpp. These two functions are the only place that knows, so the meter fill,
    // the fader cap and the 0 dB tick all keep sharing one scale whatever it is.
    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;
    static float posToDb (float pos) noexcept;
    static float dbToPos (float db) noexcept;

private:
    juce::Rectangle<int> nameRect() const;
    juce::Rectangle<int> muteRect() const;
    juce::Rectangle<int> soloRect() const;
    juce::Rectangle<int> troughRect() const;

    se::Window* window() const noexcept { return engine.windowById (id); }
    float gainPos() const noexcept;
    void setGainFromX (int x);

    /** The Window's coat, or the plain panel face before the canvas has painted it. */
    juce::Colour tintColour() const;
    /** Will this channel be heard? Muted, or held off by ANOTHER channel's solo —
        either way the strip greys out, because "you will hear nothing from this" is
        the fact that matters and the reason is one glance away at its buttons. */
    bool silenced() const;

    se::Engine& engine;
    uint64_t id = 0;

    float        meterLevel = 0.0f;   // last meter position drawn, 0..1 on the dB scale
    juce::String lastName;            // last name drawn, so a rename can be spotted
    int          lastTint = -2;       // -2 = never sampled; -1 = canvas has not tinted it
    bool  soloFilter = false;         // is ANY channel soloed? (then silence may not be ours)
    bool  selected = false;
    bool  draggingFader = false;      // while true the name cell shows the dB readout

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

    /** Mark which Window the canvas has selected, so the matching strip is framed. */
    void setSelectedWindow (uint64_t id);

    /** The panel never draws wider than this. */
    static constexpr int kMaxWidth = 250;

    std::function<void()> onNew, onOpen;
    /** A channel was clicked: focus its Window on the canvas. */
    std::function<void (uint64_t)> onChannelClicked;

private:
    juce::Rectangle<int> listArea() const;

    se::Engine& engine;

    uint64_t selectedId = 0;

    Sys7LookAndFeel lookAndFeel;   // declared first: outlives every child that uses it

    Sys7Button newButton  { "New" };
    Sys7Button openButton { juce::String::fromUTF8 ("Open\xE2\x80\xA6") };

    juce::Viewport  viewport;
    juce::Component stripHolder;   // the viewport's content: just holds the strips
    std::vector<std::unique_ptr<MixerStrip>> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
