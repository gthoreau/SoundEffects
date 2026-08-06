#include "WindowComponent.h"

#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>

using juce::Colour;
using juce::Graphics;
using juce::Rectangle;

namespace {
const Colour kFace  { 0xffbfbfbf };
const Colour kInk   { 0xff000000 };
const Colour kWhite { 0xffffffff };
const Colour kWave  { 0xff202020 };
const Colour kSelFill { 0x553060c0 };
const Colour kRecord { 0xffcc2222 };   // the record dot, red on any tint
// Audio input muted. This grey IS the app's statement that input is off — it
// replaces the banner JUCE used to run across the top of the whole window, saying
// the same thing in the one place where it matters.
const Colour kRecordOff { 0xff8a8a8a };
const Colour kNavView { 0xff2020cc };  // navigator: the slice of sound on screen
}

WindowComponent::WindowComponent (se::Engine& e, uint64_t wid)
    : engine (e), id (wid)
{
    setWantsKeyboardFocus (true);
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

    // The editor's timer is only here to animate the playhead, so repaint only
    // when something that MOVES has moved. Everything else (edits, selection,
    // toggles, resize) already repaints itself at the point of change. Without
    // this, every Window redrew 30 times a second forever, and that load landed
    // on top of panning and zooming — which repaint the whole board anyway.
    auto* w = engine.windowById (id);
    const bool playing = w != nullptr && w->transport->uiState() == se::TransportState::playing;
    const size_t head = w != nullptr ? w->transport->uiPlayhead() : 0;

    if (playing == lastPlaying && head == lastPlayhead) return;
    lastPlaying = playing;
    lastPlayhead = head;
    repaint();
}

void WindowComponent::resizeToFit()
{
    // Keep the rendered width (the grid owns it); only the height follows the tracks.
    setSize (getWidth() > 0 ? getWidth() : desiredW, preferredHeight());
    cachedWidth = -1;
    storeGeometry();
    if (onGeometryChanged) onGeometryChanged();
    repaint();
}

// --- geometry ---------------------------------------------------------------

Rectangle<int> WindowComponent::contentRect() const
{
    return getLocalBounds().withTrimmedTop (kTitleH).reduced (kMargin);
}

Rectangle<int> WindowComponent::lanesRect() const
{
    auto c = contentRect();
    c.removeFromTop (kInfoH + kMargin);
    c.removeFromBottom (kTransportH + kMargin + kNavH + kMargin);
    return c;
}

Rectangle<int> WindowComponent::lanesInner() const { return lanesRect().reduced (kPad); }

int WindowComponent::waveOriginX() const noexcept { return lanesInner().getX() + kLrW; }

int WindowComponent::waveWidth() const noexcept
{
    return std::max (1, lanesInner().getWidth() - kLrW - kCtrlW);
}

Rectangle<int> WindowComponent::infoRect() const { return contentRect().withHeight (kInfoH); }

Rectangle<int> WindowComponent::nameBounds() const
{
    return Rectangle<int> (0, 0, getWidth(), kTitleH).withTrimmedLeft (26).withTrimmedRight (26);
}

Rectangle<int> WindowComponent::addTrackButton() const
{
    auto r = infoRect().reduced (kPad);
    return { r.getRight() - 15, r.getCentreY() - 7, 15, 15 };
}

Rectangle<int> WindowComponent::laneRect (int t) const
{
    auto lanes = lanesInner();
    return { lanes.getX(), lanes.getY() + t * laneH, lanes.getWidth(), laneH };
}

Rectangle<int> WindowComponent::resizeGripBounds() const
{
    return { getWidth() - kGripW - 1, getHeight() - kGripW - 1, kGripW, kGripW };
}

// The output-channel selector and the up/down/trash controls are chrome: they keep
// a fixed size no matter how tall the lane is. Only the waveform between them grows.
//
// Both clusters are CENTRED on the lane, which puts them on the waveform's zero
// line — the horizontal rule running through the middle of the trace. Pinned to the
// top of the lane, as they were, they drifted further from it the taller the lane
// got, and a tall window ended up with its controls huddled at the top of a lot of
// empty space.

Rectangle<int> WindowComponent::outRect (int t, int which) const
{
    auto lane = laneRect (t);
    auto col = lane.removeFromLeft (kLrW).reduced (2, 0);
    const int h = std::clamp ((lane.getHeight() - 4) / 3, 4, 10);
    const int top = lane.getCentreY() - (3 * h) / 2;   // removeFromLeft left the height alone
    return { col.getX(), top + which * h, col.getWidth(), h };
}

int WindowComponent::outSelection (const se::Track& track) noexcept
{
    if (track.outLeft && track.outRight) return 1;   // L+R
    if (track.outLeft)                   return 0;   // L
    if (track.outRight)                  return 2;   // R
    return -1;                                       // routed nowhere: no cell lit
}

Rectangle<int> WindowComponent::ctrlRect (int t, int which) const
{
    auto lane = laneRect (t);
    auto col = lane.removeFromRight (kCtrlW);
    const int h = std::min (12, col.getHeight() / 3);
    const int top = col.getCentreY() - (3 * h) / 2;
    return { col.getX(), top + which * h, col.getWidth(), h };
}

