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
    return kTitleH + kInfoH + numTracks() * kLaneH + kTransportH;
}

void WindowComponent::refresh()
{
    if (doc() == nullptr) { if (onClose) onClose (id); return; }
    repaint();
}

void WindowComponent::resizeToFit()
{
    setSize (preferredWidth(), preferredHeight());
    cachedWidth = -1;
    repaint();
}

// --- geometry ---------------------------------------------------------------

int WindowComponent::waveWidth() const noexcept { return std::max (1, getWidth() - kLrW - kCtrlW); }

Rectangle<int> WindowComponent::infoRect() const { return { 0, kTitleH, getWidth(), kInfoH }; }

Rectangle<int> WindowComponent::addTrackButton() const
{
    auto r = infoRect();
    return { r.getRight() - 20, r.getY() + 6, 15, 15 };
}

Rectangle<int> WindowComponent::laneRect (int t) const
{
    return { 0, kTitleH + kInfoH + t * kLaneH, getWidth(), kLaneH };
}

Rectangle<int> WindowComponent::lrRect (int t, bool left) const
{
    auto lane = laneRect (t);
    auto col = lane.removeFromLeft (kLrW).reduced (2);
    return left ? col.removeFromTop (col.getHeight() / 2).reduced (0, 1)
                : col.removeFromBottom (col.getHeight() / 2).reduced (0, 1);
}

