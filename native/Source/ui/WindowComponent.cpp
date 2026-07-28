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

int WindowComponent::waveWidth() const noexcept { return std::max (1, getWidth() - kLrW - kCtrlW); }

Rectangle<int> WindowComponent::infoRect() const { return { 0, kTitleH, getWidth(), kInfoH }; }

Rectangle<int> WindowComponent::nameBounds() const
{
    return Rectangle<int> (0, 0, getWidth(), kTitleH).withTrimmedLeft (26).withTrimmedRight (26);
}

Rectangle<int> WindowComponent::addTrackButton() const
{
    auto r = infoRect();
    return { r.getRight() - 20, r.getY() + 6, 15, 15 };
}

Rectangle<int> WindowComponent::laneRect (int t) const
{
    return { 0, kTitleH + kInfoH + t * laneH, getWidth(), laneH };
}

Rectangle<int> WindowComponent::resizeGripBounds() const
{
    return { getWidth() - kGripW - 1, getHeight() - kGripW - 1, kGripW, kGripW };
}

// The output-channel selector and the up/down/trash controls are chrome: they keep
// a fixed size no matter how tall the lane is, pinned to the top of the lane. Only
// the waveform between them grows.

Rectangle<int> WindowComponent::outRect (int t, int which) const
{
    auto lane = laneRect (t);
    auto col = lane.removeFromLeft (kLrW).reduced (2, 0);
    const int h = std::clamp ((lane.getHeight() - 4) / 3, 4, 10);
    const int top = lane.getY() + 2;
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
    return { col.getX(), col.getY() + which * h, col.getWidth(), h };
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
    return { 0, getHeight() - kTransportH, getWidth(), kTransportH };
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
    const double frac = std::clamp ((double) (x - kLrW) / (double) waveWidth(), 0.0, 1.0);
    size_t b = (size_t) (frac * (double) maxSize);
    const size_t fs = (size_t) std::max (1, doc() ? doc()->frameSize() : 1);
    return b - (b % fs);
}

int WindowComponent::xAtByte (size_t b, size_t maxSize) const noexcept
{
    if (maxSize == 0) return kLrW;
    return kLrW + (int) ((double) b / (double) maxSize * (double) waveWidth());
}

// --- waveform peaks (shared byte axis over the longest track) ---------------

void WindowComponent::ensurePeaks (se::SoundDocument& d)
{
    const int n = d.numTracks();
    const int w = waveWidth();
    const size_t maxSize = d.maxTrackSize();

    bool changed = ((int) cachedSizes.size() != n) || (cachedWidth != w) || (cachedMax != maxSize);
    if (! changed)
        for (int t = 0; t < n; ++t)
            if (cachedSizes[(size_t) t] != d.tracks[(size_t) t].data.size()) { changed = true; break; }
    if (! changed) return;

    cachedWidth = w; cachedMax = maxSize;
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
            size_t b0 = (size_t) ((double) x       / w * (double) maxSize);
            size_t b1 = (size_t) ((double) (x + 1) / w * (double) maxSize);
            b0 -= b0 % (size_t) frameSize;
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

// --- paint ------------------------------------------------------------------

void WindowComponent::paint (Graphics& g)
{
    auto* d = doc();
    if (d == nullptr) return;
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
    g.drawText (l, info.reduced (6, 2).withTrimmedRight (24), juce::Justification::centredLeft, false);
    auto add = addTrackButton();
    g.setColour (face); g.fillRect (add); g.setColour (kInk); g.drawRect (add, 1);
    g.drawText ("+", add, juce::Justification::centred, false);

    // Track lanes.
    const int n = d->numTracks();
    auto* wsnap = engine.windowById (id);
    const bool playing = wsnap && wsnap->transport->uiState() == se::TransportState::playing;
    const size_t playPos = wsnap ? wsnap->transport->uiPlayhead() : 0;
    const se::LoopMode loopMode = wsnap ? wsnap->transport->uiLoopMode() : se::LoopMode::once;
    for (int t = 0; t < n; ++t)
    {
        auto lane = laneRect (t);
        g.setColour (kInk); g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

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

        // Right controls: move up / down / trash.
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
        const int endX = std::min (xAtByte (lenT, maxSize), wr.getRight());

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
                    if (t >= d->firstSelTrack - 1 && t <= d->lastSelTrack - 1)
                    {
                        const size_t selLen = d->selEnd > d->selSt ? d->selEnd - d->selSt : 0;
                        if (selLen > 0) { ph = d->selSt + (playPos >= d->selSt ? playPos - d->selSt : 0) % selLen; present = ph < lenA; }
                        else ph = lenA ? playPos % lenA : 0;
                    }
                    else present = false;
                    break;
            }
            if (present) { g.setColour (kInk); g.drawVerticalLine (xAtByte (ph, maxSize), (float) wr.getY(), (float) wr.getBottom()); }
        }
    }

    // Selection overlay (across the selected track range).
    if (d->selSt != d->selEnd)
    {
        const int x0 = xAtByte (d->selSt, maxSize), x1 = xAtByte (d->selEnd, maxSize);
        g.setColour (kSelFill);
        for (int t = d->firstSelTrack - 1; t <= d->lastSelTrack - 1 && t < n; ++t)
            if (t >= 0) { auto wr = waveRect (t); g.fillRect (Rectangle<int> (x0, wr.getY(), std::max (1, x1 - x0), wr.getHeight())); }
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
        g.setColour (lit ? kInk : face); g.fillRect (b);
        g.setColour (lit ? kWhite : kInk); g.drawRect (b, 1);
        g.setColour (i == 0 ? kRecord : (lit ? kWhite : kInk));
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
    repaint();
}

void WindowComponent::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected) return;
    selected = shouldBeSelected;
    repaint();
}

void WindowComponent::resized() { cachedWidth = -1; }

// --- interaction ------------------------------------------------------------

juce::Point<int> WindowComponent::parentPos (const juce::MouseEvent& e) const
{
    if (auto* parent = getParentComponent())
        return e.getEventRelativeTo (parent).getPosition();
    return e.getScreenPosition();
}

void WindowComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingTitle = false; selecting = false; resizingWindow = false;
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

    if (transportBounds().contains (p))
    {
        for (int i = 0; i < kNumTransportButtons; ++i)
            if (transportButton (i).contains (p))
            {
                switch (i)
                {
                    case 0: break;                          // record — not wired up yet
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
        auto* d = doc(); if (d == nullptr) return;
        const size_t maxSize = d->maxTrackSize();
        const int n = d->numTracks();
        const int curTrack = std::clamp ((e.getPosition().getY() - (kTitleH + kInfoH)) / laneH, 0, n - 1);
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
    draggingTitle = false; selecting = false; resizingWindow = false;
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
    // Option/Alt + Delete: replace the selected audio with silence (no length change).
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (k.getModifiers().isAltDown()) se::silenceSelection (*d);
        else                             se::clear (*d);
        commit(); resizeToFit();
        return true;
    }

    if (! k.getModifiers().isCommandDown()) return false;

    // On macOS a Command-combo yields no text character, so match on the key code
    // (modifier-independent), accepting either case.
    const auto is = [code] (int lo, int up) { return code == lo || code == up; };
    if (is ('c', 'C')) { if (clipboard) *clipboard = se::copy (*d); return true; }
    if (is ('x', 'X')) { if (clipboard) { *clipboard = se::cut (*d); commit(); resizeToFit(); } return true; }
    if (is ('v', 'V')) { if (clipboard && *clipboard) { se::paste (*d, **clipboard); commit(); resizeToFit(); } return true; }
    return false;
}