Rectangle<int> WindowComponent::waveRect (int t) const
{
    auto lane = laneRect (t);
    lane.removeFromLeft (kLrW);
    lane.removeFromRight (kCtrlW);
    return lane;
}

Rectangle<int> WindowComponent::transportBounds() const
{
    return contentRect().removeFromBottom (kTransportH);
}

// --- navigator geometry -----------------------------------------------------

Rectangle<int> WindowComponent::navRect() const
{
    auto c = contentRect();
    c.removeFromBottom (kTransportH + kMargin);
    return c.removeFromBottom (kNavH);
}

Rectangle<int> WindowComponent::navWellRect() const
{
    auto bar = navRect();
    const int left  = 3 * (kNavBtnW + kNavGap) + kNavArrowW + kNavGap;
    const int right = kNavArrowW + kNavGap;
    // The well is the elastic part: the buttons keep their size at every window
    // width and the well takes what is left, down to a floor that keeps it a well
    // rather than a sliver.
    return { bar.getX() + left, bar.getY(),
             std::max (8, bar.getWidth() - left - right), bar.getHeight() };
}

Rectangle<int> WindowComponent::navWellInner() const { return navWellRect().reduced (2, 2); }

Rectangle<int> WindowComponent::navButton (int i) const
{
    auto bar = navRect();
    if (i < 3) return { bar.getX() + i * (kNavBtnW + kNavGap), bar.getY(), kNavBtnW, bar.getHeight() };
    if (i == 3) return { navWellRect().getX() - kNavGap - kNavArrowW, bar.getY(), kNavArrowW, bar.getHeight() };
    return { navWellRect().getRight() + kNavGap, bar.getY(), kNavArrowW, bar.getHeight() };
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

void WindowComponent::setViewCentreFromWellX (int x)
{
    auto well = navWellInner();
    const double f = std::clamp ((double) (x - well.getX()) / (double) std::max (1, well.getWidth()),
                                0.0, 1.0);
    viewStartFrac = f - viewSpanFrac * 0.5;
    clampView();
    cachedWidth = -1;
}

Rectangle<int> WindowComponent::transportButton (int i) const
{
    auto bar = transportBounds().reduced (kBtnMargin, kBtnMargin);
    const int total = kNumTransportButtons * kBtnSize + (kNumTransportButtons - 1) * kBtnGap;
    const int x0 = bar.getX() + std::max (0, (bar.getWidth() - total) / 2);
    const int y0 = bar.getY() + (bar.getHeight() - kBtnSize) / 2;
    return { x0 + i * (kBtnSize + kBtnGap), y0, kBtnSize, kBtnSize };
}

Rectangle<int> WindowComponent::closeBoxBounds() const { return { 5, 4, 11, 11 }; }

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
    return std::clamp ((y - lanesInner().getY()) / std::max (1, laneH), 0, std::max (0, n - 1));
}

// --- waveform peaks (shared byte axis over the longest track) ---------------

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

    cachedWidth = w; cachedMax = maxSize;
    cachedStartFrac = viewStartFrac; cachedSpanFrac = viewSpanFrac;
    cachedSizes.assign ((size_t) n, 0);
    peaks.assign ((size_t) n, {});

    const int bps = d.bps;
    const int frameSize = se::frameSizeFor (bps);
    const double divisor = std::pow (2.0, std::min (frameSize, 4) * 8 - 1);

    for (int t = 0; t < n; ++t)
    {
        const auto& track = d.tracks[(size_t) t];
        cachedSizes[(size_t) t] = track.data.size();
        auto& cols = peaks[(size_t) t];
        cols.assign ((size_t) w, juce::Range<float> (0.0f, 0.0f));
        if (maxSize == 0 || track.data.empty()) continue;

        for (int x = 0; x < w; ++x)
        {
            // The column spans a slice of the VIEW, not of the whole sound.
            const double f0 = viewStartFrac + (double) x       / w * viewSpanFrac;
            const double f1 = viewStartFrac + (double) (x + 1) / w * viewSpanFrac;
            size_t b0 = (size_t) (std::clamp (f0, 0.0, 1.0) * (double) maxSize);
            size_t b1 = (size_t) (std::clamp (f1, 0.0, 1.0) * (double) maxSize);
            b0 -= b0 % (size_t) frameSize;
            // Zoomed right in a column can be narrower than one frame, and a column
            // that reads no samples draws as a gap. Always take at least one.
            if (b1 <= b0) b1 = b0 + (size_t) frameSize;
            float lo = 0.0f, hi = 0.0f; bool any = false;
            for (size_t b = b0; b < b1 && b + (size_t) frameSize <= track.data.size(); b += (size_t) frameSize)
            {
                const float s = (float) (se::getSample (track.data.data(), b, bps) / divisor);
                lo = any ? std::min (lo, s) : s;
                hi = any ? std::max (hi, s) : s;
                any = true;
            }
            if (any) cols[(size_t) x] = { lo, hi };
        }
    }
}

// --- transport symbols ------------------------------------------------------

