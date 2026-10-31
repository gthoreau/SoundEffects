#include "WindowComponent.h"

#include "engine/SampleFormat.h"
#include "Os8Icons.h"
#include "Reorder.h"

#include <algorithm>
#include <cmath>

using juce::Colour;
using juce::Graphics;
using juce::Rectangle;

namespace {
using namespace os8::colour;

const Colour kViewBar  { 0xff1313f5 };   // overview: the slice of sound on screen
const Colour kWaveMuted = gray300;       // a muted Window's waveform

// The units the selection readout can speak, in AppPrefs::timeUnit order.
const juce::StringArray kUnitNames  { "Seconds", "Milliseconds", "Samples" };
const juce::StringArray kUnitShorts { "s.", "ms", "smp" };

/** |a - b| per channel: Figma's DIFFERENCE blend, for the selection's inversion. */
Colour difference (Colour a, Colour b)
{
    const auto d = [] (juce::uint8 x, juce::uint8 y) { return (juce::uint8) std::abs ((int) x - (int) y); };
    return Colour (d (a.getRed(), b.getRed()), d (a.getGreen(), b.getGreen()), d (a.getBlue(), b.getBlue()));
}

/** `top` at `alpha` over `base` with Figma's LINEAR_BURN (base + top - 1, clamped). */
Colour linearBurn (Colour base, Colour top, float alpha)
{
    const auto burn = [] (juce::uint8 x, juce::uint8 y) { return (juce::uint8) std::max (0, (int) x + (int) y - 255); };
    const Colour burnt (burn (base.getRed(), top.getRed()), burn (base.getGreen(), top.getGreen()),
                        burn (base.getBlue(), top.getBlue()));
    return base.interpolatedWith (burnt, alpha);
}
} // namespace

WindowComponent::WindowComponent (se::Engine& e, uint64_t wid)
    : engine (e), id (wid)
{
    setWantsKeyboardFocus (true);

    // A click on any control here also selects this Window: the controls take the
    // mouse, so the Window hears about it through childClicks.
    for (auto* c : std::initializer_list<juce::Component*> { &startField, &lengthField, &endField, &unitDropdown,
                                                            &sourceButton, &recordButton, &stopButton, &playButton,
                                                            &pauseButton, &fastForwardButton, &loopButton })
    {
        addAndMakeVisible (c);
        c->addMouseListener (&childClicks, true);
    }

    startField.onCommit  = [this] (const juce::String&) { commitSelectionField (0); };
    lengthField.onCommit = [this] (const juce::String&) { commitSelectionField (1); };
    endField.onCommit    = [this] (const juce::String&) { commitSelectionField (2); };

    unitDropdown.setItems (kUnitNames, kUnitShorts);
    unitDropdown.setSelectedIndex (AppPrefs::get().timeUnit());
    unitDropdown.onChange = [] (int i) { AppPrefs::get().setTimeUnit (i); };
    AppPrefs::get().addListener (this);
    configureSelectionFields();

    sourceButton.setTextStyle (os8::TextStyle::smallLabel);
    sourceButton.onClick = [this] { perform (Action::selectAudioSource); };
    stopButton.onClick = [this] { perform (Action::stop); };
    playButton.onPlay = [this] { if (! isPlaying()) togglePlayPause(); };
    playButton.onDirectionChange = [this] (Os8ComboPlay::Direction d)
    {
        setDirection (d == Os8ComboPlay::Direction::backward ? se::PlayDirection::backward
                      : d == Os8ComboPlay::Direction::pingPong ? se::PlayDirection::pingPong
                                                               : se::PlayDirection::forward);
    };
    recordButton.onClick = [this] { perform (Action::record); };
    pauseButton.onClick = [this]
    {
        if (isRecording()) { togglePlayPause(); return; }   // pauses / resumes the take
        auto* w = engineWindow();
        if (w != nullptr && w->transport->uiState() != se::TransportState::stopped)
            engine.pause (id);   // playing <-> paused
        updateTransportButtons();
    };
    fastForwardButton.onClick = [this] { perform (Action::toggleAdvance); };
    loopButton.onClick = [this] { perform (Action::toggleLoop); };
    updateTransportButtons();
}

WindowComponent::~WindowComponent()
{
    AppPrefs::get().removeListener (this);
}

int WindowComponent::numTracks() const noexcept
{
    auto* d = doc();
    return d ? std::max (1, d->numTracks()) : 1;
}

int WindowComponent::preferredHeight() const noexcept
{
    return chromeHeight() + numTracks() * laneH;
}

void WindowComponent::applySize (int width, int height)
{
    const int n = numTracks();
    desiredW = std::max (kMinWidth, width);
    laneH = std::clamp ((height - chromeHeight()) / std::max (1, n), kMinLaneH, kMaxLaneH);
    setSize (desiredW, chromeHeight() + n * laneH);
}

void WindowComponent::storeGeometry()
{
    if (auto* w = engine.windowById (id))
    {
        w->x = (float) getX();        w->y = (float) getY();
        w->width = (float) desiredW;  w->height = (float) getHeight();
    }
}

void WindowComponent::refresh()
{
    if (doc() == nullptr) { if (onClose) onClose (id); return; }

    updateSelectionFields();
    updateTransportButtons();

    // The editor's timer is only here to animate the playhead and to follow the
    // mixer, so repaint only when something that MOVES has moved. Everything else
    // (edits, selection, toggles, resize) already repaints itself where it changes.
    auto* w = engineWindow();
    const bool playing = w != nullptr && w->transport->uiState() == se::TransportState::playing;
    const bool muted = w != nullptr && w->muted.load (std::memory_order_relaxed);
    const bool soloed = w != nullptr && w->soloed.load (std::memory_order_relaxed);
    const bool silenced = muted || (! soloed && engine.anyWindowSoloed());
    const size_t blockSt  = w != nullptr ? w->transport->uiPlaySt()  : 0;
    const size_t blockEnd = w != nullptr ? w->transport->uiPlayEnd() : 0;
    auto heads = playheadXs();

    if (playing != lastPlaying || muted != lastMuted || soloed != lastSoloed || silenced != lastSilenced)
    {
        // The whole face changes (the play button's state, the mute veil): all of it.
        lastPlaying = playing;
        lastMuted = muted;
        lastSoloed = soloed;
        lastSilenced = silenced;
        lastHeadX = std::move (heads);
        lastBlockSt = blockSt; lastBlockEnd = blockEnd;
        repaint();
        return;
    }

    // Rolling, only the playheads and the overview's block move. Repaint the column
    // each playhead leaves and the one it lands on, track by track, and the block.
    // A full repaint here would redraw every transport button and field 30 times a
    // second, and the canvas under them (perf audit P2).
    // The canvas zooms its Windows with a transform: below 100 % a 1 px column is
    // less than a screen pixel and would round to nothing, leaving the old line
    // behind. Pad it so it always spans whole device pixels, antialiasing included.
    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    const int pad = 1 + (int) std::ceil (1.0f / std::max (0.05f, scale));
    const auto column = [this, pad] (int x, Rectangle<int> rows)
    {
        if (x != kNoHead) repaint (x - pad, rows.getY(), 1 + 2 * pad, rows.getHeight());
    };
    for (size_t t = 0; t < heads.size(); ++t)
    {
        const int was = t < lastHeadX.size() ? lastHeadX[t] : kNoHead;
        if (heads[t] == was) continue;
        column (was, trackRow ((int) t));
        column (heads[t], trackRow ((int) t));
    }
    for (size_t t = heads.size(); t < lastHeadX.size(); ++t)   // a track gone since
        column (lastHeadX[t], tracksArea());
    lastHeadX = std::move (heads);

    if (blockSt != lastBlockSt || blockEnd != lastBlockEnd)
    {
        lastBlockSt = blockSt; lastBlockEnd = blockEnd;
        repaint (overviewRect());
    }
}

std::vector<int> WindowComponent::playheadXs() const
{
    std::vector<int> xs;
    auto* d = doc();
    auto* w = engineWindow();
    if (d == nullptr || w == nullptr || w->transport->uiState() != se::TransportState::playing) return xs;

    const size_t maxSize = d->maxTrackSize();
    xs.assign ((size_t) d->numTracks(), kNoHead);
    for (int t = 0; t < d->numTracks(); ++t)
    {
        size_t ph = 0;
        if (d->tracks[(size_t) t].data.empty() || ! w->transport->uiTrackPosition (t, ph)) continue;
        const int px = xAtByte (ph, maxSize);
        const auto lane = waveRect (t).reduced (1, 0);
        if (px >= lane.getX() && px < lane.getRight()) xs[(size_t) t] = px;
    }
    return xs;
}

void WindowComponent::resizeToFit()
{
    // Keep the rendered width (the grid owns it); only the height follows the tracks.
    setSize (getWidth() > 0 ? getWidth() : desiredW, preferredHeight());
    cachedWidth = -1;
    summaryStale = true;
    storeGeometry();
    if (onGeometryChanged) onGeometryChanged();
    repaint();
}

// --- geometry ---------------------------------------------------------------

Rectangle<int> WindowComponent::body() const { return getLocalBounds().reduced (kRing); }

Rectangle<int> WindowComponent::titleBar() const { return body().withHeight (kTitleH); }

Rectangle<int> WindowComponent::statusArea() const
{
    const auto b = body();
    return { b.getX() + kSideInset, b.getY() + kTitleH, b.getWidth() - 2 * kSideInset, kStatusH };
}