Rectangle<int> WindowComponent::ctrlRect (int t, int which) const
{
    auto lane = laneRect (t);
    auto col = lane.removeFromRight (kCtrlW);
    const int h = col.getHeight() / 3;
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
    auto bar = transportBounds().reduced (4, 4);
    const int bw = 22, gap = 4;
    return { bar.getX() + i * (bw + gap), bar.getY(), bw, bar.getHeight() };
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

// --- paint ------------------------------------------------------------------

void WindowComponent::paint (Graphics& g)
{
    auto* d = doc();
    if (d == nullptr) return;
    ensurePeaks (*d);
    const size_t maxSize = d->maxTrackSize();

    auto full = getLocalBounds();
    g.setColour (kFace); g.fillRect (full);
    g.setColour (kInk);  g.drawRect (full, 1);

    // Title bar.
    auto title = full.removeFromTop (kTitleH);
    g.setColour (kInk); g.drawHorizontalLine (title.getBottom() - 1, (float) title.getX(), (float) title.getRight());
    for (int y = title.getY() + 3; y < title.getBottom() - 3; y += 2)
        g.drawHorizontalLine (y, (float) (title.getX() + 22), (float) (title.getRight() - 22));
    g.setColour (kInk); g.drawRect (closeBoxBounds(), 1);
    { auto na = title.withTrimmedLeft (26).withTrimmedRight (26);
      g.setColour (kFace); g.fillRect (na.reduced (24, 2));
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
    g.setColour (kFace); g.fillRect (add); g.setColour (kInk); g.drawRect (add, 1);
    g.drawText ("+", add, juce::Justification::centred, false);

    // Track lanes.
    const int n = d->numTracks();
    for (int t = 0; t < n; ++t)
    {
        auto lane = laneRect (t);
        g.setColour (kInk); g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

        // L/R output toggles.
        const auto& track = d->tracks[(size_t) t];
        auto lB = lrRect (t, true), rB = lrRect (t, false);
        g.setColour (kInk); g.drawRect (lB, 1); if (track.outLeft)  g.fillRect (lB.reduced (2));
        g.drawRect (rB, 1); if (track.outRight) g.fillRect (rB.reduced (2));

        // Right controls: move up / down / trash.
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String::fromUTF8 ("\xE2\x96\xB2"), ctrlRect (t, 0), juce::Justification::centred, false);
        g.drawText (juce::String::fromUTF8 ("\xE2\x96\xBC"), ctrlRect (t, 1), juce::Justification::centred, false);
        g.drawText (juce::String::fromUTF8 ("\xC3\x97"),     ctrlRect (t, 2), juce::Justification::centred, false);

        // Waveform.
        auto wr = waveRect (t);
        g.setColour (kWhite); g.fillRect (wr);
        const float mid = (float) wr.getCentreY();
        const float amp = (float) wr.getHeight() * 0.5f - 3.0f;
        g.setColour (kWave);
        if (t < (int) peaks.size())
        {
            const auto& cols = peaks[(size_t) t];
            for (int x = 0; x < (int) cols.size(); ++x)
            {
                const auto r = cols[(size_t) x];
                if (r.getStart() == 0.0f && r.getEnd() == 0.0f) continue;
                g.drawVerticalLine (wr.getX() + x, mid - r.getEnd() * amp, mid - r.getStart() * amp + 1.0f);
            }
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

    // Playhead (single line across all lanes).
    if (auto* w = engine.windowById (id))
        if (w->transport->uiState() == se::TransportState::playing)
        {
            const int px = xAtByte (w->transport->uiPlayhead(), maxSize);
            g.setColour (kInk);
            g.drawVerticalLine (px, (float) (kTitleH + kInfoH), (float) (getHeight() - kTransportH));
        }

    // Transport strip.
    auto tb = transportBounds();
    g.setColour (kInk); g.drawRect (tb, 1);
    const char* glyph[6] = { "\xE2\x97\x8F", "\xE2\x96\xA0", "II", "\xE2\x96\xB6", "\xE2\x96\xB6\xE2\x96\xB6", "\xE2\x86\xBB" };
    const bool playing = engine.windowById (id) && engine.windowById (id)->transport->uiState() == se::TransportState::playing;
    for (int i = 0; i < 6; ++i)
    {
        auto b = transportButton (i);
        const bool lit = (i == 3 && playing) || (i == 5 && loopEnabled);
        g.setColour (lit ? kInk : kFace); g.fillRect (b);
        g.setColour (lit ? kWhite : kInk); g.drawRect (b, 1);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String::fromUTF8 (glyph[i]), b, juce::Justification::centred, false);
    }
}

void WindowComponent::resized() { cachedWidth = -1; }

// --- interaction ------------------------------------------------------------

void WindowComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingTitle = false; selecting = false;
    grabKeyboardFocus();
    auto* d = doc();
    if (d == nullptr) return;
    const auto p = e.getPosition();
    const size_t maxSize = d->maxTrackSize();
    const int n = d->numTracks();

    if (closeBoxBounds().contains (p)) { if (onClose) onClose (id); return; }
    if (addTrackButton().contains (p)) { d->addTrack(); commit(); resizeToFit(); return; }

    if (transportBounds().contains (p))
    {
        for (int i = 0; i < 6; ++i)
            if (transportButton (i).contains (p))
            {
                switch (i)
                {
                    case 1: engine.stop (id); break;
                    case 2: engine.pause (id); break;
                    case 3: { se::PlayOptions o; o.loop = loopEnabled; engine.play (id, o); break; }
                    case 4: engine.advance (id); break;
                    case 5: loopEnabled = ! loopEnabled; break;
                    default: break;
                }
                repaint(); return;
            }
    }

    for (int t = 0; t < n; ++t)
    {
        if (lrRect (t, true).contains (p))  { auto& tr = d->tracks[(size_t) t]; tr.outLeft  = ! tr.outLeft;  commit(); repaint(); return; }
        if (lrRect (t, false).contains (p)) { auto& tr = d->tracks[(size_t) t]; tr.outRight = ! tr.outRight; commit(); repaint(); return; }
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
        dragMouseStart = e.getScreenPosition();
        dragBoundsStart = getBounds().getPosition();
        toFront (true);
    }
}

void WindowComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingTitle)
    {
        const auto np = dragBoundsStart + (e.getScreenPosition() - dragMouseStart);
        setTopLeftPosition (np);
        if (auto* w = engine.windowById (id)) { w->x = (float) np.getX(); w->y = (float) np.getY(); }
        return;
    }
    if (selecting)
    {
        auto* d = doc(); if (d == nullptr) return;
        const size_t maxSize = d->maxTrackSize();
        const int n = d->numTracks();
        const int curTrack = std::clamp ((e.getPosition().getY() - (kTitleH + kInfoH)) / kLaneH, 0, n - 1);
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
    draggingTitle = false; selecting = false;
}

bool WindowComponent::keyPressed (const juce::KeyPress& k)
{
    if (! k.getModifiers().isCommandDown()) return false;
    auto* d = doc(); if (d == nullptr) return false;

    // On macOS a Command-combo yields no text character, so match on the key code
    // (modifier-independent), accepting either case.
    const int code = k.getKeyCode();
    const auto is = [code] (int lo, int up) { return code == lo || code == up; };

    if (is ('c', 'C')) { if (clipboard) *clipboard = se::copy (*d); return true; }
    if (is ('x', 'X')) { if (clipboard) { *clipboard = se::cut (*d); commit(); resizeToFit(); } return true; }
    if (is ('v', 'V')) { if (clipboard && *clipboard) { se::paste (*d, **clipboard); commit(); resizeToFit(); } return true; }
    return false;
}
