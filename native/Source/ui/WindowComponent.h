// WindowComponent — one sound Window drawn on the canvas, System 7 styling.
//
// Draws the UI-authoritative document (engine.editDoc), edits it in place, and
// publishes via engine.commitEdit — so edits apply mid-playback with no glitch.
// Interactions: title drag, rename (double-click the title), close, transport
// (Space toggles play/pause on the focused Window), the per-track output-channel
// selector, add/move/trash track, selection drag, and ⌘X/⌘C/⌘V clipboard.
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

#include <algorithm>
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

    /** The width this window ASKS for (grip drag). In the grid its rendered width is
        the column's, and a grow-box drag sets that for the whole column — so the
        two normally agree. */
    int preferredWidth() const noexcept { return desiredW; }
    int preferredHeight() const noexcept;

    /** Adopt a desired width without touching the height, for the canvas to keep a
        column in step. The rendered width follows at the next layout pass. */
    void setDesiredWidth (int width) noexcept { desiredW = std::max (kMinWidth, width); }

    /** Adopt a size the canvas restored/chose, in the window's own terms. */
    void applySize (int width, int height);

    /** Write this window's desired size and position back to the engine's Window. */
    void storeGeometry();

    /** Flat colour this window is painted in. */
    void setTint (juce::Colour c) { tint = c; repaint(); }

    /** Shared clipboard slot (owned by the canvas). */
    std::unique_ptr<se::SoundDocument>* clipboard = nullptr;

    void refresh();
    std::function<void (uint64_t)> onClose;
    /** Size changed (user grip drag, or a track added/removed) — relayout the canvas. */
    std::function<void()> onGeometryChanged;
    /** The grip drag changed the width this window asks for. In the grid that is a
        column-wide change, so it goes out ahead of onGeometryChanged's relayout. */
    std::function<void (int)> onWidthChanged;
    /** Title-bar drag in progress, with the pointer in canvas coords — the canvas
        re-targets this window's grid cell and reflows around it. */
    std::function<void (juce::Point<int>)> onDragged;
    /** Title-bar drag finished — settle into the layout. */
    std::function<void()> onDropped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    // The active window is the one holding keyboard focus — i.e. the one ⌘X/⌘C/⌘V
    // and Delete act on. Highlighting it makes that otherwise invisible.
    void focusGained (FocusChangeType) override;
    void focusLost   (FocusChangeType) override;
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
    /** One cell of a track's output-channel selector: 0 = L, 1 = L+R, 2 = R. */
    juce::Rectangle<int> outRect (int t, int which) const;
    /** Which cell a track's routing currently reads as, or -1 for neither channel. */
    static int outSelection (const se::Track&) noexcept;
    juce::Rectangle<int> ctrlRect (int t, int which) const;   // 0=up 1=down 2=trash
    juce::Rectangle<int> waveRect (int t) const;
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> transportButton (int i) const;       // 0..kNumTransportButtons-1
    /** Draw transport symbol `which` inside `button`, in the current colour. The
        symbols are paths, not text: ● ■ ▶ ↻ have wildly different metrics at any
        one point size, so glyphs cannot be made to match. */
    void drawTransportSymbol (juce::Graphics&, int which, juce::Rectangle<int> button,
                              bool playing) const;
    juce::Rectangle<int> closeBoxBounds() const;
    /** The title bar's name area — what the name is drawn in, and edited in. */
    juce::Rectangle<int> nameBounds() const;

    void togglePlayPause();
    void beginNameEdit();
    void endNameEdit (bool keepChanges);
    int    waveWidth() const noexcept;
    size_t byteAtX (int x, size_t maxSize) const noexcept;
    int    xAtByte (size_t b, size_t maxSize) const noexcept;

    se::Engine& engine;
    uint64_t id;
    bool loopEnabled = true;
    // 2x speed. Mirrors Transport::advanceScale, which play() resets to 1.0 — so
    // anything that (re)starts playback must clear this too.
    bool advanceEnabled = false;

    // Transport: record / stop / play-pause / 2x / loop, square, centred as a
    // group. The strip is sized from the buttons, not the other way round.
    static constexpr int kNumTransportButtons = 5;
    static constexpr int kBtnSize = 28;
    static constexpr int kBtnGap = 5;
    static constexpr int kBtnMargin = 4;

    static constexpr int kTitleH = 18;
    static constexpr int kInfoH = 28;
    static constexpr int kTransportH = kBtnSize + 2 * kBtnMargin;
    static constexpr int kLaneH = 48;      // default lane height
    // Wide enough for "L+R" — the output-channel selector is three stacked cells,
    // not two boxes, and a label that cannot be read is not a control.
    static constexpr int kLrW = 22;
    static constexpr int kCtrlW = 14;
    static constexpr int kGripW = 14;

    // Border weight carries the state: plain, hovered, active (keyboard focus).
    static constexpr int kBorderIdle = 1;
    static constexpr int kBorderHover = 2;
    static constexpr int kBorderActive = 4;

    // Height of one track lane. This is the only thing a vertical resize changes:
    // total height is always chromeHeight() + numTracks() * laneH.
    int laneH = kLaneH;

    // Width this window asks for; the grid may render it wider. See preferredWidth().
    int desiredW = kDefaultWidth;

    juce::Colour tint { 0xffbfbfbf };

    // waveform peak cache (per track, one min/max per pixel column of waveWidth)
    int cachedWidth = -1;
    std::vector<size_t> cachedSizes;
    size_t cachedMax = 0;
    std::vector<std::vector<juce::Range<float>>> peaks;

    bool hovered = false;

    // last drawn playhead state, so refresh() can skip idle repaints
    bool   lastPlaying = false;
    size_t lastPlayhead = 0;

    // title drag / corner resize
    bool draggingTitle = false;
    bool resizingWindow = false;
    juce::Point<int> dragMouseStart, dragBoundsStart;
    juce::Point<int> resizeSizeStart;

    // inline rename, over the title bar
    std::unique_ptr<juce::TextEditor> nameEditor;

    // selection drag
    bool selecting = false;
    int  selAnchorTrack = 0;
    size_t selAnchorByte = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowComponent)
};