void WindowComponent::drawTransportSymbol (Graphics& g, int which, Rectangle<int> button,
                                           bool playing) const
{
    // Every symbol is built inside the SAME centred square, so they all carry the
    // same visual weight. The play triangle fills it exactly — it is the benchmark
    // the others are matched to.
    const float s = (float) button.getWidth() * 0.44f;
    const auto box = juce::Rectangle<float> (s, s).withCentre (button.toFloat().getCentre());

    switch (which)
    {
        case 0:     // record: a dot inscribed in the box
            g.fillEllipse (box);
            break;

        case 1:     // stop: the box itself
            g.fillRect (box);
            break;

        case 2:
            if (playing)    // pause: two bars spanning the box
            {
                const float bw = s * 0.34f;
                g.fillRect (box.getX(), box.getY(), bw, s);
                g.fillRect (box.getRight() - bw, box.getY(), bw, s);
            }
            else            // play: the benchmark triangle
            {
                juce::Path p;
                p.addTriangle (box.getX(), box.getY(),
                               box.getX(), box.getBottom(),
                               box.getRight(), box.getCentreY());
                g.fillPath (p);
            }
            break;

        case 3:     // 2x: two triangles sharing the same box
        {
            const float tw = s * 0.46f;
            juce::Path p;
            p.addTriangle (box.getX(), box.getY(),
                           box.getX(), box.getBottom(),
                           box.getX() + tw, box.getCentreY());
            p.addTriangle (box.getRight() - tw, box.getY(),
                           box.getRight() - tw, box.getBottom(),
                           box.getRight(), box.getCentreY());
            g.fillPath (p);
            break;
        }

        case 4:     // loop: a circular arrow inscribed in the box
        {
            const float r = s * 0.5f;
            const float cx = box.getCentreX(), cy = box.getCentreY();
            const float end = juce::MathConstants<float>::twoPi * 0.92f;

            juce::Path arc;
            arc.addCentredArc (cx, cy, r, r, 0.0f,
                               juce::MathConstants<float>::twoPi * 0.10f, end, true);
            g.strokePath (arc, juce::PathStrokeType (std::max (1.5f, s * 0.16f)));

            // Arrowhead on the arc's end, pointing along the (clockwise) tangent.
            // JUCE measures these angles clockwise from 12 o'clock.
            const juce::Point<float> tip { cx + r * std::sin (end), cy - r * std::cos (end) };
            const juce::Point<float> dir { std::cos (end), std::sin (end) };
            const juce::Point<float> nrm { -dir.y, dir.x };
            const float h = s * 0.34f;

            juce::Path head;
            head.addTriangle (tip + dir * h,
                              tip - nrm * (h * 0.6f),
                              tip + nrm * (h * 0.6f));
            g.fillPath (head);
            break;
        }

        default: break;
    }
}

// --- navigator icons --------------------------------------------------------

