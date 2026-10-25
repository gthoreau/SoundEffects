// MixerComponent — the Mixer side panel's content ("Type=Mixer" 636:10485): the
// New Sound / Open Sound bar, then one channel strip per Window, each seated in a
// static, numbered slot.
//
// A strip ("Mixer-channel" 509:9257) is two rows beside a numbered handle:
//   [#] | Name ......................... [M][S]
//       | L (--pan--) R   [=====VU====|=fader=]
// It wears its Window's theme colour (636:11566). The sliders' troughs are MULTIPLY
// layers, so on a tinted strip they come out as a darker shade of it. A muted strip
// lays a white veil over itself and greys its text; a strip silenced by somebody
// else's solo looks the same, because "you will hear nothing from this" is the fact
// that matters. Hover outlines the strip in black; the strip whose Window is
// selected on the canvas is the Active one: heavier frame, drag bars on its handle,
// and 2 px taller, as in the design.
//
// Slots are the panel's own order, independent of the canvas: a new Sound takes
// the lowest free slot (Engine::addWindow), Close frees it, and the gaps between
// used slots show as empty numbered rows. Dragging a strip by its handle or its
// name onto another row moves it there, swapping with whoever sat there; the canvas
// is left alone. The slot is saved in the .sefx (state blob v2).
//
// Clicking anywhere on a strip focuses its Window on the canvas: the panel is a
// list OF the Windows, not a parallel set of nameplates that happen to match.
//
// Writes go to the per-Window atomics (gain / balance / muted / soloed), so it is
// safe to drive while audio runs; the VU reads Window::meterLeft / meterRight,
// published by the audio thread.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Os8Widgets.h"
#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <vector>

/** One mixer channel: handle, name, M/S, balance and the fader with its stereo VU. */
class MixerStrip : public juce::Component
{
public:
    MixerStrip (se::Engine& engine, uint64_t windowId);
    ~MixerStrip() override;

    uint64_t windowId() const noexcept { return id; }

    /** Sample what this channel looks like now — VU, name (edited over on the
        Window, with nothing else to announce it), theme, mixer atomics changed from
        elsewhere (a menu, a loaded canvas), and whether the desktop-wide solo filter
        is on. Repaints only when something changed. */
    void refreshState (bool soloFilterOn);

    /** The canvas's selected Window is the Active strip here. */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** Clicked anywhere — the panel asks the canvas to focus this Window. */
    std::function<void (uint64_t)> onFocusRequested;
    /** Dragged by the handle or the name: where the strip's top is now (in the
        parent's coordinates) while it moves, and the drop when it is let go. */
    std::function<void (MixerStrip&, int top)> onDragMove;
    std::function<void (MixerStrip&)> onDragEnd;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isBeingDragged() const noexcept { return draggingStrip; }

    /** Draw the Hover state without a mouse: for the off-screen snapshots. */
    void setForcedHover (bool h) { forcedHover = h; repaint(); }

    // Geometry, from Mixer-channel 509:9257. A strip is laid out 270 px wide, like
    // the component; the panel's content is 268, so its last two columns fall under
    // the panel's frame exactly as they do in the design.
    static constexpr int kWidth = 270;
    static constexpr int kHeight = 44, kActiveHeight = 46;
    static constexpr int kHandleWidth = 18;

    // The fader's dB scale. Position runs 0..1 across the travel; 0 is silence, not
    // kMinDb, so the fader shuts the channel fully. The travel is NOT divided evenly
    // over the range — see the taper table in the .cpp; unity sits at 80 %, where
    // the design puts the 0 dB tick.
    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;
    static float posToDb (float pos) noexcept;
    static float dbToPos (float db) noexcept;

private:
    se::Window* window() const noexcept { return engine.windowById (id); }

    /** The Active strip's content sits 1 px right and down (its frame is heavier). */
    juce::Point<int> inset() const noexcept { return selected ? juce::Point<int> (1, 1) : juce::Point<int>(); }
    juce::Rectangle<int> muteBox() const;
    juce::Rectangle<int> soloBox() const;
    bool inDragArea (juce::Point<int>) const;

    juce::Colour themeFill() const;
    bool muted() const;
    /** Will this channel be heard? Muted, or held off by ANOTHER channel's solo. */
    bool silenced() const;

    /** Pushes the look of a muted strip and the colour under the troughs into the
        two sliders. */
    void updateSliderLooks();

    se::Engine& engine;
    uint64_t id = 0;

    Os8Slider pan    { os8::SliderKind::pan };
    Os8Slider volume { os8::SliderKind::volume };

    /** Hover: the sliders are children, so the strip's own enter/exit is not
        enough to know whether the mouse is over it. */
    struct HoverWatch : juce::MouseListener
    {
        explicit HoverWatch (MixerStrip& s) : strip (s) {}
        void mouseEnter (const juce::MouseEvent&) override { strip.repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { strip.repaint(); }
        MixerStrip& strip;
    } hoverWatch { *this };

    juce::String lastName;
    int   lastTint = -2;          // -2 = never sampled
    bool  lastMuted = false, lastSoloed = false, lastSilenced = false;
    bool  soloFilter = false;     // is ANY channel soloed? (then silence may not be ours)
    bool  selected = false;
    bool  pressInDragArea = false;
    bool  draggingStrip = false;
    bool  forcedHover = false;
    int   dragStartTop = 0;

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

    /** Mark which Window the canvas has selected, so the matching strip is Active. */
    void setSelectedWindow (uint64_t id);

    std::function<void()> onNew, onOpen;
    /** A channel was clicked: focus its Window on the canvas. */
    std::function<void (uint64_t)> onChannelClicked;

    /** The New Sound / Open Sound bar ("new-open" 636:10150). */
    static constexpr int kBarHeight = 44;

private:
    /** The scrolled list: strips are its children, empty rows and the drop target
        are painted. */
    struct SlotList : juce::Component
    {
        explicit SlotList (MixerComponent& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        MixerComponent& owner;
    };

    void layoutRows();
    /** Which row a strip whose top is at `top` would drop into. */
    int slotAt (int top) const;
    MixerStrip* stripInSlot (int slot) const;

    se::Engine& engine;
    uint64_t selectedId = 0;

    Os8Button newButton  { "New Sound",  os8::ButtonKind::primary };
    Os8Button openButton { "Open Sound", os8::ButtonKind::primary };

    juce::Viewport viewport;
    SlotList       list { *this };
    std::vector<std::unique_ptr<MixerStrip>> strips;

    std::vector<int> rowTops;      // y of each slot's row in `list`, plus the end
    int dropSlot = -1;             // while a strip is dragged: the row it would land in

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
