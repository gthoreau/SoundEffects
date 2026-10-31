// WindowComponent — one sound Window drawn on the canvas, in the 0.4.0 Mac OS 8
// design ("Sound-window" 135:4722, every measurement from that component).
//
// Draws the UI-authoritative document (engine.editDoc), edits it in place, and
// publishes via engine.commitEdit — so edits apply mid-playback with no glitch.
//
// From the top: the title bar (close box, name, M and S); the status row (mixer
// slot, rate, bits, tracks, length, then the selection readout: start, length, end
// and their unit); the tracks, each with its two output speakers on the left and
// its drag handle on the right, with the new-track handle and the trash in a column
// of their own; the zoom bar (zoom out, zoom in, whole sound, then ◀ overview ▶);
// and the transport (Source, record, stop, play with its direction, pause, fast
// forward, loop), with the grow box in the corner.
//
// Tracks are moved, deleted, duplicated and added by DRAGGING: a track's handle
// onto another place (⌥ duplicates), onto the trash, or the new-track handle into
// the column of tracks — a ghost shows where it will land.
//
// Waveforms share one byte-time axis (spanning the window's longest track) so a
// shorter track visibly ends partway and the selection lines up across tracks.
// That axis is a VIEW onto the document, not the whole of it: the zoom bar zooms
// and pans it, exactly as the original's navigator did.
//
// The chrome keeps a fixed height; resizing only stretches the tracks.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "engine/Edit.h"
#include "engine/Engine.h"
#include "engine/Recorder.h"
#include "AppPrefs.h"
#include "EditPopups.h"
#include "Os8Widgets.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