Rectangle<int> WindowComponent::tracksArea() const
{
    const auto s = statusArea();
    return { s.getX(), s.getBottom(), s.getWidth(), numTracks() * laneH };
}

Rectangle<int> WindowComponent::trackRow (int t) const
{
    const auto a = tracksArea();
    return { a.getX(), a.getY() + t * laneH, a.getWidth() - kNewDelW, laneH };
}

Rectangle<int> WindowComponent::speakerBox (int t, bool right) const
{
    // The speakers sit at 6.5 and 21.5 px into the row in Figma; rounded to whole
    // pixels, as pixel art must be.
    const auto row = trackRow (t);
    return { row.getX() + (right ? 22 : 7), row.getY() + (laneH - 13) / 2, 13, 13 };
}

Rectangle<int> WindowComponent::waveRect (int t) const
{
    const auto row = trackRow (t);
    return { row.getX() + kVoicesW, row.getY(), row.getWidth() - kVoicesW - kMoveW, laneH };
}

Rectangle<int> WindowComponent::moveHandle (int t) const
{
    const auto w = waveRect (t);
    return { w.getRight() + 4, w.getY() + (laneH - 13) / 2, 16, 11 };
}

Rectangle<int> WindowComponent::newTrackHandle() const
{
    const auto a = tracksArea();
    return { a.getRight() - kNewDelW + 4, a.getY() + 2, 16, 11 };
}

Rectangle<int> WindowComponent::trashRect() const
{
    const auto a = tracksArea();
    return { a.getRight() - kNewDelW + 5, a.getBottom() - 18, 14, 16 };
}

Rectangle<int> WindowComponent::zoomArea() const
{
    const auto a = tracksArea();
    return { a.getX(), a.getBottom(), a.getWidth(), kZoomH };
}

Rectangle<int> WindowComponent::zoomButton (int i) const
{
    const auto z = zoomArea();
    const int x0 = z.getX() + kVoicesW, y = z.getY() + 5;
    switch (i)
    {
        case 0: return { x0,      y, 16, 16 };
        case 1: return { x0 + 19, y, 16, 16 };
        case 2: return { x0 + 38, y, 16, 16 };
        case 3: return { x0 + 63, y, 16, 16 };
        default: return { z.getRight() - 65, y, 16, 16 };
    }
}

Rectangle<int> WindowComponent::overviewRect() const
{
    const int x0 = zoomButton (3).getRight() + 3;
    const int x1 = zoomButton (4).getX() - 3;
    return { x0, zoomButton (3).getY(), std::max (8, x1 - x0), 16 };
}

Rectangle<int> WindowComponent::transportArea() const
{
    const auto z = zoomArea();
    return { z.getX(), z.getBottom(), z.getWidth(), kTransportH };
}

Rectangle<int> WindowComponent::gripRect() const
{
    const auto b = body();
    return { b.getRight() - 14, b.getBottom() - 14, 10, 10 };
}

Rectangle<int> WindowComponent::closeBox() const { return { body().getX() + 6, body().getY() + 5, 11, 11 }; }
Rectangle<int> WindowComponent::muteBox() const  { return { body().getRight() - 34, body().getY() + 5, 11, 11 }; }
Rectangle<int> WindowComponent::soloBox() const  { return { body().getRight() - 16, body().getY() + 5, 11, 11 }; }

Rectangle<int> WindowComponent::nameBounds() const
{
    const auto t = titleBar();
    return { t.getX() + 30, t.getY() + 3, std::max (40, muteBox().getX() - 8 - (t.getX() + 30)), 15 };
}

int WindowComponent::waveOriginX() const noexcept { return waveRect (0).getX(); }
int WindowComponent::waveWidth() const noexcept   { return std::max (1, waveRect (0).getWidth()); }

void WindowComponent::resized()
{
    cachedWidth = -1;
    clampView();

    // The selection readout, right-aligned in the status row: start, length and end,
    // each behind its icon, then the unit. Spacing as "selection-info" 526:4846.
    const auto s = statusArea();
    const int right = s.getRight() - 8;
    unitDropdown.setBounds (right - 36, s.getY() + 9, 36, os8::kMiniDropdownHeight);
    endField.setBounds    (right - 90,  s.getY() + 8, 50, 15);
    lengthField.setBounds (right - 162, s.getY() + 8, 50, 15);
    startField.setBounds  (right - 239, s.getY() + 8, 50, 15);

    // The transport, centred as one group ("Transport-controls" 589:8301).
    const auto t = transportArea();
    const int x0 = t.getCentreX() - 114;
    sourceButton.setBounds (x0, t.getY() + 9, 50, 20);
    // Transport faces are 25 px with a 1 px outside stroke; the component carries
    // the stroke, so it sits 1 px up and left of the face.
    const auto face = [&t, x0] (juce::Component& c, int dx) { c.setBounds (x0 + dx - 1, t.getY() + 6 - 1, 27, 27); };
    face (recordButton, 58);
    face (stopButton, 85);
    playButton.setTopLeftPosition (x0 + 112, t.getY() + 5);
    face (pauseButton, 148);
    face (fastForwardButton, 175);
    face (loopButton, 202);
}

bool WindowComponent::hitTest (int x, int y)
{
    // The border ring outside the 2 px frame is empty until hovered or selected:
    // clicks there belong to the canvas.
    return body().expanded (2).contains (x, y);
}

// --- the view ---------------------------------------------------------------

double WindowComponent::minSpanFrac() const noexcept
{
    auto* d = doc();
    const size_t maxSize = d != nullptr ? d->maxTrackSize() : 0;
    if (maxSize == 0) return 1.0;
    // One frame per pixel column is the floor. Zoomed in further, every column
    // would be the same sample repeated — more magnification, no more waveform.
    const double frames = (double) waveWidth() * (double) std::max (1, d->frameSize());
    return std::min (1.0, frames / (double) maxSize);
}

void WindowComponent::clampView() noexcept
{
    viewSpanFrac  = std::clamp (viewSpanFrac, minSpanFrac(), 1.0);
    viewStartFrac = std::clamp (viewStartFrac, 0.0, 1.0 - viewSpanFrac);
}

void WindowComponent::zoomBy (double factor)
{
    // Zoom about the CENTRE of the view: whatever you were looking at is still
    // what you are looking at, only nearer or further away.
    const double centre = viewStartFrac + viewSpanFrac * 0.5;
    viewSpanFrac = std::clamp (viewSpanFrac * factor, minSpanFrac(), 1.0);
    viewStartFrac = centre - viewSpanFrac * 0.5;
    clampView();
    cachedWidth = -1;
}

void WindowComponent::panBy (double pages)
{
    viewStartFrac += pages * viewSpanFrac;
    clampView();
    cachedWidth = -1;
}

void WindowComponent::setViewCentreFromOverviewX (int x)
{
    const auto o = overviewRect().reduced (2, 0);
    const double f = std::clamp ((double) (x - o.getX()) / (double) std::max (1, o.getWidth()), 0.0, 1.0);
    viewStartFrac = f - viewSpanFrac * 0.5;
    clampView();
    cachedWidth = -1;
}

size_t WindowComponent::byteAtX (int x, size_t maxSize) const noexcept
{
    if (maxSize == 0) return 0;
    const double frac = std::clamp ((double) (x - waveOriginX()) / (double) waveWidth(), 0.0, 1.0);
    const double docFrac = std::clamp (viewStartFrac + frac * viewSpanFrac, 0.0, 1.0);
    size_t b = (size_t) (docFrac * (double) maxSize);
    const size_t fs = (size_t) std::max (1, doc() ? doc()->frameSize() : 1);
    return b - (b % fs);
}

int WindowComponent::xAtByte (size_t b, size_t maxSize) const noexcept
{
    if (maxSize == 0) return waveOriginX();
    const double frac = ((double) b / (double) maxSize - viewStartFrac)
                        / std::max (1.0e-12, viewSpanFrac);
    // Deliberately unclamped: a byte outside the view has to read as off-lane so
    // callers can drop it, not pile up on the edge pretending to be visible.
    return waveOriginX() + (int) std::lround (frac * (double) waveWidth());
}

int WindowComponent::trackAtY (int y, int n) const noexcept
{
    return std::clamp ((y - tracksArea().getY()) / std::max (1, laneH), 0, std::max (0, n - 1));
}

int WindowComponent::insertionAtY (int y, int n) const noexcept
{
    const double rows = (double) (y - tracksArea().getY()) / (double) std::max (1, laneH);
    return std::clamp ((int) std::lround (rows), 0, n);
}

// --- waveform peaks (shared byte axis over the longest track) ---------------

namespace {
/** A sample as the waveform draws it: -1..1, the same float either way it is read. */
float peakSample (const uint8_t* data, size_t byte, int bps, double divisor) noexcept
{
    return (float) (se::getSample (data, byte, bps) / divisor);
}

double peakDivisor (int bps) noexcept
{
    return std::pow (2.0, std::min (se::frameSizeFor (bps), 4) * 8 - 1);
}
} // namespace

