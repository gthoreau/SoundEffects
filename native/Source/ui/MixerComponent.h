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
// name lifts it out of the list (Reorder.h): the strips below or above slide out of
// its way, its gap follows it, and letting go settles it into the gap — the only
// moment the slots change. The canvas is left alone. The slot is saved in the .sefx
// (state blob v2).
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
#include "Reorder.h"
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
    /** Dragged by the handle or the name: lifted once the pointer has travelled
        (with where in the strip it was grabbed), every move after that, and the
        release. The panel works out where the strip goes from the pointer. */
    std::function<void (MixerStrip&, juce::Point<int> grab)> onDragStart;
    std::function<void (MixerStrip&)> onDragMove;
    std::function<void (MixerStrip&)> onDragEnd;

    /** The panel cancelled the drag (Escape, the list changed): ignore the rest of
        this press. */
    void abandonDrag();

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isBeingDragged() const noexcept { return draggingStrip; }

    /** Draw the Hover state without a mouse: for the off-screen snapshots. */
    void setForcedHover (bool h) { forcedHover = h; repaint(); }

    // Geometry, from the refined Mixer-channel-strip 870:16438: 268 x 46 in every
    // state (the panel's content width), the content 2 px down.
    static constexpr int kWidth = 268;
    static constexpr int kHeight = 46, kActiveHeight = 46;
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

    /** Where the content sits: since 870:16438 the same in every state. */
    juce::Point<int> inset() const noexcept { return {}; }
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
    bool keyPressed (const juce::KeyPress&) override;

    /** Mark which Window the canvas has selected, so the matching strip is Active. */
    void setSelectedWindow (uint64_t id);

    std::function<void()> onNew, onOpen;
    /** A channel was clicked: focus its Window on the canvas. */
    std::function<void (uint64_t)> onChannelClicked;

    /** The New Sound / Open Sound bar ("new-open" 636:10150). */
    static constexpr int kBarHeight = 44;

private:
    /** The scrolled list: strips are its children; the open gap is painted under them. */
    struct SlotList : juce::Component
    {
        explicit SlotList (MixerComponent& m) : owner (m) {}
        void paint (juce::Graphics&) override;
        MixerComponent& owner;
    };

    /** Where every strip belongs: its slot's row, or — while a strip is carried —
        the order with the carried one taken out and its gap opened at `gap`. Slides
        there, or jumps when `animate` is false. */
    void layoutRows (bool animate = false);
    MixerStrip* stripInSlot (int slot) const;
    static int rowHeight (const MixerStrip& s) noexcept;

    // --- the carried strip (see Reorder.h) ---
    void beginDrag (MixerStrip&, juce::Point<int> grab);
    /** Follow the pointer: auto-scroll near an edge, move the floating strip, pick
        the gap. Every move and every frame while held. */
    void followPointer();
    /** The strips in slot order, the carried one left out. */
    std::vector<MixerStrip*> othersInOrder() const;
    /** The top of the gap if it opened before the `index`-th of the others. */
    int gapTop (const std::vector<MixerStrip*>& others, int index) const;
    /** Let go (or Escape): glide into the gap, or home when `commit` is false. */
    void land (bool commit);
    /** Settled: restore the strip's look and, if `commit`, move it to its slot. */
    void endDrag (bool commit);
    /** Stop at once, no glide: the strips changed under the drag. */
    void abortDrag();
    bool carrying() const noexcept { return drag.id != 0; }

    struct Drag
    {
        uint64_t id = 0;
        juce::Component::SafePointer<MixerStrip> strip;
        int from = -1;              // its slot when lifted
        int gap = -1;               // the slot it would land in now
        int grabY = 0;              // where in the strip it was taken hold of
        bool landing = false;       // let go: gliding into the gap (or home)
        bool commit = false;        // ...and once there, does the order change?
        size_t count = 0;           // how many strips there were when it began
    } drag;

    se::Engine& engine;
    uint64_t selectedId = 0;

    Os8Button newButton  { "New Sound",  os8::ButtonKind::primary };
    Os8Button openButton { "Open Sound", os8::ButtonKind::primary };

    juce::Viewport viewport;
    SlotList       list { *this };
    std::vector<std::unique_ptr<MixerStrip>> strips;

    std::vector<int> rowTops;      // y of each slot's row in `list`, plus the end

    reorder::Slider slider;
    reorder::Lift   lift;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