class WindowComponent : public juce::Component,
                        private AppPrefs::Listener
{
public:
    WindowComponent (se::Engine& engine, uint64_t id);
    ~WindowComponent() override;

    uint64_t windowId() const noexcept { return id; }

    static constexpr int kDefaultWidth = 533;   // the design's 523 + the border ring
    static constexpr int kMinWidth = 330;
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

    /** The theme colour this window is painted in: its face and its speakers. */
    void setTint (juce::Colour c) { if (c != tint) { tint = c; repaint(); } }

    /** The mixer slot shown in the status row's little box. */
    void setSlotNumber (int n) { if (slot != n) { slot = n; repaint(); } }

    /** Canvas selection state — what the effects panel acts on. */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** The audio was rewritten under us — drop the cached peaks.
        The cache is keyed on track SIZES, so it cannot notice a destructive effect
        that rewrote every sample without changing the length. Amplify is exactly
        that, and without this the window keeps drawing the old waveform. */
    void invalidateWaveform();

    /** The recorder the editor owns, and how a take is set up for this Window (its
        audio source and conversion). Null in a build with no recording. */
    se::Recorder* recorder = nullptr;
    std::function<bool (uint64_t window, se::RecordSetup&)> recordSetupFor;
    /** Is this Window's source usable right now (an External one needs live input)? */
    std::function<bool (uint64_t window)> recordReady;
    /** A take is running or paused in THIS Window. */
    bool isRecording() const;
    static juce::MouseCursor trackDragCursor (os8::icon::Id);

    /** Shared clipboard slot (owned by the canvas). */
    std::unique_ptr<se::SoundDocument>* clipboard = nullptr;

    /** What the application menus and their shortcuts can ask a Window to do. The
        Window does not read those keys itself: the shortcut table (AppCommands)
        binds them once for the whole app and routes them here, to the selected
        Window. */
    enum class Action
    {
        playPause, stop, toggleLoop, toggleAdvance,
        playForward, playBackward, playPingPong, cycleDirection,   // the Window's playback direction
        selectAll, cut, copy, paste, duplicate, clear, crop, insertSilence,
        leaveSilence, shiftSound,   // open their popup; the edit happens on Apply
        extendToBeginning, extendToEnd, extendAbove, extendBelow,
        selectExactly, selectPeak,  // popups too
        selectSilence, setLoop, selectLoop,
        selectAudioSource,   // the Source button / ⌘⇧R: the popup
        record,     // start a take, or carry on with a paused one
        rename
    };
    /** Do it; false when it does not apply right now (nothing to paste, say). */
    bool perform (Action);
    /** Would perform() do anything? Drives the menus' greying. */
    bool canPerform (Action) const;

    bool isPlaying() const;
    bool isLoopOn() const noexcept    { return loopEnabled; }
    bool isAdvanceOn() const noexcept { return advanceEnabled; }
    se::PlayDirection direction() const;
    void setDirection (se::PlayDirection);

    void refresh();
    std::function<void (uint64_t)> onClose;
    /** Clicked — the canvas makes this the selected Window. Separate from keyboard
        focus on purpose: focus moves away the moment a side panel is used, and the
        thing a panel acts on must not evaporate under it. */
    std::function<void (uint64_t)> onSelected;
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
    /** The title-bar press became a drag: the pointer has travelled far enough
        (reorder::kLiftThreshold) that it is not a click. Fired before the first
        onDragged. */
    std::function<void()> onDragStarted;
    /** The canvas cancelled the drag (Escape, the board changed): ignore the rest
        of this press. */
    void abandonTitleDrag();
    /** Source was clicked: the editor shows the Select Audio Source popup for this Window. */
    std::function<void (uint64_t)> onSourceRequested;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    se::SoundDocument* doc() const noexcept { return engine.editDoc (id); }
    se::Window* engineWindow() const noexcept { return engine.windowById (id); }
    // Publish the edit AND invalidate the waveform cache (content may have changed
    // without a size change, e.g. silence — the size-based cache wouldn't notice).
    void commit()   { engine.commitEdit (id); cachedWidth = -1; repaint(); }
    void resizeToFit();
    /** Show one of the Edit/Selection popups centred over this Window, field focused. */
    void openPopup (editpopups::Built);
    void ensurePeaks (se::SoundDocument& d);
    /** Mouse position in canvas ("world") coordinates — stable while this window
        moves or the canvas is zoomed, unlike local or screen coordinates. */
    juce::Point<int> parentPos (const juce::MouseEvent&) const;
    void appPrefChanged (const juce::String& key) override;

    // --- geometry (Figma "Sound-window", in body coordinates) -------------------
    //
    // The component is the window body plus a ring around it for the border, which
    // is 2 px at rest, 3 px under the pointer and 5 px when selected — growing
    // OUTWARD, as the design draws it. The ring is always there, so neighbours in
    // the grid never overlap, and clicks in its unused part go through to the canvas.
    static constexpr int kRing = 5;
    static constexpr int kTitleH = 21;
    static constexpr int kStatusH = 30;
    static constexpr int kZoomH = 26;
    static constexpr int kTransportH = 34;
    static constexpr int kBottomH = 4;
    static constexpr int kSideInset = 4;      // window-content's spacers, left and right
    static constexpr int kVoicesW = 41;       // the speaker column
    static constexpr int kMoveW = 24;         // the drag-handle column
    static constexpr int kNewDelW = 24;       // new-track / trash column
    static constexpr int kLaneH = 64;         // default track height
    static constexpr int chromeHeight() noexcept
    { return kTitleH + kStatusH + kZoomH + kTransportH + kBottomH + 2 * kRing; }

    juce::Rectangle<int> body() const;
    juce::Rectangle<int> titleBar() const;
    juce::Rectangle<int> statusArea() const;
    juce::Rectangle<int> tracksArea() const;
    juce::Rectangle<int> trackRow (int t) const;
    juce::Rectangle<int> speakerBox (int t, bool right) const;
    juce::Rectangle<int> waveRect (int t) const;
    juce::Rectangle<int> moveHandle (int t) const;
    juce::Rectangle<int> newTrackHandle() const;
    juce::Rectangle<int> trashRect() const;
    juce::Rectangle<int> zoomArea() const;
    /** 0 zoom out, 1 zoom in, 2 whole sound, 3 ◀, 4 ▶. */
    juce::Rectangle<int> zoomButton (int i) const;
    /** The white overview between the arrows: the whole sound at a glance. */
    juce::Rectangle<int> overviewRect() const;
    juce::Rectangle<int> transportArea() const;
    juce::Rectangle<int> gripRect() const;
    juce::Rectangle<int> closeBox() const;
    juce::Rectangle<int> muteBox() const;
    juce::Rectangle<int> soloBox() const;
    /** The title bar's name area — what the name is drawn in, and edited in. */
    juce::Rectangle<int> nameBounds() const;
    int numTracks() const noexcept;

    // --- drawing ---------------------------------------------------------------
    void paintTitle (juce::Graphics&, const se::SoundDocument&, bool muted, bool soloed);
    void paintStatus (juce::Graphics&, const se::SoundDocument&);
    void paintTracks (juce::Graphics&, const se::SoundDocument&, bool muted);
    void paintZoomBar (juce::Graphics&, const se::SoundDocument&);
    void paintGrip (juce::Graphics&);

    // --- the selection readout --------------------------------------------------
    /** Bytes → the readout's unit (s / ms / samples) and back. */
    double toUnits (size_t bytes) const;
    size_t fromUnits (double value) const;
    juce::String formatUnits (double value) const;
    void configureSelectionFields();
    void updateSelectionFields (bool force = false);
    void commitSelectionField (int which);   // 0 start, 1 length, 2 end

    // --- transport ----------------------------------------------------------------
    void togglePlayPause();
    void updateTransportButtons();

    /** Select every track end to end (triple-click, ⌘A) and publish it. */
    void selectAllAudio();
    void beginNameEdit();
    void endNameEdit (bool keepChanges);
    void updateHover();

    // --- the shared byte axis and the view ----------------------------------------
    int    waveOriginX() const noexcept;
    int    waveWidth() const noexcept;
    size_t byteAtX (int x, size_t maxSize) const noexcept;
    /** Where a byte lands on the axis. Zoomed in this is routinely OFF the lane, on
        either side — every caller has to clamp or skip. */
    int    xAtByte (size_t b, size_t maxSize) const noexcept;
    /** Smallest fraction of the document worth showing: one frame per pixel column. */
    double minSpanFrac() const noexcept;
    void   clampView() noexcept;
    void   zoomBy (double factor);
    void   panBy (double pages);
    void   setViewCentreFromOverviewX (int x);
    /** Which track a y falls in, clamped to the real ones. */
    int    trackAtY (int y, int numTracks) const noexcept;
    /** The gap a dragged track or new track would land in: 0 = above the first. */
    int    insertionAtY (int y, int numTracks) const noexcept;

    se::Engine& engine;
    uint64_t id;
    bool loopEnabled = true;
    // 2x speed. Mirrors Transport::advanceScale, which play() resets to 1.0 — so
    // anything that (re)starts playback must clear this too.
    bool advanceEnabled = false;

    int laneH = kLaneH;
    int desiredW = kDefaultWidth;
    int slot = 0;
    juce::Colour tint { os8::colour::gray100 };   // Placeholder-gray-100 until painted

    // The visible slice of the document, as FRACTIONS of the longest track rather
    // than byte counts — so a cut or a paste rescales the view instead of leaving
    // it pointing off the end of a document that just got shorter.
    double viewStartFrac = 0.0;
    double viewSpanFrac  = 1.0;

    // waveform peak cache (per track, one min/max per pixel column of waveWidth)
    int cachedWidth = -1;
    std::vector<size_t> cachedSizes;
    size_t cachedMax = 0;
    double cachedStartFrac = -1.0, cachedSpanFrac = -1.0;
    std::vector<std::vector<juce::Range<float>>> peaks;

    bool hovered = false;
    bool selected = false;

    // last drawn state, so refresh() can skip idle repaints
    bool   lastPlaying = false, lastMuted = false, lastSoloed = false, lastSilenced = false;
    std::vector<int> lastHeadX;                 // per track, kNoHead when none is drawn
    size_t lastBlockSt = 0, lastBlockEnd = 0;   // the overview's block, as last drawn
    static constexpr int kNoHead = std::numeric_limits<int>::min();
    /** Where each track's playhead is drawn right now (kNoHead: none), as paintTracks does it. */
    std::vector<int> playheadXs() const;
    size_t lastSelSt = (size_t) -1, lastSelEnd = (size_t) -1;
    int    lastUnit = -1;

    // title drag / corner resize
    bool draggingTitle = false;
    bool titleLifted = false;     // ...and it has travelled past the lift threshold
    bool resizingWindow = false;
    juce::Point<int> dragMouseStart, dragBoundsStart;
    juce::Point<int> resizeSizeStart;

    // inline rename, over the title bar
    std::unique_ptr<juce::TextEditor> nameEditor;

    // zoom bar: which button is held, and overview scrubbing
    int pressedZoomButton = -1;
    bool overviewDragging = false;
    // title boxes held
    int pressedTitleBox = -1;   // 0 close, 1 mute, 2 solo

    // selection drag
    bool selecting = false;
    int  selAnchorTrack = 0;
    size_t selAnchorByte = 0;

    // track drags: a track's handle (move / delete / ⌥ duplicate) or the new-track one
    enum class TrackDrag { none, move, add };
    TrackDrag trackDrag = TrackDrag::none;
    int dragTrack = -1;
    juce::Point<int> dragPoint;          // the pointer, local
    bool dragDuplicate = false;

    /** Selects this Window when one of its controls is clicked. A separate listener
        on the children — listening to our own events too would deliver every click
        on the Window twice. */
    struct ChildClicks : juce::MouseListener
    {
        explicit ChildClicks (WindowComponent& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent&) override { if (owner.onSelected) owner.onSelected (owner.id); }
        WindowComponent& owner;
    };
    ChildClicks childClicks { *this };

    // the child controls
    Os8TextField startField { os8::FieldSize::xSmall }, lengthField { os8::FieldSize::xSmall },
                 endField { os8::FieldSize::xSmall };
    Os8Dropdown unitDropdown { Os8Dropdown::Style::mini };
    Os8Button sourceButton { "Source" };
    Os8TransportButton recordButton { os8::icon::Id::record }, stopButton { os8::icon::Id::stop },
                       pauseButton { os8::icon::Id::pause }, fastForwardButton { os8::icon::Id::fastForward },
                       loopButton { os8::icon::Id::loopBig };
    Os8ComboPlay playButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowComponent)
};