void WindowComponent::summariseBlocks (const se::SoundDocument& d, int t, size_t fromBlock, size_t toBlock)
{
    const auto& data = d.tracks[(size_t) t].data;
    const size_t fs = (size_t) d.frameSize();
    const size_t frames = data.size() / fs;
    auto& blocks = summary[(size_t) t];
    blocks.resize ((frames + kSummaryFrames - 1) / kSummaryFrames);
    toBlock = std::min (toBlock, blocks.size());

    const double divisor = peakDivisor (d.bps);
    for (size_t k = fromBlock; k < toBlock; ++k)
    {
        const size_t f0 = k * kSummaryFrames, f1 = std::min (frames, f0 + kSummaryFrames);
        float lo = peakSample (data.data(), f0 * fs, d.bps, divisor), hi = lo;
        for (size_t f = f0 + 1; f < f1; ++f)
        {
            const float s = peakSample (data.data(), f * fs, d.bps, divisor);
            lo = std::min (lo, s);
            hi = std::max (hi, s);
        }
        blocks[k] = { lo, hi };
    }
}

void WindowComponent::invalidateWaveformFrames (const std::vector<int>& tracks, size_t fromFrame, size_t toFrame)
{
    auto* d = doc();
    if (d == nullptr || summaryStale || (int) summary.size() != d->numTracks()) { invalidateWaveform(); return; }

    // Only the take's tracks changed, and only there (plus whatever it appended):
    // re-read those blocks, and let the columns follow from the summary.
    for (int t : tracks)
    {
        if (t < 0 || t >= d->numTracks()) continue;
        const size_t had = summary[(size_t) t].size();
        const size_t from = std::min (fromFrame / kSummaryFrames, had > 0 ? had - 1 : 0);
        summariseBlocks (*d, t, from, (toFrame + kSummaryFrames - 1) / kSummaryFrames);
        summariseBlocks (*d, t, had > 0 ? had - 1 : 0, summary[(size_t) t].size());   // the grown tail
        summarySizes[(size_t) t] = d->tracks[(size_t) t].data.size();
    }

    cachedWidth = -1;
    if (getHeight() != preferredHeight()) { resizeToFit(); return; }
    repaint();
}

void WindowComponent::ensurePeaks (se::SoundDocument& d)
{
    const int n = d.numTracks();
    const int w = waveWidth();
    const size_t maxSize = d.maxTrackSize();

    bool changed = ((int) cachedSizes.size() != n) || (cachedWidth != w) || (cachedMax != maxSize)
                   || ! juce::exactlyEqual (cachedStartFrac, viewStartFrac)
                   || ! juce::exactlyEqual (cachedSpanFrac, viewSpanFrac);
    if (! changed)
        for (int t = 0; t < n; ++t)
            if (cachedSizes[(size_t) t] != d.tracks[(size_t) t].data.size()) { changed = true; break; }
    if (! changed) return;

    // The summary first: whole tracks whose content may have changed.
    if (summaryStale || (int) summary.size() != n)
    {
        summary.assign ((size_t) n, {});
        summarySizes.assign ((size_t) n, (size_t) -1);
        summaryStale = false;
    }
    for (int t = 0; t < n; ++t)
        if (summarySizes[(size_t) t] != d.tracks[(size_t) t].data.size())
        {
            summary[(size_t) t].clear();
            summariseBlocks (d, t, 0, (size_t) -1);
            summarySizes[(size_t) t] = d.tracks[(size_t) t].data.size();
        }

    cachedWidth = w; cachedMax = maxSize;
    cachedStartFrac = viewStartFrac; cachedSpanFrac = viewSpanFrac;
    cachedSizes.assign ((size_t) n, 0);
    peaks.assign ((size_t) n, {});

    const int bps = d.bps;
    const size_t fs = (size_t) se::frameSizeFor (bps);
    const double divisor = peakDivisor (bps);

    for (int t = 0; t < n; ++t)
    {
        const auto& track = d.tracks[(size_t) t];
        const auto& blocks = summary[(size_t) t];
        cachedSizes[(size_t) t] = track.data.size();
        auto& cols = peaks[(size_t) t];
        cols.assign ((size_t) w, juce::Range<float> (0.0f, 0.0f));
        if (maxSize == 0 || track.data.empty()) continue;
        const size_t frames = track.data.size() / fs;

        for (int x = 0; x < w; ++x)
        {
            // The column spans a slice of the VIEW, not of the whole sound.
            const double f0 = viewStartFrac + (double) x       / w * viewSpanFrac;
            const double f1 = viewStartFrac + (double) (x + 1) / w * viewSpanFrac;
            size_t b0 = (size_t) (std::clamp (f0, 0.0, 1.0) * (double) maxSize);
            size_t b1 = (size_t) (std::clamp (f1, 0.0, 1.0) * (double) maxSize);
            b0 -= b0 % fs;
            // Zoomed right in a column can be narrower than one frame, and a column
            // that reads no samples draws as a gap. Always take at least one.
            if (b1 <= b0) b1 = b0 + fs;

            // The frames the column reads: [a, e), cut at the track's own end.
            const size_t a = b0 / fs;
            const size_t e = std::min (frames, a + (b1 - b0 + fs - 1) / fs);
            if (a >= e) continue;

            // Samples up to the first block boundary, whole blocks from the summary,
            // samples after the last one. Min and max do not care how they are
            // grouped, so this is the same column the samples alone would give.
            float lo = 0.0f, hi = 0.0f; bool any = false;
            const auto take = [&lo, &hi, &any] (float l, float h)
            {
                lo = any ? std::min (lo, l) : l;
                hi = any ? std::max (hi, h) : h;
                any = true;
            };
            const size_t k0 = (a + kSummaryFrames - 1) / kSummaryFrames, k1 = e / kSummaryFrames;
            if (k0 < k1)
            {
                for (size_t f = a; f < k0 * kSummaryFrames; ++f)
                {
                    const float s = peakSample (track.data.data(), f * fs, bps, divisor);
                    take (s, s);
                }
                for (size_t k = k0; k < k1; ++k) take (blocks[k].getStart(), blocks[k].getEnd());
                for (size_t f = k1 * kSummaryFrames; f < e; ++f)
                {
                    const float s = peakSample (track.data.data(), f * fs, bps, divisor);
                    take (s, s);
                }
            }
            else
            {
                for (size_t f = a; f < e; ++f)
                {
                    const float s = peakSample (track.data.data(), f * fs, bps, divisor);
                    take (s, s);
                }
            }
            if (any) cols[(size_t) x] = { lo, hi };
        }
    }
}

// --- paint ------------------------------------------------------------------

void WindowComponent::paint (Graphics& g)
{
    auto* d = doc();
    if (d == nullptr) return;
    // An edit under us changes the length the view is a fraction OF, so re-anchor
    // before anything is measured against it.
    clampView();
    ensurePeaks (*d);

    auto* w = engineWindow();
    const bool muted = w != nullptr && w->muted.load (std::memory_order_relaxed);
    const bool soloed = w != nullptr && w->soloed.load (std::memory_order_relaxed);
    // Silenced by somebody else's solo: the Mute look with the M box unlit, as its
    // mixer strip shows it.
    const bool silenced = muted || (! soloed && engine.anyWindowSoloed());

    // The face is the theme colour; silenced, a white veil at 80% washes it out (the
    // "Window" frame's own fill in the Mute=On variants).
    const auto b = body();
    g.setColour (silenced ? tint.interpolatedWith (white, 0.8f) : tint);
    g.fillRect (b);

    // A playing Window mostly repaints 1 px columns (refresh()): skip the parts the
    // clip does not reach rather than build their paths and text for nothing.
    if (g.clipRegionIntersects (titleBar()))   paintTitle (g, *d, muted, soloed);
    if (g.clipRegionIntersects (statusArea())) paintStatus (g, *d);
    if (g.clipRegionIntersects (tracksArea())) paintTracks (g, *d, silenced);
    if (g.clipRegionIntersects (zoomArea().withTrimmedBottom (-1))) paintZoomBar (g, *d);

    // Source is joined to Record by a short groove ("Menu_separator", 8 x 2): the
    // source feeds the recording.
    {
        const auto t = transportArea();
        const float x = (float) (t.getCentreX() - 114 + 50), y = (float) t.getY() + 18.0f;
        const juce::Colour rules[] = { gray200, gray400, gray500, white };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (rules[i]);
            g.fillRect (juce::Rectangle<float> (x, y + (float) i * 0.5f, 8.0f, 0.5f));
        }
    }

    paintGrip (g);

    // The border: 2 px always, 3 px under the pointer, 5 px when selected, all
    // OUTSIDE the body. Drawn last so nothing paints over it.
    const int border = selected ? 5 : hovered ? 3 : 2;
    g.setColour (black);
    const auto outer = b.expanded (border);
    g.fillRect (outer.getX(), outer.getY(), outer.getWidth(), border);
    g.fillRect (outer.getX(), b.getBottom(), outer.getWidth(), border);
    g.fillRect (outer.getX(), b.getY(), border, b.getHeight());
    g.fillRect (b.getRight(), b.getY(), border, b.getHeight());
}

void WindowComponent::paintTitle (Graphics& g, const se::SoundDocument& d, bool muted, bool soloed)
{
    const auto bar = titleBar();
    const auto close = closeBox(), mute = muteBox(), solo = soloBox();

    // Stripes only on the active (selected) Window, as on the Mac. They start 24 px
    // in and stop 8 px short of M.
    os8::paintTitleBar (g, bar, nameEditor != nullptr ? juce::String() : juce::String (d.name),
                        selected, close.getRight() + 7 - bar.getX(), bar.getRight() - mute.getX() + 8);

    os8::paintTitleBarBox (g, close.getPosition(), false, os8::TitleBox::close);
    os8::paintTitleBarBox (g, mute.getPosition(), muted, os8::TitleBox::mute);
    os8::paintTitleBarBox (g, solo.getPosition(), soloed, os8::TitleBox::solo);
}

