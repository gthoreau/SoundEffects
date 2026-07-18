// WindowComponent — one sound Window drawn on the canvas, System 7 styling.
//
// Draws the UI-authoritative document (engine.editDoc), edits it in place, and
// publishes via engine.commitEdit — so edits apply mid-playback with no glitch.
// Interactions: title drag, close, transport, L/R output toggles, add/move/trash
// track, selection drag, and ⌘X/⌘C/⌘V clipboard.
//
// Waveforms share one byte-time axis (spanning the window's longest track) so a
// shorter track visibly ends partway and the selection lines up across tracks.

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
    int preferredWidth() const noexcept { return 380; }
    int preferredHeight() const noexcept;

    /** Shared clipboard slot (owned by the canvas). */
    std::unique_ptr<se::SoundDocument>* clipboard = nullptr;

    void refresh();
    std::function<void (uint64_t)> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    se::SoundDocument* doc() const noexcept { return engine.editDoc (id); }
    void commit()   { engine.commitEdit (id); }
    void resizeToFit();
    void ensurePeaks (se::SoundDocument& d);

    // geometry
    int numTracks() const noexcept;
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
    static constexpr int kLaneH = 48;
    static constexpr int kLrW = 12;
    static constexpr int kCtrlW = 14;

    // waveform peak cache (per track, one min/max per pixel column of waveWidth)
    int cachedWidth = -1;
    std::vector<size_t> cachedSizes;
    size_t cachedMax = 0;
    std::vector<std::vector<juce::Range<float>>> peaks;

    // title drag
    bool draggingTitle = false;
    juce::Point<int> dragMouseStart, dragBoundsStart;

    // selection drag
    bool selecting = false;
    int  selAnchorTrack = 0;
    size_t selAnchorByte = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowComponent)
};