// The zoom pair is the original's mountain range, drawn twice: ONE big peak (zoomed
// in — the sound magnified) against THREE small ones (zoomed out — more of it, each
// feature smaller). Side by side the direction is unmistakable, which a magnifier
// glyph at 10 pixels never is. Peaks, not the stroked waveform this used to draw:
// a baseline with two ticks on it reads as a letter H at this size, not a sound.
void WindowComponent::drawNavIcon (Graphics& g, int which, Rectangle<int> button) const
{
    const auto box = button.reduced (5, 5).toFloat();

    switch (which)
    {
        case 0:     // zoom in
        case 1:     // zoom out
        {
            const int n = which == 0 ? 1 : 3;
            const float w = box.getWidth() / (float) n;
            juce::Path p;
            for (int i = 0; i < n; ++i)
            {
                const float x = box.getX() + (float) i * w;
                p.addTriangle (x, box.getBottom(), x + w, box.getBottom(),
                               x + w * 0.5f, box.getY());
            }
            g.fillPath (p);
            break;
        }

        case 2:     // whole sound: three stacked rules, the "flat" view
            for (int i = 0; i < 3; ++i)
                g.fillRect (juce::Rectangle<float> (box.getX(),
                                                    box.getY() + box.getHeight() * 0.5f * (float) i,
                                                    box.getWidth(), 1.5f));
            break;

        case 3:     // pan left
        case 4:     // pan right
        {
            const float w = box.getWidth() * 0.8f, h = box.getHeight() * 0.8f;
            const auto a = juce::Rectangle<float> (w, h).withCentre (box.getCentre());
            juce::Path p;
            if (which == 3) p.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY());
            else            p.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
            g.fillPath (p);
            break;
        }

        default: break;
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
    const size_t maxSize = d->maxTrackSize();

    // The whole window is painted in its tint; the waveform bed is the same hue
    // washed out towards white so the trace stays readable.
    const Colour face = tint;
    const Colour bed  = tint.interpolatedWith (kWhite, 0.72f);

    auto full = getLocalBounds();
    g.setColour (face); g.fillRect (full);

    // Title bar.
    auto title = full.removeFromTop (kTitleH);
    g.setColour (kInk); g.drawHorizontalLine (title.getBottom() - 1, (float) title.getX(), (float) title.getRight());
    for (int y = title.getY() + 3; y < title.getBottom() - 3; y += 2)
        g.drawHorizontalLine (y, (float) (title.getX() + 22), (float) (title.getRight() - 22));
    g.setColour (kInk); g.drawRect (closeBoxBounds(), 1);
    if (nameEditor == nullptr)   // while renaming, the editor IS the name
    { auto na = nameBounds();
      g.setColour (face); g.fillRect (na.reduced (24, 2));
      g.setColour (kInk); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
      g.drawText (juce::String (d->name), na, juce::Justification::centred, false); }

    // Info strip + add-track button.
    auto info = infoRect();
    g.setColour (kInk); g.drawRect (info, 1);
    g.setFont (juce::FontOptions (10.0f));
    juce::String l; l << juce::String (d->rateHz / 1000.0, 1) << " kHz; " << d->bps << " bits    "
                     << d->numTracks() << (d->numTracks() == 1 ? " track   " : " tracks   ") << (int) maxSize << " bytes";
    g.drawText (l, info.reduced (kPad * 2, kPad).withTrimmedRight (24),
                juce::Justification::centredLeft, false);
    auto add = addTrackButton();
    g.setColour (face); g.fillRect (add); g.setColour (kInk); g.drawRect (add, 1);
    g.drawText ("+", add, juce::Justification::centred, false);

    // Track lanes.
    const int n = d->numTracks();
    auto* wsnap = engine.windowById (id);
    const bool playing = wsnap && wsnap->transport->uiState() == se::TransportState::playing;
    const size_t playPos = wsnap ? wsnap->transport->uiPlayhead() : 0;
    const se::LoopMode loopMode = wsnap ? wsnap->transport->uiLoopMode() : se::LoopMode::once;

    // What is ACTUALLY playing, frozen when play was pressed — not the document's
    // selection. Selecting elsewhere mid-loop leaves the old region looping (stop
    // and play again is how you re-aim it), so the playhead below and the block in
    // the navigator both come from here. Reading the live selection instead put
    // the playhead in a range the ear was not hearing.
    const bool rolling = wsnap && wsnap->transport->uiState() != se::TransportState::stopped;
    const size_t loopSt   = rolling ? wsnap->transport->uiPlaySt()  : 0;
    const size_t loopEnd  = rolling ? wsnap->transport->uiPlayEnd() : 0;
    const int loopFirstTr = rolling ? wsnap->transport->uiFirstPlayTrack() : 1;
    const int loopLastTr  = rolling ? wsnap->transport->uiLastPlayTrack()  : 1;
    for (int t = 0; t < n; ++t)
    {
        // Output channel: one three-way selector, L / L+R / R top to bottom. The
        // chosen cell is inverted, the same "on" convention as the transport.
        const auto& track = d->tracks[(size_t) t];
        static const char* const outLabels[] = { "L", "L+R", "R" };
        const int chosen = outSelection (track);
        for (int i = 0; i < 3; ++i)
        {
            auto b = outRect (t, i);
            const bool on = i == chosen;
            g.setColour (on ? kInk : face);  g.fillRect (b);
            g.setColour (kInk);              g.drawRect (b, 1);
            g.setColour (on ? kWhite : kInk);
            g.setFont (juce::FontOptions (std::min (8.0f, (float) b.getHeight() - 1.0f)));
            g.drawText (outLabels[i], b, juce::Justification::centred, false);
        }

        // Right controls: move up / down / trash. The colour has to be set here:
        // without it these inherited whatever the selector loop above left behind,
        // so a track routed to R — the last cell drawn, and the lit one — painted
        // its three controls in WHITE and they all but disappeared. Every R track
        // and the right half of every stereo import was affected.
        g.setColour (kInk);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String::fromUTF8 ("\xE2\x96\xB2"), ctrlRect (t, 0), juce::Justification::centred, false);
        g.drawText (juce::String::fromUTF8 ("\xE2\x96\xBC"), ctrlRect (t, 1), juce::Justification::centred, false);
        g.drawText (juce::String::fromUTF8 ("\xC3\x97"),     ctrlRect (t, 2), juce::Justification::centred, false);

        // Waveform on a shared byte-time axis. Silence (real samples) draws as a
        // solid line; absence (past this track's end) draws as a dotted line.
        auto wr = waveRect (t);
        g.setColour (bed); g.fillRect (wr);
        const float mid = (float) wr.getCentreY();
        const float amp = (float) wr.getHeight() * 0.5f - 3.0f;
        const size_t lenT = track.data.size();
        // Zoomed in, this track's end is routinely off one side of the lane or the
        // other, so the split between "audio" and "past the end" is clamped rather
        // than trusted: unclamped it drew the baseline backwards, or dotted the
        // whole lane of a track that is entirely on screen.
        const int endX = std::clamp (xAtByte (lenT, maxSize), wr.getX(), wr.getRight());

        g.setColour (kWave);
        g.drawHorizontalLine ((int) mid, (float) wr.getX(), (float) endX);   // silence: solid baseline
        if (t < (int) peaks.size())
        {
            const auto& cols = peaks[(size_t) t];
            for (int x = 0; x < (int) cols.size() && wr.getX() + x < endX; ++x)
            {
                const auto r = cols[(size_t) x];
                if (r.getStart() == 0.0f && r.getEnd() == 0.0f) continue;
                g.drawVerticalLine (wr.getX() + x, mid - r.getEnd() * amp, mid - r.getStart() * amp + 1.0f);
            }
        }
        for (int x = endX + 1; x < wr.getRight(); x += 3)                    // absence: dotted
            g.fillRect (x, (int) mid, 1, 1);

        // Per-track playhead (each track at its OWN loop phase).
        if (playing && lenT > 0)
        {
            const size_t fs = (size_t) std::max (1, d->frameSize());
            const size_t lenA = lenT - lenT % fs;
            bool present = true; size_t ph = 0;
            switch (loopMode)
            {
                case se::LoopMode::once: present = playPos < lenA; ph = playPos; break;
                case se::LoopMode::all:  ph = lenA ? playPos % lenA : 0; break;
                case se::LoopMode::selection:
                    if (t >= loopFirstTr - 1 && t <= loopLastTr - 1)
                    {
                        const size_t loopLen = loopEnd > loopSt ? loopEnd - loopSt : 0;
                        if (loopLen > 0) { ph = loopSt + (playPos >= loopSt ? playPos - loopSt : 0) % loopLen; present = ph < lenA; }
                        else ph = lenA ? playPos % lenA : 0;
                    }
                    else present = false;
                    break;
            }
            // ...and only if it is inside the view. Zoomed in, the playhead spends
            // most of its pass off-lane; drawn anyway it would sit on the edge and
            // read as a playhead that had stopped there.
            const int px = xAtByte (ph, maxSize);
            if (present && px >= wr.getX() && px < wr.getRight())
            { g.setColour (kInk); g.drawVerticalLine (px, (float) wr.getY(), (float) wr.getBottom()); }
        }
    }

    // Rules LAST, so the beds cannot paint over them: the block is one framed well
    // with a hairline between each pair of lanes, spanning the padded width rather
    // than running into the frame. (Drawn per lane before the bed, as they used to
    // be, every separator vanished across the waveform region and survived only in
    // the two control columns either side of it.)
    g.setColour (kInk);
    for (int t = 0; t < n - 1; ++t)
    {
        auto lane = laneRect (t);
        g.drawHorizontalLine (lane.getBottom() - 1, (float) lane.getX(), (float) lane.getRight());
    }
    g.drawRect (lanesRect(), 1);

    // Selection overlay (across the selected track range) — or, when it is empty,
    // the INSERTION POINT. A click without a drag has always set one (an empty
    // selection at the clicked byte) and paste has always honoured it, but nothing
    // drew it, so the click looked like it had done nothing at all. It is an I-beam
    // caret, serifed, so it cannot be read as the playhead: that is a plain rule.
    //
    // Both are CLIPPED to the lane. Zoomed in, the selection routinely runs off one
    // or both ends of the view, and an unclipped fill would spill sideways over the
    // control columns and the window frame.
    if (d->selSt != d->selEnd)
    {
        const int rawL = xAtByte (d->selSt, maxSize), rawR = xAtByte (d->selEnd, maxSize);
        g.setColour (kSelFill);
        for (int t = d->firstSelTrack - 1; t <= d->lastSelTrack - 1 && t < n; ++t)
        {
            if (t < 0) continue;
            auto wr = waveRect (t);
            if (rawR < wr.getX() || rawL > wr.getRight()) continue;   // wholly off-view
            const int x0 = std::max (rawL, wr.getX()), x1 = std::min (rawR, wr.getRight());
            g.fillRect (Rectangle<int> (x0, wr.getY(), std::max (1, x1 - x0), wr.getHeight()));
        }
    }
    else
    {
        const int cx = xAtByte (d->selSt, maxSize);
        g.setColour (kInk);
        for (int t = d->firstSelTrack - 1; t <= d->lastSelTrack - 1 && t < n; ++t)
        {
            if (t < 0) continue;
            auto wr = waveRect (t).reduced (0, 2);
            if (cx < wr.getX() + 2 || cx > wr.getRight() - 3) continue;   // serifs need room
            g.drawVerticalLine (cx, (float) wr.getY(), (float) wr.getBottom());
            g.fillRect (cx - 2, wr.getY(), 5, 1);              // serifs, top and bottom
            g.fillRect (cx - 2, wr.getBottom() - 1, 5, 1);
        }
    }

    // Navigator: zoom in · zoom out · whole sound, then ◀ well ▶. The well is the
    // whole sound at a glance — the blue bar is the slice the lanes above are
    // showing, the black block is what is PLAYING (stopped: what would play, i.e.
    // the selection).
    {
        const bool canZoomIn  = viewSpanFrac > minSpanFrac();
        const bool canZoomOut = viewSpanFrac < 1.0;
        const bool canLeft    = viewStartFrac > 0.0;
        const bool canRight   = viewStartFrac + viewSpanFrac < 1.0;
        const bool enabled[5] = { canZoomIn, canZoomOut, canZoomOut, canLeft, canRight };

        for (int i = 0; i < 5; ++i)
        {
            auto b = navButton (i);
            g.setColour (face);  g.fillRect (b);
            g.setColour (kInk);  g.drawRect (b, 1);
            // A button with nothing left to do says so, rather than clicking and
            // appearing to do nothing: fully zoomed out, that is three of the five.
            g.setColour (enabled[i] ? kInk : kInk.withAlpha (0.30f));
            drawNavIcon (g, i, b);
        }

        auto well = navWellRect();
        g.setColour (kWhite); g.fillRect (well);
        g.setColour (kInk);   g.drawRect (well, 1);

        auto inner = navWellInner();
        auto barRow = inner.removeFromBottom (std::max (4, inner.getHeight() / 2));
        auto topRow = inner;

        // Quarter ticks, so a glance at the well is a glance at a position.
        g.setColour (kInk);
        for (int i = 0; i <= 4; ++i)
        {
            const int x = topRow.getX() + (int) std::lround ((double) i / 4.0 * (topRow.getWidth() - 1));
            g.drawVerticalLine (x, (float) topRow.getY(), (float) topRow.getBottom());
        }

        const auto spanX = [&] (Rectangle<int> row, double f0, double f1)
        {
            const int x0 = row.getX() + (int) std::lround (std::clamp (f0, 0.0, 1.0) * row.getWidth());
            const int x1 = row.getX() + (int) std::lround (std::clamp (f1, 0.0, 1.0) * row.getWidth());
            return Rectangle<int> (x0, row.getY(), std::max (2, x1 - x0), row.getHeight());
        };

        // The block: the looping region while it rolls, the selection while stopped.
        // They usually coincide — until you select somewhere else mid-loop, and then
        // the block is the only thing on screen still telling you what you can hear.
        const bool showLoop = rolling && loopEnd > loopSt;
        const bool hasSel   = d->selEnd > d->selSt;
        const bool diverged = showLoop && hasSel && (d->selSt != loopSt || d->selEnd != loopEnd);

        // Once they HAVE diverged, the selection gets a hollow box: solid is what
        // the ear is getting, hollow is what the next play would take. Drawn FIRST,
        // over its own white, so the quarter ticks do not run through it and split
        // it in two — and so the solid loop block wins any overlap.
        if (maxSize > 0 && diverged)
        {
            const auto r = spanX (topRow, (double) d->selSt  / (double) maxSize,
                                          (double) d->selEnd / (double) maxSize);
            g.setColour (kWhite); g.fillRect (r);
            g.setColour (kInk);   g.drawRect (r, 1);
        }

        if (maxSize > 0 && (showLoop || hasSel))
        {
            const size_t b0 = showLoop ? loopSt  : d->selSt;
            const size_t b1 = showLoop ? loopEnd : d->selEnd;
            g.fillRect (spanX (topRow, (double) b0 / (double) maxSize,
                                       (double) b1 / (double) maxSize));
        }

        g.setColour (kNavView);
        g.fillRect (spanX (barRow, viewStartFrac, viewStartFrac + viewSpanFrac));
    }

    // Transport strip.
    auto tb = transportBounds();
    g.setColour (kInk); g.drawRect (tb, 1);
    // record · stop · play/pause · 2x · loop. The play button doubles as pause
    // while playing, so the pair is one toggle rather than two buttons.
    for (int i = 0; i < kNumTransportButtons; ++i)
    {
        auto b = transportButton (i);
        const bool lit = (i == 2 && playing) || (i == 3 && advanceEnabled) || (i == 4 && loopEnabled);
        // Record greys out while audio input is muted — frame AND glyph, not just
        // the glyph: a black box around a grey dot reads as a live button whose icon
        // happens to be pale. It stays clickable either way; see mouseDown.
        const bool greyed = (i == 0 && ! recordEnabled);
        g.setColour (lit ? kInk : face); g.fillRect (b);
        g.setColour (greyed ? kRecordOff : (lit ? kWhite : kInk)); g.drawRect (b, 1);
        g.setColour (greyed ? kRecordOff : (i == 0 ? kRecord : (lit ? kWhite : kInk)));
        drawTransportSymbol (g, i, b, playing);
    }

    // Grow box: the System 7 corner grip, drag it to resize.
    auto grip = resizeGripBounds();
    g.setColour (bed); g.fillRect (grip);
    g.setColour (kInk);   g.drawRect (grip, 1);
    for (int i = 3; i < kGripW - 1; i += 3)
        g.drawLine ((float) (grip.getRight() - i), (float) (grip.getBottom() - 2),
                    (float) (grip.getRight() - 2), (float) (grip.getBottom() - i));

    // Border LAST, so the highlight is never partly overdrawn by the contents.
    // Weight is the whole signal: plain, hovered, active. Since Windows in the
    // grid touch, a thicker border grows inwards and never disturbs a neighbour.
    const int border = selected ? kBorderActive
                     : hovered  ? kBorderHover
                                : kBorderIdle;
    g.setColour (kInk);
    g.drawRect (getLocalBounds(), border);
}