void WindowComponent::paintStatus (Graphics& g, const se::SoundDocument& d)
{
    const auto s = statusArea();
    // Outline on three sides; the tracks below close it.
    g.setColour (black);
    g.fillRect (s.getX(), s.getY(), s.getWidth(), 1);
    g.fillRect (s.getX(), s.getY(), 1, s.getHeight());
    g.fillRect (s.getRight() - 1, s.getY(), 1, s.getHeight());

    // The mixer slot, boxed.
    const juce::Rectangle<int> box (s.getX() + 8, s.getY() + 7, 16, 16);
    g.drawRect (box, 1);
    os8::drawText (g, slot > 0 ? juce::String (slot) : juce::String ("-"), os8::TextStyle::xSmallLabel,
                   { (float) box.getX() + 1.0f, (float) box.getY() + 2.0f, 14.0f, 9.0f },
                   juce::Justification::horizontallyCentred);

    // Rate, bits, tracks, length — 6 px apart, and dropped from the right when the
    // window is too narrow for them beside the selection readout.
    const double seconds = d.rateHz > 0.0 && d.frameSize() > 0
                         ? (double) d.maxTrackSize() / (double) d.frameSize() / d.rateHz : 0.0;
    const juce::String parts[] = {
        juce::String (d.rateHz / 1000.0, 1) + "khz",
        juce::String (d.bps) + "bits",
        juce::String (d.numTracks()) + (d.numTracks() == 1 ? " track" : " tracks"),
        juce::String (seconds, 2) + "s"
    };
    const int limit = startField.getX() - 14 - 8;   // the start icon, and some air
    int x = box.getRight() + 6;
    for (const auto& part : parts)
    {
        const int w = os8::widthOf (part, os8::TextStyle::smallLabel);
        if (x + w > limit) break;
        os8::drawText (g, part, os8::TextStyle::smallLabel, { (float) x, (float) s.getY() + 10.0f, (float) w, 10.0f });
        x += w + 6;
    }

    // The readout's three icons, each centred in its frame (14, 19 and 14 px wide).
    const auto iconAt = [&g, &s] (os8::icon::Id icon, int frameX, int frameW)
    {
        const auto size = os8::icon::sizeOf (icon);
        g.setColour (black);
        g.fillPath (os8::icon::outline (icon),
                    juce::AffineTransform::translation ((float) (frameX + (frameW - size.x) / 2), (float) s.getY() + 10.0f));
    };
    iconAt (os8::icon::Id::selectionStart,  startField.getX() - 18,  14);
    iconAt (os8::icon::Id::selectionLength, lengthField.getX() - 23, 19);
    iconAt (os8::icon::Id::selectionEnd,    endField.getX() - 18,    14);
}

void WindowComponent::paintTracks (Graphics& g, const se::SoundDocument& d, bool muted)
{
    const auto area = tracksArea();
    const auto clip = g.getClipBounds();
    const int n = d.numTracks();
    const size_t maxSize = d.maxTrackSize();

    auto* wsnap = engineWindow();
    const bool playing = wsnap && wsnap->transport->uiState() == se::TransportState::playing;

    const Colour ink = muted ? kWaveMuted : black;
    // The selection inverts what is under it: a DIFFERENCE layer, white (or #666
    // on a muted Window), as the waveform placeholder draws it.
    const Colour sel = muted ? gray500 : white;
    // The playhead is a 1 px DIFFERENCE line too (#9d9d9d when muted), so it shows
    // inside the inverted selection as well as outside it.
    const Colour headInk = muted ? gray400 : white;
    const Colour red = os8::colour::transportRed;

    // The Tracks a take is writing into wear the Recording states (196:4395).
    std::vector<int> recTracks;
    if (isRecording()) recTracks = recorder->targetTracks();

    for (int t = 0; t < n; ++t)
    {
        const auto& track = d.tracks[(size_t) t];

        // Output speakers: lit (white, with the speaker) or off (the theme, with a
        // one-pixel highlight at its bottom-right).
        for (int right = 0; right < 2; ++right)
        {
            const auto boxR = speakerBox (t, right == 1);
            const bool on = right == 1 ? track.outRight : track.outLeft;
            if (on)
            {
                g.setColour (white); g.fillRect (boxR);
                g.setColour (black); g.drawRect (boxR, 1);
                os8::icon::draw (g, right == 1 ? os8::icon::Id::speakerRightOn : os8::icon::Id::speakerLeftOn,
                                 boxR.getPosition().toFloat() + juce::Point<float> (1.0f, 1.0f));
            }
            else
            {
                g.setColour (tint); g.fillRect (boxR);
                g.setColour (juce::Colour (0xfffffdfd));
                g.fillRect (boxR.getX() + 1, boxR.getBottom() - 2, boxR.getWidth() - 2, 1);
                g.fillRect (boxR.getRight() - 2, boxR.getY() + 1, 1, boxR.getHeight() - 2);
                g.setColour (gray600); g.drawRect (boxR, 1);
            }
        }

        // The waveform bed: white, framed in 20% black on three sides (the lane
        // below supplies the fourth).
        const auto wr = waveRect (t);
        g.setColour (white); g.fillRect (wr);
        g.setColour (black.withAlpha (0.2f));
        g.fillRect (wr.getX(), wr.getY(), wr.getWidth(), 1);
        g.fillRect (wr.getX(), wr.getY(), 1, wr.getHeight());
        g.fillRect (wr.getRight() - 1, wr.getY(), 1, wr.getHeight());

        const auto lane = wr.reduced (1, 0).withTrimmedTop (1);
        const float mid = (float) lane.getCentreY();
        const float amp = (float) lane.getHeight() * 0.5f - 3.0f;
        const size_t lenT = track.data.size();
        const int endX = std::clamp (xAtByte (lenT, maxSize), lane.getX(), lane.getRight());

        // Is (x, y) inked by the waveform: a peak column, the line of silence, or a
        // dot of absence. drawWave paints exactly these pixels.
        const auto waveCovers = [&] (int x, int y)
        {
            if (y == (int) mid)
            {
                if (x >= lane.getX() && x < endX) return true;
                if (x > endX && (x - endX - 1) % 3 == 0) return true;
            }
            const int col = x - waveOriginX();
            if (t >= (int) peaks.size() || x >= endX || col < 0 || col >= (int) peaks[(size_t) t].size()) return false;
            const auto r = peaks[(size_t) t][(size_t) col];
            if (juce::exactlyEqual (r.getStart(), 0.0f) && juce::exactlyEqual (r.getEnd(), 0.0f)) return false;
            return (float) y + 1.0f > mid - r.getEnd() * amp && (float) y < mid - r.getStart() * amp + 1.0f;
        };

        // Silence (real samples) is a solid line; absence (past this track's end) a
        // dotted one — two different things, drawn differently.
        const auto drawWave = [&] (Colour colour)
        {
            g.setColour (colour);
            g.drawHorizontalLine ((int) mid, (float) lane.getX(), (float) endX);
            // Only the columns the clip reaches: Core Graphics records every call,
            // clipped or not, and a playing Window repaints 1 px columns (refresh()).
            const int x0 = std::max (lane.getX(), clip.getX());
            const int x1 = std::min (lane.getRight(), clip.getRight());
            if (t < (int) peaks.size())
            {
                const auto& cols = peaks[(size_t) t];
                for (int px = x0; px < x1 && px < endX; ++px)
                {
                    const int x = px - waveOriginX();
                    if (x < 0 || x >= (int) cols.size()) continue;
                    const auto r = cols[(size_t) x];
                    if (juce::exactlyEqual (r.getStart(), 0.0f) && juce::exactlyEqual (r.getEnd(), 0.0f)) continue;
                    g.drawVerticalLine (px, mid - r.getEnd() * amp, mid - r.getStart() * amp + 1.0f);
                }
            }
            for (int x = endX + 1; x < x1; x += 3)
                if (x >= x0) g.fillRect (x, (int) mid, 1, 1);
        };

        const bool inSel = t >= d.firstSelTrack - 1 && t <= d.lastSelTrack - 1;
        const bool hasRange = inSel && d.selEnd > d.selSt;
        const bool rec = std::find (recTracks.begin(), recTracks.end(), t) != recTracks.end();
        const int selX0 = hasRange ? std::max (xAtByte (d.selSt, maxSize), lane.getX()) : 0;
        const int selX1 = hasRange ? std::min (xAtByte (d.selEnd, maxSize), lane.getRight()) : 0;

        // Recording with no selection: the waveform itself turns red (30 % over the
        // black line of silence when muted).
        const bool recFaint = rec && ! hasRange && muted;
        const Colour waveInk = rec && ! hasRange && ! muted ? red : ink;
        if (recFaint)
        {
            // 30 % red over whatever is under it: the bed, or the black line of
            // silence the design leaves showing through.
            g.setColour (black);
            g.drawHorizontalLine ((int) mid, (float) lane.getX(), (float) endX);
            drawWave (red.withAlpha (0.3f));
        }
        else
        {
            drawWave (waveInk);
        }

        // What the selection layer turns a pixel into.
        const auto underSelection = [&] (Colour under, bool inked)
        {
            if (! rec)  return difference (under, sel);
            if (! muted) return inked ? white : red;               // a red block, the wave white in it
            return linearBurn (under, red, 0.3f);                 // red 30 % LINEAR_BURN
        };

        if (hasRange && selX1 > selX0)
        {
            juce::Graphics::ScopedSaveState save (g);
            const Rectangle<int> selRect (selX0, lane.getY(), selX1 - selX0, lane.getHeight());
            g.reduceClipRegion (selRect);
            g.setColour (underSelection (white, false));
            g.fillRect (selRect);
            drawWave (underSelection (ink, true));
        }
        else if (inSel && d.selSt == d.selEnd)
        {
            // The insertion point: a serifed I-beam, so it cannot pass for a playhead.
            const int cx = xAtByte (d.selSt, maxSize);
            const auto r = lane.reduced (0, 2);
            if (cx >= r.getX() + 2 && cx <= r.getRight() - 3)
            {
                g.setColour (black);
                g.drawVerticalLine (cx, (float) r.getY(), (float) r.getBottom());
                g.fillRect (cx - 2, r.getY(), 5, 1);
                g.fillRect (cx - 2, r.getBottom() - 1, 5, 1);
            }
        }

        // Per-track playhead, each track at its OWN loop phase.
        if (playing && lenT > 0)
        {
            // Where THIS track is reading: its own length, phase and direction (the
            // transport publishes it per track, so the playhead can run backward).
            size_t ph = 0;
            const bool present = wsnap->transport->uiTrackPosition (t, ph);
            const int px = xAtByte (ph, maxSize);
            if (present && px >= lane.getX() && px < lane.getRight())
            {
                // DIFFERENCE, pixel by pixel, against what the lane shows there.
                const bool inRange = hasRange && px >= selX0 && px < selX1;
                for (int y = lane.getY(); y < lane.getBottom(); ++y)
                {
                    const bool inked = waveCovers (px, y);
                    Colour under = white;
                    if (inked)
                    {
                        if (recFaint)
                            under = (y == (int) mid ? black : white).interpolatedWith (red, 0.3f);
                        else
                            under = waveInk;
                    }
                    if (inRange) under = underSelection (under, inked);
                    g.setColour (difference (under, headInk));
                    g.fillRect (px, y, 1, 1);
                }
            }
        }

        // The track's drag handle.
        const auto handle = moveHandle (t);
        g.setColour (black);
        g.fillPath (os8::icon::outline (os8::icon::Id::activeTrack),
                    juce::AffineTransform::translation (handle.getPosition().toFloat()));
    }

    // The tracks' outline, left and right; the new-track handle and the trash.
    g.setColour (black);
    g.fillRect (area.getX(), area.getY(), 1, area.getHeight());
    g.fillRect (area.getRight() - 1, area.getY(), 1, area.getHeight());
    g.fillPath (os8::icon::outline (os8::icon::Id::newTrack),
                juce::AffineTransform::translation (newTrackHandle().getPosition().toFloat()));
    os8::icon::draw (g, os8::icon::Id::trashcan, trashRect().getPosition().toFloat());
}

