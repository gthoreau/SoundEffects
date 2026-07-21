// WindowComponent — one sound Window drawn on the canvas, System 7 styling.
//
// Draws the UI-authoritative document (engine.editDoc), edits it in place, and
// publishes via engine.commitEdit — so edits apply mid-playback with no glitch.
// Interactions: title drag, close, transport, L/R output toggles, add/move/trash
// track, selection drag, and ⌘X/⌘C/⌘V clipboard.
//
// Waveforms share one byte-time axis (spanning the window's longest track) so a
// shorter track visibly ends partway and the selection lines up across tracks.
//
// The window is resizable by the grip in its bottom-right corner. The chrome
// (title bar, info strip, transport) keeps a fixed height — resizing only ever
// stretches the waveform lanes, horizontally and vertically.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "engine/Edit.h"
#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <vector>

class WindowComponent : public juce::Component
{
public:
    WindowComponent (se::Engine& engine, uint64_t id);

    uint64_t windowId() const noexcept { return id; }

    static constexpr int kDefaultWidth = 380;
    static constexpr int kMinWidth = 200;
    static constexpr int kMinLaneH = 20;
    static constexpr int kMaxLaneH = 400;

    /** The size this window wants right now: its current size once it has one. */
    int preferredWidth() const noexcept { return getWidth() > 0 ? getWidth() : kDefaultWidth; }
    int preferredHeight() const noexcept;

    /** Adopt a size the canvas restored/chose, in the window's own terms. */
    void applySize (int width, int height);

    /** Write this window's bounds back to the engine's Window record. */
    void storeGeometry();

    /** Shared clipboard slot (owned by the canvas). */
    std::unique_ptr<se::SoundDocument>* clipboard = nullptr;

    void refresh();
    std::function<void (uint64_t)> onClose;
    /** Size changed (user grip drag, or a track added/removed) — relayout the canvas. */
    std::function<void()> onGeometryChanged;
    /** The user dragged this window by its title bar — an explicit manual placement. */
    std::function<void()> onUserMoved;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    se::SoundDocument* doc() const noexcept { return engine.editDoc (id); }
    // Publish the edit AND invalidate the waveform cache (content may have changed
    // without a size change, e.g. silence — the size-based cache wouldn't notice).
    void commit()   { engine.commitEdit (id); cachedWidth = -1; repaint(); }
    void resizeToFit();
    void ensurePeaks (se::SoundDocument& d);
    /** Mouse position in canvas ("world") coordinates — stable while this window
        moves or the canvas is zoomed, unlike local or screen coordinates. */
    juce::Point<int> parentPos (const juce::MouseEvent&) const;

    // geometry
    int numTracks() const noexcept;
    int laneHeight() const noexcept { return laneH; }
    static constexpr int chromeHeight() noexcept { return kTitleH + kInfoH + kTransportH; }
    juce::Rectangle<int> resizeGripBounds() const;
    juce::Rectangle<int> infoRect() const;
    juce::Rectangle<int> addTrackButton() const;
    juce::Rectangle<int> laneRect (int t) const;
    juce::Rectangle<int> lrRect (int t, bool left) const;
    juce::Rectangle<int> ctrlRect (int t, int which) const;   // 0=up 1=down 2=trash
    juce::Rectangle<int> waveRect (int t) const;
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> transportButton (int i) const;       // 0..5
    juce::Rectangle<int> closeBoxBounds() const;
    int    waveWidth() const noexcept;
    size_t byteAtX (int x, size_t maxSize) const noexcept;
    int    xAtByte (size_t b, size_t maxSize) const noexcept;

    se::Engine& engine;
    uint64_t id;
    bool loopEnabled = true;

    static constexpr int kTitleH = 18;
    static constexpr int kInfoH = 28;
    static constexpr int kTransportH = 26;
    static constexpr int kLaneH = 48;      // default lane height
    static constexpr int kLrW = 12;
    static constexpr int kCtrlW = 14;
    static constexpr int kGripW = 14;

    // Height of one track lane. This is the only thing a vertical resize changes:
    // total height is always chromeHeight() + numTracks() * laneH.
    int laneH = kLaneH;

    // waveform peak cache (per track, one min/max per pixel column of waveWidth)
    int cachedWidth = -1;
    std::vector<size_t> cachedSizes;
    size_t cachedMax = 0;
    std::vector<std::vector<juce::Range<float>>> peaks;

    // title drag / corner resize
    bool draggingTitle = false;
    bool resizingWindow = false;
    juce::Point<int> dragMouseStart, dragBoundsStart;
    juce::Point<int> resizeSizeStart;

    // selection drag
    bool selecting = false;
    int  selAnchorTrack = 0;
    size_t selAnchorByte = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowComponent)
};