void WindowComponent::mouseEnter (const juce::MouseEvent&) { hovered = true;  repaint(); }
void WindowComponent::mouseExit  (const juce::MouseEvent&) { hovered = false; repaint(); }
void WindowComponent::focusGained (FocusChangeType) { repaint(); }
void WindowComponent::focusLost   (FocusChangeType) { repaint(); }

void WindowComponent::invalidateWaveform()
{
    cachedWidth = -1;

    // A module that ADDS tracks — Selection to Channel, Iterative Crossfade, and
    // Quick Mix or Difference rendering to a New Channel — leaves this Window laid
    // out for the count it had a moment ago. The extra lanes then draw underneath
    // the transport bar and the last one is off the bottom entirely, so the effect
    // reports success and the result is invisible until you resize by hand.
    //
    // Every explicit sizing path ends at `chromeHeight() + n * laneH`, which is
    // exactly what preferredHeight() returns — so a mismatch here means the TRACK
    // COUNT moved and nothing else. That makes this safe to check on every
    // document change: a Window the user resized still matches, and only a real
    // change in track count re-fits.
    if (getHeight() != preferredHeight())
    {
        resizeToFit();   // repaints, re-stores geometry, and relays out the board
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

void WindowComponent::setRecordEnabled (bool enabled)
{
    if (recordEnabled == enabled) return;
    recordEnabled = enabled;
    repaint();
}

// A narrower lane holds fewer columns, so the zoom floor moves with the width —
// a window shrunk while zoomed all the way in has to give some magnification back.
void WindowComponent::resized() { cachedWidth = -1; clampView(); }

// --- interaction ------------------------------------------------------------

juce::Point<int> WindowComponent::parentPos (const juce::MouseEvent& e) const
{
    if (auto* parent = getParentComponent())
        return e.getEventRelativeTo (parent).getPosition();
    return e.getScreenPosition();
}

void WindowComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingTitle = false; selecting = false; resizingWindow = false; navDragging = false;
    // Focus for the keyboard shortcuts (⌘X/⌘C/⌘V, Delete, Space); selection for
    // everything that has to outlive focus moving to a panel.
    grabKeyboardFocus();
    if (onSelected) onSelected (id);
    auto* d = doc();
    if (d == nullptr) return;
    const auto p = e.getPosition();
    const size_t maxSize = d->maxTrackSize();
    const int n = d->numTracks();

    if (resizeGripBounds().contains (p))
    {
        resizingWindow = true;
        dragMouseStart = parentPos (e);
        resizeSizeStart = { getWidth(), getHeight() };
        toFront (true);
        return;
    }

    if (closeBoxBounds().contains (p)) { if (onClose) onClose (id); return; }
    if (addTrackButton().contains (p)) { d->addTrack(); commit(); resizeToFit(); return; }

    // Navigator. It only ever moves the VIEW — no click in this strip touches the
    // document, the selection or the transport, so nothing here needs a commit.
    if (navRect().contains (p))
    {
        if (navButton (0).contains (p)) { zoomBy (0.5); repaint(); return; }
        if (navButton (1).contains (p)) { zoomBy (2.0); repaint(); return; }
        if (navButton (2).contains (p)) { viewStartFrac = 0.0; viewSpanFrac = 1.0;
                                          cachedWidth = -1; repaint(); return; }
        if (navButton (3).contains (p)) { panBy (-0.5); repaint(); return; }
        if (navButton (4).contains (p)) { panBy (+0.5); repaint(); return; }
        if (navWellRect().contains (p))
        {
            // Click anywhere in the well to jump there; hold and it scrubs.
            navDragging = true;
            setViewCentreFromWellX (p.getX());
            repaint();
        }
        return;
    }

    if (transportBounds().contains (p))
    {
        for (int i = 0; i < kNumTransportButtons; ++i)
            if (transportButton (i).contains (p))
            {
                switch (i)
                {
                    // Record while greyed — input muted — opens the audio device
                    // settings, which is where input gets turned on. Once it IS on
                    // the button is live and this is where recording will go; there
                    // is nothing behind it yet, so for now the click does nothing.
                    case 0:
                        if (! recordEnabled && onRecordRequested) onRecordRequested();
                        break;
                    case 1: engine.stop (id); advanceEnabled = false; break;
                    case 2: togglePlayPause(); break;       // same toggle as Space
                    case 3:
                        // 2x engaged / disengaged. advance() starts playback if
                        // the transport is stopped, so engaging it also rolls.
                        if (advanceEnabled) engine.normalSpeed (id);
                        else                engine.advance (id);
                        advanceEnabled = ! advanceEnabled;
                        break;
                    case 4: loopEnabled = ! loopEnabled; break;
                    default: break;
                }
                repaint(); return;
            }
    }

    for (int t = 0; t < n; ++t)
    {
        for (int i = 0; i < 3; ++i)
            if (outRect (t, i).contains (p))
            {
                auto& tr = d->tracks[(size_t) t];
                tr.outLeft  = i != 2;    // L or L+R
                tr.outRight = i != 0;    // R or L+R
                commit(); repaint(); return;
            }
        if (ctrlRect (t, 0).contains (p)) { if (t > 0)     { d->swapTracks (t + 1, t);     commit(); repaint(); } return; }
        if (ctrlRect (t, 1).contains (p)) { if (t < n - 1) { d->swapTracks (t + 1, t + 2); commit(); repaint(); } return; }
        if (ctrlRect (t, 2).contains (p)) { if (d->delTrack (t + 1)) { commit(); resizeToFit(); } return; }
        if (waveRect (t).contains (p))
        {
            // Double-click: the whole track. Triple-click: every track, all of it.
            // Neither arms a drag — the selection is final on the click itself, so
            // it is published here rather than waiting for mouseUp.
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
    }

    if (p.getY() < kTitleH)
    {
        draggingTitle = true;
        dragMouseStart = parentPos (e);
        dragBoundsStart = getBounds().getPosition();
        toFront (true);
    }
}

void WindowComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (resizingWindow)
    {
        const auto delta = parentPos (e) - dragMouseStart;
        applySize (resizeSizeStart.getX() + delta.getX(),
                   resizeSizeStart.getY() + delta.getY());
        cachedWidth = -1;
        storeGeometry();
        // Width first: in the grid the whole column takes this width, and the
        // relayout right behind it is what draws the result.
        if (onWidthChanged) onWidthChanged (desiredW);
        if (onGeometryChanged) onGeometryChanged();
        repaint();
        return;
    }
    if (navDragging)
    {
        setViewCentreFromWellX (e.getPosition().getX());
        repaint();
        return;
    }
    if (draggingTitle)
    {
        auto np = dragBoundsStart + (parentPos (e) - dragMouseStart);
        np = { std::max (0, np.getX()), std::max (0, np.getY()) };   // stay on the board
        setTopLeftPosition (np);
        storeGeometry();
        // Report the window's own top-left, not the pointer: the cell you are
        // aiming at is the one the window's corner is over.
        if (onDragged) onDragged (np);
        return;
    }
    if (selecting)
    {
        // Not until the pointer has really travelled. A click is meant to leave an
        // insertion point, and without the threshold a few pixels of hand-shake
        // between press and release turned it into a one-frame selection instead —
        // which looks the same but is NOT the same thing to paste or to Play.
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

void WindowComponent::mouseUp (const juce::MouseEvent&)
{
    if (selecting) commit();       // publish the selection so Play can loop it
    if (draggingTitle && onDropped) onDropped();
    draggingTitle = false; selecting = false; resizingWindow = false; navDragging = false;
}

void WindowComponent::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (resizeGripBounds().contains (e.getPosition())
                        ? juce::MouseCursor::BottomRightCornerResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void WindowComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (p.getY() < kTitleH && ! closeBoxBounds().contains (p))
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
    auto* w = engine.windowById (id);
    const auto state = w != nullptr ? w->transport->uiState() : se::TransportState::stopped;

    // One toggle. Transport::pause() already flips playing <-> paused, so only a
    // STOPPED transport needs play() — calling it while paused would restart from
    // the top.
    if (state == se::TransportState::stopped)
    {
        se::PlayOptions o; o.loop = loopEnabled;
        engine.play (id, o);
        advanceEnabled = false;         // play() resets advanceScale
    }
    else engine.pause (id);

    repaint();
}

void WindowComponent::beginNameEdit()
{
    auto* d = doc();
    if (d == nullptr || nameEditor != nullptr) return;

    draggingTitle = false;   // the double-click's own mouseDown armed a title drag

    nameEditor = std::make_unique<juce::TextEditor>();
    auto& ed = *nameEditor;
    ed.setBounds (nameBounds().reduced (0, 2));
    ed.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    ed.setJustification (juce::Justification::centred);
    ed.setMultiLine (false);
    ed.setReturnKeyStartsNewLine (false);
    ed.setBorder (juce::BorderSize<int> (1));
    // A white well with a black frame: the same System 7 grammar as everything else.
    ed.setColour (juce::TextEditor::backgroundColourId, kWhite);
    ed.setColour (juce::TextEditor::textColourId, kInk);
    ed.setColour (juce::TextEditor::outlineColourId, kInk);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, kInk);
    ed.setColour (juce::TextEditor::highlightColourId, kInk.withAlpha (0.25f));
    ed.setColour (juce::TextEditor::highlightedTextColourId, kInk);
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

bool WindowComponent::keyPressed (const juce::KeyPress& k)
{
    auto* d = doc(); if (d == nullptr) return false;
    const int code = k.getKeyCode();

    // Space: play/pause the ACTIVE Window — the one holding keyboard focus, which
    // is the one the thick border is already pointing at. Same toggle as the
    // transport's play button, so the two can never disagree.
    if (code == juce::KeyPress::spaceKey && ! k.getModifiers().isAnyModifierKeyDown())
    {
        togglePlayPause();
        return true;
    }

    // Delete / Backspace: remove selected audio (like Cut — splices out, shortens).
    // Option/Alt + Delete: crop — keep the selection, discard the rest.
    // Shift + Delete: replace the selected audio with silence (no length change).
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (k.getModifiers().isAltDown())        se::crop (*d);
        else if (k.getModifiers().isShiftDown()) se::silenceSelection (*d);
        else                                     se::clear (*d);
        commit(); resizeToFit();
        return true;
    }

    if (! k.getModifiers().isCommandDown()) return false;

    // On macOS a Command-combo yields no text character, so match on the key code
    // (modifier-independent), accepting either case.
    const auto is = [code] (int lo, int up) { return code == lo || code == up; };
    if (is ('a', 'A')) { selectAllAudio(); return true; }
    if (is ('c', 'C')) { if (clipboard) *clipboard = se::copy (*d); return true; }
    if (is ('x', 'X')) { if (clipboard) { *clipboard = se::cut (*d); commit(); resizeToFit(); } return true; }
    if (is ('v', 'V')) { if (clipboard && *clipboard) { se::paste (*d, **clipboard); commit(); resizeToFit(); } return true; }
    return false;
}