void WindowComponent::paintZoomBar (Graphics& g, const se::SoundDocument& d)
{
    const auto z = zoomArea();
    g.setColour (black);
    g.fillRect (z.getX(), z.getY(), 1, z.getHeight());
    g.fillRect (z.getRight() - 1, z.getY(), 1, z.getHeight());
    g.fillRect (z.getX(), z.getBottom() - 1, z.getWidth(), 1);
    g.setColour (black.withAlpha (0.25f));               // its drop shadow
    g.fillRect (z.getX(), z.getBottom(), z.getWidth(), 1);

    const bool canZoomIn  = viewSpanFrac > minSpanFrac();
    const bool canZoomOut = viewSpanFrac < 1.0;
    const bool canLeft    = viewStartFrac > 0.0;
    const bool canRight   = viewStartFrac + viewSpanFrac < 1.0;
    const bool enabled[5] = { canZoomOut, canZoomIn, canZoomOut, canLeft, canRight };
    const os8::icon::Id icons[5] = { os8::icon::Id::zoomOut, os8::icon::Id::zoomIn, os8::icon::Id::zoomReset,
                                     os8::icon::Id::zoomArrowLeft, os8::icon::Id::zoomArrowRight };
    for (int i = 0; i < 5; ++i)
    {
        const auto r = zoomButton (i);
        os8::paintSmallBevelButton (g, r, pressedZoomButton == i);
        // A button with nothing left to do says so rather than clicking and doing
        // nothing: fully zoomed out, that is three of the five.
        os8::icon::draw (g, icons[i], r.getPosition().toFloat() + juce::Point<float> (2.0f, 2.0f),
                         enabled[i] ? 1.0f : 0.35f);
    }

    // The overview: the whole sound at a glance.
    const auto o = overviewRect();
    juce::Path shape;
    shape.addRectangle (o.toFloat());
    g.setColour (white);
    g.fillPath (shape);
    os8::innerShadow (g, shape,  1.0f,  1.0f, 0, juce::Colour (0xff777777));
    os8::innerShadow (g, shape, -1.0f, -1.0f, 0, white);

    const auto inner = o.reduced (2, 0);
    const size_t maxSize = d.maxTrackSize();
    const auto spanX = [&inner] (double f0, double f1, int y, int h)
    {
        const int x0 = inner.getX() + (int) std::lround (std::clamp (f0, 0.0, 1.0) * inner.getWidth());
        const int x1 = inner.getX() + (int) std::lround (std::clamp (f1, 0.0, 1.0) * inner.getWidth());
        return Rectangle<int> (x0, y, std::max (2, x1 - x0), h);
    };

    // Quarter ticks: full height at the ends, shorter between.
    g.setColour (black);
    for (int i = 0; i <= 4; ++i)
    {
        const int x = inner.getX() + (int) std::lround ((double) i / 4.0 * (inner.getWidth() - 1));
        g.fillRect (x, o.getY() + 2, 1, (i == 0 || i == 4) ? 5 : 3);
    }

    // The block: the region rolling while it plays; stopped, the Set Loop region the
    // Loop button will play, or the selection when no region is set.
    auto* w = engineWindow();
    const bool rolling = w && w->transport->uiState() != se::TransportState::stopped;
    const bool region = d.loopEnd > d.loopSt;
    const size_t b0 = rolling ? w->transport->uiPlaySt()  : region ? d.loopSt  : d.selSt;
    const size_t b1 = rolling ? w->transport->uiPlayEnd() : region ? d.loopEnd : d.selEnd;
    if (maxSize > 0 && b1 > b0)
        g.fillRect (spanX ((double) b0 / (double) maxSize, (double) b1 / (double) maxSize, o.getY() + 2, 4));

    // What the tracks above are showing.
    g.setColour (kViewBar);
    g.fillRect (spanX (viewStartFrac, viewStartFrac + viewSpanFrac, o.getY() + 8, 6));
}

void WindowComponent::paintGrip (Graphics& g)
{
    // "Resize-window": three 45° strokes, 50% black with a white highlight under each.
    const auto r = gripRect().toFloat();
    for (int k = 0; k < 3; ++k)
    {
        const float o = 2.0f * (float) k;
        const juce::Line<float> line (r.getX() + 1.7f + o, r.getY() + 5.7f + o,
                                      r.getX() + 6.7f + o, r.getY() + 0.7f + o);
        g.setColour (white);
        g.drawLine (line.getStartX(), line.getStartY() + 1.0f, line.getEndX(), line.getEndY() + 1.0f, 1.0f);
        g.setColour (black.withAlpha (0.5f));
        g.drawLine (line, 1.0f);
    }
}

void WindowComponent::paintOverChildren (Graphics& g)
{
    if (trackDrag == TrackDrag::none) return;
    auto* d = doc();
    if (d == nullptr) return;

    const int n = d->numTracks();
    const auto area = tracksArea();
    const bool overTrash = trackDrag == TrackDrag::move && trashRect().expanded (6).contains (dragPoint);

    if (overTrash)
    {
        // The trash takes it: the trash is highlighted, nothing else is shown.
        g.setColour (os8::colour::selectedMenuItem.withAlpha (0.35f));
        g.fillRect (trashRect().expanded (3));
        return;
    }

    if (! area.expanded (0, laneH / 2).contains (dragPoint)) return;

    // The ghost target: a 2 px rule across the tracks at the gap it would land in,
    // and the row's outline following the pointer.
    const int gap = insertionAtY (dragPoint.getY(), n);
    const int y = area.getY() + gap * laneH;
    g.setColour (os8::colour::selectedMenuItem);
    g.fillRect (area.getX() + 2, y - 1, area.getWidth() - kNewDelW - 4, 2);

    const auto row = trackRow (0).withY (dragPoint.getY() - laneH / 2);
    const float dashes[] = { 2.0f, 2.0f };
    juce::Path outline;
    outline.addRectangle (row.reduced (1).toFloat());
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (black);
    g.fillPath (dashed);
}

// --- hover, selection, record ---------------------------------------------------

void WindowComponent::updateHover()
{
    const bool now = isMouseOver (true);
    if (now == hovered) return;
    hovered = now;
    repaint();
}

void WindowComponent::mouseEnter (const juce::MouseEvent&) { updateHover(); }
void WindowComponent::mouseExit  (const juce::MouseEvent&) { updateHover(); }

