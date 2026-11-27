// MagnetophonComponent — the Magnetophon panel's content ("Magnetophon" 1307:6817),
// at the bottom of the left column, inside a SidePanel that gives it its title bar
// and collapse box (layout 700:14314).
//
// From the top, in a 268 px column:
//   - the deck (137 px): two reels, turning while the tape moves; the tape runs from
//     each one round a pinch roller and under the head;
//   - the strip (53 px): the tape's waveform scrolling under a fixed centre playhead,
//     black behind it and grey ahead, a "Take N" label at each take (it sticks to the
//     left edge while its take runs off it, and shortens in a narrow one); the take
//     being recorded is red. Drag it to wind the tape by hand. The take under the
//     mouse shows a trash icon that deletes it, audio and all, with no question
//     asked (greyed while recording);
//   - the status line (16 px, dark): "From: <source>", the position centred on the
//     playhead, then rate and bits;
//   - the transport (44 px): Source, Record, Stop, Previous, Play, Next, Send.
//
// The panel draws and drives the processor's TapeDeck (which outlives it); the
// editor opens the source popup and the Send popup through the callbacks.

#pragma once

#include "Os8Widgets.h"
#include "engine/Tape.h"

#include <functional>
#include <optional>
#include <vector>

class MagnetophonComponent : public juce::Component
{
public:
    MagnetophonComponent (se::Engine&, se::TapeDeck&);

    /** The content's height (the SidePanel adds its title bar and frame). */
    static constexpr int kContentHeight = 251;

    /** The editor's 30 Hz tick: follow the deck, turn the reels, redraw what moved. */
    void refresh();

    /** The text after "From: ". */
    void setSourceName (const juce::String&);

    /** Source clicked. */
    std::function<void()> onSourceRequested;
    /** Record clicked: the editor checks the source can be heard, then records (true)
        or explains why not (false). */
    std::function<bool()> onRecordRequested;
    /** Send clicked (the button is greyed while this is not set). */
    std::function<void()> onSendRequested;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // Layout, in content coordinates (Figma's component less its frame and bar).
    static constexpr int kDeckHeight = 137;
    static constexpr int kStripTop = kDeckHeight, kStripHeight = 53;
    static constexpr int kStatusTop = kStripTop + kStripHeight, kStatusHeight = 16;
    static constexpr int kTransportTop = kStatusTop + kStatusHeight + 1, kTransportHeight = 44;
    /** The strip's playhead (2 px, centred on this x: the first column of what is
        still to come) and its time scale. */
    static constexpr int kPlayheadX = 134;
    static constexpr double kPixelsPerSecond = 24.0;

private:
    /** The delete-take icon of the take under a point: where it is drawn, and where
        a click on it counts (never outside its take). take -1: no take there. */
    struct Trash { int take = -1; juce::Rectangle<int> icon, hit; };
    Trash trashAt (juce::Point<int>) const noexcept;
    void hover (std::optional<juce::Point<int>>);
    void updateButtons();
    void updatePeaks();
    size_t binFrames() const noexcept;
    float peakAt (int64_t bin) const noexcept;
    void paintDeck (juce::Graphics&) const;
    void paintStrip (juce::Graphics&) const;
    void paintStatus (juce::Graphics&) const;
    void paintTransport (juce::Graphics&) const;
    /** Where `frame` is in the strip, with the tape where it is now. */
    int xOfFrame (size_t frame) const noexcept;

    se::Engine& engine;
    se::TapeDeck& deck;

    Os8Button sourceButton { "Source" };
    Os8TransportButton recordButton { os8::icon::Id::record };
    Os8TransportButton stopButton { os8::icon::Id::stop };
    Os8TransportButton previousButton { os8::icon::Id::previous };
    Os8TransportButton playButton { os8::icon::Id::playForward };
    Os8TransportButton nextButton { os8::icon::Id::next };
    Os8TransportButton sendButton { os8::icon::Id::sendToCanvasArrow };

    juce::String sourceName { "Main" };

    // The strip's peak cache: one value per bin of binFrames(), max |L|,|R|.
    std::vector<float> peaks;
    size_t cachedBin = 0;
    double cachedRate = 0.0;

    // What was last drawn, to repaint only what moved.
    size_t lastPos = 0, lastLen = 0;
    se::Tape::Mode lastMode = se::Tape::Mode::stopped;
    int lastTakes = 0;
    uint32_t lastCuts = 0, peakCuts = 0;
    float angleL = 0.0f, angleR = 0.0f;   // reel rotations, radians (screen clockwise)

    // Winding the tape by hand in the strip.
    bool scrubbing = false;
    size_t scrubFrom = 0;
    // The mouse over the panel, and a press on a trash icon.
    std::optional<juce::Point<int>> mouse;
    Trash pressed;
    uint32_t lastDelete = 0;   // getMillisecondCounter()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagnetophonComponent)
};