void WindowComponent::invalidateWaveform()
{
    cachedWidth = -1;
    summaryStale = true;

    // A module that ADDS tracks leaves this Window laid out for the count it had a
    // moment ago, and the new lanes would be drawn under the zoom bar. Every sizing
    // path ends at chromeHeight() + n * laneH, which is what preferredHeight()
    // returns — so a mismatch means the TRACK COUNT moved, and only that re-fits.
    if (getHeight() != preferredHeight())
    {
        resizeToFit();
        return;
    }

    repaint();
}

void WindowComponent::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected) return;
    selected = shouldBeSelected;
    repaint();
}

// --- the selection readout --------------------------------------------------------

double WindowComponent::toUnits (size_t bytes) const
{
    auto* d = doc();
    if (d == nullptr) return 0.0;
    const double frames = (double) bytes / (double) std::max (1, d->frameSize());
    switch (AppPrefs::get().timeUnit())
    {
        case 0:  return d->rateHz > 0.0 ? frames / d->rateHz : 0.0;
        case 1:  return d->rateHz > 0.0 ? frames / d->rateHz * 1000.0 : 0.0;
        default: return frames;
    }
}

size_t WindowComponent::fromUnits (double value) const
{
    auto* d = doc();
    if (d == nullptr) return 0;
    double frames = 0.0;
    switch (AppPrefs::get().timeUnit())
    {
        case 0:  frames = value * d->rateHz; break;
        case 1:  frames = value / 1000.0 * d->rateHz; break;
        default: frames = value; break;
    }
    const auto fs = (size_t) std::max (1, d->frameSize());
    const size_t bytes = (size_t) std::max (0.0, std::round (frames)) * fs;
    return std::min (bytes, d->maxTrackSize() - d->maxTrackSize() % fs);
}

juce::String WindowComponent::formatUnits (double value) const
{
    switch (AppPrefs::get().timeUnit())
    {
        case 0:  return juce::String (value, 5);
        case 1:  return juce::String (value, 2);
        default: return juce::String ((juce::int64) std::llround (value));
    }
}

void WindowComponent::configureSelectionFields()
{
    // ↑/↓ steps commensurate with the unit: a hundredth of a second, a millisecond,
    // a sample — ×10 with shift.
    const int unit = AppPrefs::get().timeUnit();
    const double step = unit == 0 ? 0.01 : 1.0;
    const int places = unit == 0 ? 5 : unit == 1 ? 2 : 0;
    for (auto* f : { &startField, &lengthField, &endField })
        f->setNumeric (step, places, 0.0, 1.0e12);
}

void WindowComponent::updateSelectionFields (bool force)
{
    auto* d = doc();
    if (d == nullptr) return;
    const int unit = AppPrefs::get().timeUnit();
    if (! force && d->selSt == lastSelSt && d->selEnd == lastSelEnd && unit == lastUnit) return;
    lastSelSt = d->selSt;
    lastSelEnd = d->selEnd;
    lastUnit = unit;

    // A field being typed in keeps what is being typed.
    const auto show = [this] (Os8TextField& f, size_t bytes)
    {
        if (f.currentState() == os8::FieldState::active) return;
        f.setText (formatUnits (toUnits (bytes)));
    };
    show (startField, d->selSt);
    show (lengthField, d->selEnd - d->selSt);
    show (endField, d->selEnd);
}

void WindowComponent::commitSelectionField (int which)
{
    auto* d = doc();
    if (d == nullptr) return;

    size_t st = d->selSt, en = d->selEnd;
    const size_t len = en - st;
    switch (which)
    {
        case 0:   // start: the selection moves, keeping its length (none: the caret)
            st = fromUnits (startField.getValue());
            en = std::min (st + len, d->maxTrackSize());
            break;
        case 1:   // length: the end moves
            en = std::min (st + fromUnits (lengthField.getValue()), d->maxTrackSize());
            break;
        default:  // end: the length follows; an end before the start swaps them
            en = fromUnits (endField.getValue());
            if (en < st) std::swap (st, en);
            break;
    }

    se::setSelection (*d, st, en);
    commit();
    updateSelectionFields (true);
}

void WindowComponent::appPrefChanged (const juce::String& key)
{
    if (key != AppPrefs::timeUnitKey) return;
    unitDropdown.setSelectedIndex (AppPrefs::get().timeUnit());
    configureSelectionFields();
    updateSelectionFields (true);
}

// --- transport ------------------------------------------------------------------

void WindowComponent::updateTransportButtons()
{
    auto* w = engineWindow();
    auto state = w != nullptr ? w->transport->uiState() : se::TransportState::stopped;

    // A take is the transport's state here, whatever the engine's transport is
    // doing underneath (a take into new tracks of an empty Sound has nothing to
    // play, so its transport is stopped): recording = playing, paused = paused.
    const bool taking = isRecording();
    const bool takePaused = taking && recorder->state() == se::Recorder::State::paused;
    if (taking) state = takePaused ? se::TransportState::paused : se::TransportState::playing;

    playButton.setPlaying (state == se::TransportState::playing);
    // The icon follows the Window's direction, whoever changed it (menu, D, the strip).
    const auto dir = direction();
    playButton.setDirection (dir == se::PlayDirection::backward ? Os8ComboPlay::Direction::backward
                             : dir == se::PlayDirection::pingPong ? Os8ComboPlay::Direction::pingPong
                                                                  : Os8ComboPlay::Direction::forward);
    playButton.setDirectionMenuEnabled (! taking);   // recording forces Forward
    pauseButton.setToggleState (state == se::TransportState::paused, juce::dontSendNotification);
    pauseButton.setEnabled (state != se::TransportState::stopped);
    stopButton.setEnabled (state != se::TransportState::stopped);
    fastForwardButton.setToggleState (advanceEnabled, juce::dontSendNotification);
    fastForwardButton.setEnabled (! taking);
    loopButton.setToggleState (loopEnabled, juce::dontSendNotification);

    // Record: lit while recording, flashing while the take is paused, otherwise
    // available whenever a take could start.
    bool recLit = taking && ! takePaused;
    if (takePaused) recLit = (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
    recordButton.setEnabled (taking || canPerform (Action::record));
    sourceButton.setEnabled (canPerform (Action::selectAudioSource));
    recordButton.setToggleState (recLit, juce::dontSendNotification);
}

bool WindowComponent::isRecording() const
{
    return recorder != nullptr && recorder->active() && recorder->window() == id;
}

// --- interaction ------------------------------------------------------------

juce::Point<int> WindowComponent::parentPos (const juce::MouseEvent& e) const
{
    if (auto* parent = getParentComponent())
        return e.getEventRelativeTo (parent).getPosition();
    return e.getScreenPosition();
}

void WindowComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingTitle = false; selecting = false; resizingWindow = false; overviewDragging = false;
    trackDrag = TrackDrag::none; pressedZoomButton = -1; pressedTitleBox = -1;
    grabKeyboardFocus();
    if (onSelected) onSelected (id);
    auto* d = doc();
    if (d == nullptr) return;
    const auto p = e.getPosition();
    const size_t maxSize = d->maxTrackSize();
    const int n = d->numTracks();

    if (gripRect().expanded (2).contains (p))
    {
        resizingWindow = true;
        dragMouseStart = parentPos (e);
        resizeSizeStart = { getWidth(), getHeight() };
        toFront (true);
        return;
    }

    if (closeBox().expanded (1).contains (p)) { if (onClose) onClose (id); return; }
    if (muteBox().expanded (1).contains (p) || soloBox().expanded (1).contains (p))
    {
        if (auto* w = engineWindow())
        {
            const bool onMute = muteBox().expanded (1).contains (p);
            auto& flag = onMute ? w->muted : w->soloed;
            // ⌘-click on a lit box clears that box on every Window.
            if (e.mods.isCommandDown() && flag.load (std::memory_order_relaxed))
                onMute ? engine.unmuteAll() : engine.clearAllSolos();
            else
                flag.store (! flag.load (std::memory_order_relaxed), std::memory_order_relaxed);
        }
        repaint();
        return;
    }

    // Track drags: the new-track handle, or a track's own handle.
    if (newTrackHandle().expanded (3).contains (p))
    {
        trackDrag = TrackDrag::add;
        dragPoint = p;
        setMouseCursor (trackDragCursor (os8::icon::Id::newTrackDrag));
        return;
    }
    for (int t = 0; t < n; ++t)
        if (moveHandle (t).expanded (3).contains (p))
        {
            trackDrag = TrackDrag::move;
            dragTrack = t;
            dragPoint = p;
            dragDuplicate = e.mods.isAltDown();
            setMouseCursor (trackDragCursor (os8::icon::Id::activeTrackDrag));
            return;
        }

    // Output speakers: each one toggles its own side, so a track can go left, right,
    // both — or nowhere.
    for (int t = 0; t < n; ++t)
        for (int right = 0; right < 2; ++right)
            if (speakerBox (t, right == 1).contains (p))
            {
                auto& tr = d->tracks[(size_t) t];
                (right == 1 ? tr.outRight : tr.outLeft) = ! (right == 1 ? tr.outRight : tr.outLeft);
                commit();
                return;
            }

    // The zoom bar only ever moves the VIEW — nothing here touches the document.
    if (zoomArea().contains (p))
    {
        for (int i = 0; i < 5; ++i)
            if (zoomButton (i).contains (p))
            {
                pressedZoomButton = i;
                switch (i)
                {
                    case 0: zoomBy (2.0); break;
                    case 1: zoomBy (0.5); break;
                    case 2: viewStartFrac = 0.0; viewSpanFrac = 1.0; cachedWidth = -1; break;
                    case 3: panBy (-0.5); break;
                    default: panBy (+0.5); break;
                }
                repaint();
                return;
            }
        if (overviewRect().contains (p))
        {
            // Click anywhere in the overview to jump there; hold and it scrubs.
            overviewDragging = true;
            setViewCentreFromOverviewX (p.getX());
            repaint();
        }
        return;
    }

    for (int t = 0; t < n; ++t)
        if (waveRect (t).contains (p))
        {
            // Double-click: the whole track. Triple-click: every track, all of it.
            const int clicks = e.getNumberOfClicks();
            if (clicks >= 3) { selectAllAudio(); return; }
            if (clicks == 2)
            {
                d->firstSelTrack = d->lastSelTrack = t + 1;
                se::setSelection (*d, 0, d->tracks[(size_t) t].data.size());
                commit(); return;
            }
            selecting = true; selAnchorTrack = t; selAnchorByte = byteAtX (p.getX(), maxSize);
            d->firstSelTrack = d->lastSelTrack = t + 1;
            se::setSelection (*d, selAnchorByte, selAnchorByte);
            repaint(); return;
        }

    if (titleBar().contains (p))
    {
        draggingTitle = true;
        titleLifted = false;
        dragMouseStart = parentPos (e);
        dragBoundsStart = getBounds().getPosition();
        toFront (true);
    }
}

void WindowComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (trackDrag != TrackDrag::none)
    {
        dragPoint = e.getPosition();
        repaint();
        return;
    }
    if (resizingWindow)
    {
        const auto delta = parentPos (e) - dragMouseStart;
        applySize (resizeSizeStart.getX() + delta.getX(), resizeSizeStart.getY() + delta.getY());
        cachedWidth = -1;
        storeGeometry();
        if (onWidthChanged) onWidthChanged (desiredW);
        if (onGeometryChanged) onGeometryChanged();
        repaint();
        return;
    }
    if (overviewDragging)
    {
        setViewCentreFromOverviewX (e.getPosition().getX());
        repaint();
        return;
    }
    if (draggingTitle)
    {
        // A click with a shaky hand is still a click: nothing moves until the
        // pointer has really travelled.
        if (! titleLifted)
        {
            if (e.getDistanceFromDragStart() < reorder::kLiftThreshold) return;
            titleLifted = true;
            setMouseCursor (reorder::grabbingCursor());
            if (onDragStarted) onDragStarted();
            if (! draggingTitle) return;   // refused
        }
        auto np = dragBoundsStart + (parentPos (e) - dragMouseStart);
        np = { std::max (0, np.getX()), std::max (0, np.getY()) };   // stay on the board
        setTopLeftPosition (np);
        storeGeometry();
        if (onDragged) onDragged (np);
        return;
    }
    if (selecting)
    {
        // Not until the pointer has really travelled: a click leaves an insertion
        // point, and hand-shake must not turn it into a one-frame selection.
        if (! e.mouseWasDraggedSinceMouseDown()) return;

        auto* d = doc(); if (d == nullptr) return;
        const size_t maxSize = d->maxTrackSize();
        const int n = d->numTracks();
        const int curTrack = trackAtY (e.getPosition().getY(), n);
        const size_t curByte = byteAtX (e.getPosition().getX(), maxSize);
        d->firstSelTrack = std::min (selAnchorTrack, curTrack) + 1;
        d->lastSelTrack  = std::max (selAnchorTrack, curTrack) + 1;
        se::setSelection (*d, std::min (selAnchorByte, curByte), std::max (selAnchorByte, curByte));
        repaint();
    }
}

void WindowComponent::mouseUp (const juce::MouseEvent& e)
{
    if (trackDrag != TrackDrag::none)
    {
        const auto kind = trackDrag;
        trackDrag = TrackDrag::none;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);   // still over the handle it left
        auto* d = doc();
        if (d == nullptr) { repaint(); return; }

        const int n = d->numTracks();
        const auto p = e.getPosition();
        const bool inTracks = tracksArea().expanded (0, laneH / 2).contains (p);

        if (kind == TrackDrag::move && dragTrack >= 0 && dragTrack < n)
        {
            if (trashRect().expanded (6).contains (p))
            {
                // Onto the trash: deleted. The last track of a sound refuses.
                if (d->delTrack (dragTrack + 1)) { commit(); resizeToFit(); }
            }
            else if (inTracks)
            {
                const int gap = insertionAtY (p.getY(), n);
                if (dragDuplicate || e.mods.isAltDown())
                {
                    // ⌥: a copy lands in the gap; the original stays.
                    const auto copy = d->tracks[(size_t) dragTrack];
                    auto& made = d->addTrack (gap + 1, 0);
                    made = copy;
                    commit(); resizeToFit();
                }
                else
                {
                    // Moved into the gap: one swap at a time, so the chunks follow.
                    const int target = gap > dragTrack ? gap - 1 : gap;
                    for (int t = dragTrack; t < target; ++t) d->swapTracks (t + 1, t + 2);
                    for (int t = dragTrack; t > target; --t) d->swapTracks (t + 1, t);
                    commit();
                }
            }
        }
        else if (kind == TrackDrag::add && inTracks)
        {
            d->addTrack (insertionAtY (p.getY(), n) + 1, 0);
            commit(); resizeToFit();
        }

        repaint();
        return;
    }

    if (selecting) commit();       // publish the selection so Play can loop it
    const bool dropped = draggingTitle && titleLifted;
    if (titleLifted) mouseMove (e);   // the hover cursor again
    if (pressedZoomButton >= 0) { pressedZoomButton = -1; repaint(); }
    draggingTitle = titleLifted = false; selecting = false; resizingWindow = false; overviewDragging = false;
    if (dropped && onDropped) onDropped();
}

void WindowComponent::abandonTitleDrag()
{
    draggingTitle = titleLifted = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

juce::MouseCursor WindowComponent::trackDragCursor (os8::icon::Id icon)
{
    // The design's drag icons (208:5690: Active-track-drag, new-track-drag) as the
    // pointer while a Track is carried, drawn at 2x for Retina screens.
    const auto size = os8::icon::sizeOf (icon);
    juce::Image image (juce::Image::ARGB, size.x * 2, size.y * 2, true);
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        os8::icon::draw (g, icon, {});
    }
    return juce::MouseCursor (juce::ScaledImage (image, 2.0), { size.x / 2, size.y / 2 });
}

void WindowComponent::mouseMove (const juce::MouseEvent& e)
{
    updateHover();
    const auto p = e.getPosition();
    bool overHandle = newTrackHandle().expanded (3).contains (p);
    for (int t = 0; t < numTracks() && ! overHandle; ++t)
        overHandle = moveHandle (t).expanded (3).contains (p);

    setMouseCursor (gripRect().expanded (2).contains (p) ? juce::MouseCursor::BottomRightCornerResizeCursor
                    : overHandle                          ? juce::MouseCursor::DraggingHandCursor
                                                          : juce::MouseCursor::NormalCursor);
}

void WindowComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (titleBar().contains (p) && ! closeBox().expanded (1).contains (p)
        && ! muteBox().expanded (1).contains (p) && ! soloBox().expanded (1).contains (p))
        beginNameEdit();
}

// --- transport / rename -----------------------------------------------------

void WindowComponent::selectAllAudio()
{
    auto* d = doc(); if (d == nullptr) return;
    d->firstSelTrack = 1;
    d->lastSelTrack  = d->numTracks();
    se::setSelection (*d, 0, d->maxTrackSize());
    commit();   // publish, so Play can loop it — same as a drag's mouseUp
}

void WindowComponent::togglePlayPause()
{
    if (isRecording())
    {
        // Pause holds the take (Record flashes); Play / Pause again carries on.
        if (recorder->state() == se::Recorder::State::recording)   recorder->pause();
        else if (recorder->state() == se::Recorder::State::paused) recorder->resume();
        updateTransportButtons();
        repaint();
        return;
    }
    auto* w = engineWindow();
    const auto state = w != nullptr ? w->transport->uiState() : se::TransportState::stopped;

    // One toggle. Transport::pause() already flips playing <-> paused, so only a
    // STOPPED transport needs play() — calling it while paused would restart from
    // the top.
    if (state == se::TransportState::stopped)
    {
        // Loop plays the Set Loop region when there is one, else the selection
        // (Transport falls back by itself when the region is empty).
        se::PlayOptions o; o.loop = loopEnabled; o.useLoopRegion = true;
        engine.play (id, o);
        advanceEnabled = false;         // play() resets advanceScale
    }
    else engine.pause (id);

    updateTransportButtons();
    repaint();
}

void WindowComponent::beginNameEdit()
{
    auto* d = doc();
    if (d == nullptr || nameEditor != nullptr) return;

    draggingTitle = false;   // the double-click's own mouseDown armed a title drag

    nameEditor = std::make_unique<juce::TextEditor>();
    auto& ed = *nameEditor;
    ed.setBounds (nameBounds());
    ed.setFont (os8::font (os8::TextStyle::headline));
    ed.setJustification (juce::Justification::centred);
    ed.setMultiLine (false);
    ed.setReturnKeyStartsNewLine (false);
    ed.setBorder (juce::BorderSize<int> (1));
    // A white well with a black frame, selected text in the field highlight.
    ed.setColour (juce::TextEditor::backgroundColourId, white);
    ed.setColour (juce::TextEditor::textColourId, black);
    ed.setColour (juce::TextEditor::outlineColourId, black);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, black);
    ed.setColour (juce::TextEditor::highlightColourId, selectedTextField);
    ed.setColour (juce::TextEditor::highlightedTextColourId, black);
    ed.setText (juce::String (d->name), false);

    ed.onReturnKey = [this] { endNameEdit (true);  grabKeyboardFocus(); };
    ed.onEscapeKey = [this] { endNameEdit (false); grabKeyboardFocus(); };
    ed.onFocusLost = [this] { endNameEdit (true); };   // clicking away commits

    addAndMakeVisible (ed);
    ed.grabKeyboardFocus();
    ed.selectAll();
    repaint();
}

void WindowComponent::endNameEdit (bool keepChanges)
{
    if (nameEditor == nullptr) return;

    if (keepChanges)
    {
        const auto text = nameEditor->getText().trim();
        // A nameless window is unusable in the mixer, so an empty box keeps the
        // old name rather than committing a blank.
        if (text.isNotEmpty())
            if (auto* d = doc())
                d->name = text.toStdString();
        // The name is UI-only — the audio thread never reads it — so this does NOT
        // go through commitEdit(), which would clone every sample to publish a
        // string. The mixer reads uiDoc->name as it paints, so it follows anyway.
    }

    // Detach now, destroy later: this can arrive from the editor's own focus
    // callback, and a component must not be deleted inside one.
    auto* dying = nameEditor.release();
    removeChildComponent (dying);
    juce::MessageManager::callAsync ([dying] { delete dying; });

    repaint();
}

bool WindowComponent::keyPressed (const juce::KeyPress&)
{
    // Every key this Window used to read itself — Space, Delete and its variants,
    // ⌘A/⌘C/⌘X/⌘V — is now an application command (AppCommands), bound once and
    // routed to the selected Window through perform(). Declining here lets the key
    // bubble up to those bindings.
    return false;
}

se::PlayDirection WindowComponent::direction() const
{
    return engine.direction (id);
}

void WindowComponent::setDirection (se::PlayDirection d)
{
    engine.setDirection (id, d);
    updateTransportButtons();
    repaint();
}

bool WindowComponent::isPlaying() const
{
    if (isRecording()) return recorder->state() == se::Recorder::State::recording;
    auto* w = engine.windowById (id);
    return w != nullptr && w->transport->uiState() == se::TransportState::playing;
}

bool WindowComponent::canPerform (Action a) const
{
    auto* d = doc();
    if (d == nullptr) return false;

    switch (a)
    {
        case Action::paste:
            return clipboard != nullptr && *clipboard != nullptr;
        case Action::cut: case Action::copy: case Action::duplicate: case Action::clear:
        case Action::crop: case Action::insertSilence:
            return d->selEnd > d->selSt;   // an insertion point has nothing to act on
        case Action::extendToBeginning: return d->maxSelectedTrackSize() > 0 && d->selSt > 0;
        case Action::extendToEnd:       return d->maxSelectedTrackSize() > 0 && d->selEnd < d->maxSelectedTrackSize();
        case Action::extendAbove:       return d->firstSelTrack > 1;
        case Action::extendBelow:       return d->lastSelTrack < d->numTracks();
        case Action::setLoop:           return d->maxTrackSize() > 0;   // a caret clears the region
        case Action::selectLoop:        return d->maxTrackSize() > 0 && d->loopEnd > d->loopSt;
        case Action::selectSilence:     return d->maxTrackSize() > 0 && d->selSt == d->selEnd;   // needs a caret
        case Action::shiftSound: case Action::selectExactly: case Action::selectPeak:
            return d->maxTrackSize() > 0;   // "can only be applied to an existing sound"
        case Action::selectAudioSource:
            return onSourceRequested != nullptr && ! isRecording();   // the source is fixed while a take runs
        case Action::record:
            // The source is live, nobody else is recording, and the selection covers
            // at most two tracks. Recording is a take already running here: Pause
            // and Stop hold it, and Record carries on a paused one.
            if (recorder == nullptr) return false;
            if (isRecording()) return recorder->state() == se::Recorder::State::paused;
            return recordReady && recordReady (id) && ! recorder->active() && se::Recorder::canRecord (*d);
        case Action::playForward: case Action::playBackward: case Action::playPingPong: case Action::cycleDirection:
        case Action::toggleAdvance:
            return ! isRecording();   // recording forces Forward, plays at normal speed
        case Action::playPause: case Action::stop: case Action::toggleLoop:
        case Action::selectAll: case Action::rename:
        case Action::leaveSilence:
            return true;
    }
    return false;
}

bool WindowComponent::perform (Action a)
{
    if (! canPerform (a)) return false;
    auto* d = doc();

    switch (a)
    {
        case Action::playPause: togglePlayPause(); return true;
        case Action::stop:
            if (isRecording()) recorder->stop();
            else               engine.stop (id);
            advanceEnabled = false; updateTransportButtons(); repaint(); return true;
        case Action::selectAudioSource:
            onSourceRequested (id);
            return true;
        case Action::record:
        {
            if (isRecording()) { recorder->resume(); updateTransportButtons(); return true; }
            se::RecordSetup s;
            if (! recordSetupFor || ! recordSetupFor (id, s)) return false;
            s.loop = loopEnabled;
            const bool ok = recorder->start (id, s);
            advanceEnabled = false;
            updateTransportButtons(); invalidateWaveform();
            return ok;
        }
        case Action::playForward:  setDirection (se::PlayDirection::forward);  return true;
        case Action::playBackward: setDirection (se::PlayDirection::backward); return true;
        case Action::playPingPong: setDirection (se::PlayDirection::pingPong); return true;
        case Action::cycleDirection:   // Forward -> Backward -> Ping-pong -> Forward
            setDirection (static_cast<se::PlayDirection> ((static_cast<int> (direction()) + 1) % 3));
            return true;
        case Action::toggleLoop: loopEnabled = ! loopEnabled; updateTransportButtons(); repaint(); return true;
        case Action::toggleAdvance:
            // advance() starts playback if the transport is stopped, so engaging it
            // also rolls — the same as the button.
            if (advanceEnabled) engine.normalSpeed (id);
            else                engine.advance (id);
            advanceEnabled = ! advanceEnabled;
            updateTransportButtons();
            repaint();
            return true;

        case Action::selectAll: selectAllAudio(); return true;
        case Action::copy:      *clipboard = se::copy (*d); return true;
        case Action::cut:       *clipboard = se::cut (*d); commit(); resizeToFit(); return true;
        case Action::paste:     se::paste (*d, **clipboard); commit(); resizeToFit(); return true;
        case Action::duplicate: se::duplicate (*d); commit(); resizeToFit(); return true;        // copy after the selection, clipboard untouched
        case Action::clear:     se::clear (*d); commit(); resizeToFit(); return true;            // splice out, shortens
        case Action::crop:      se::crop (*d); commit(); resizeToFit(); return true;             // keep only the selection
        case Action::insertSilence: se::silenceSelection (*d); commit(); return true;          // zeros in place, same length
        case Action::rename:    beginNameEdit(); return true;

        case Action::extendToBeginning: se::extendToBeginning (*d); commit(); return true;   // selection only: publish so Play follows
        case Action::extendToEnd:       se::extendToEnd (*d);       commit(); return true;
        case Action::extendAbove:       se::extendAbove (*d);       commit(); return true;
        case Action::extendBelow:       se::extendBelow (*d);       commit(); return true;

        case Action::selectSilence:
            if (se::selectSilence (*d)) commit();
            return true;
        case Action::setLoop:    se::setLoop (*d);    commit(); return true;   // published: the next Play loops it
        case Action::selectLoop: se::selectLoop (*d); commit(); return true;
        case Action::selectExactly:
            openPopup (editpopups::selectExactly (*d, [safe = SafePointer<WindowComponent> (this)] (size_t bytes, int anchor)
            {
                if (safe == nullptr || safe->doc() == nullptr) return;
                se::selectExactly (*safe->doc(), bytes, static_cast<se::ExactlyAnchor> (anchor));
                safe->commit();
            }));
            return true;
        case Action::selectPeak:
            openPopup (editpopups::selectPeak (*d, [safe = SafePointer<WindowComponent> (this)] (size_t margin, bool wholeSound)
            {
                if (safe == nullptr || safe->doc() == nullptr) return;
                se::selectPeak (*safe->doc(), margin, wholeSound);
                safe->commit();
            }));
            return true;

        case Action::leaveSilence:
            openPopup (editpopups::leaveSilence (*d, [safe = SafePointer<WindowComponent> (this)] (size_t bytes, bool wholeSound)
            {
                if (safe == nullptr || safe->doc() == nullptr) return;
                se::leaveSilence (*safe->doc(), bytes, wholeSound);
                safe->commit();
                safe->resizeToFit();
            }));
            return true;
        case Action::shiftSound:
            openPopup (editpopups::shiftSound (*d, [safe = SafePointer<WindowComponent> (this)] (size_t bytes, bool right)
            {
                if (safe == nullptr || safe->doc() == nullptr) return;
                se::shiftSound (*safe->doc(), bytes, right);
                safe->commit();
                safe->resizeToFit();
            }));
            return true;
    }
    return false;
}

void WindowComponent::openPopup (editpopups::Built built)
{
    auto* popup = built.popup.release();
    popup->showOver (*this, { (getWidth() - popup->getWidth()) / 2, (getHeight() - popup->getHeight()) / 2 });
    built.field->grabFocus();
}
