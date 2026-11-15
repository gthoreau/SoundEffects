#include "MagnetophonComponent.h"

#include <cmath>

namespace {

using namespace os8::colour;

// --- the deck, as drawn in 1199:16185 (content coordinates) -------------------
const juce::Point<float> kLeftReel  { 66.0f, 68.0f };
const juce::Point<float> kRightReel { 202.0f, 68.0f };
const juce::Point<float> kLeftRoller  { 93.0f, 123.5f };
const juce::Point<float> kRightRoller { 186.0f, 123.5f };
constexpr float kRollerRadius = 6.0f;
constexpr float kReelRadius = 48.0f;        // the reel's face
constexpr float kHubRadius = 14.0f;         // an empty pack: the hub
constexpr float kFullPackRadius = 42.0f;    // a full one (Figma's left reel)
const juce::Rectangle<float> kHead { 109.0f, 108.0f, 50.0f, 22.0f };   // "Frames"

/** The nominal reel, in seconds of tape: a longer tape fills the reels instead. */
constexpr double kReelSeconds = 600.0;
/** How fast the tape crosses the deck, in pixels per second of audio: it sets how
    quickly the reels turn (about 0.4 turns a second on a full pack). */
constexpr float kTapeSpeed = 100.0f;

const juce::Colour kStatusNavy { 0xff17212b };
const juce::Colour kBevelDark  { 0xff808080 };
const juce::Colour kHeadShade  { 0xffd0d0d0 };

/** The reel's face ("Subtract" 1199:16204): a disc with three windows, as Figma
    exports it, centred on (50, 50). */
const juce::Path& reelFace()
{
    static const juce::Path p = []
    {
        auto path = juce::Drawable::parseSVGPath (
            "M50 1C77.062 1 99 22.938 99 50C99 77.062 77.062 99 50 99C22.938 99 1 77.062 1 50C1 22.938 22.938 1 50 1Z"
            "M56.6016 69.0977C54.6236 69.811 52.5046 70.2237 50.2988 70.2793L49.7861 70.2861C47.7557 70.2861 45.7942 69.982 43.9434 69.4199"
            "L36.0439 88.5C37.2516 89.3521 41.0797 91.3603 50.3408 91.3604L51.4541 91.3506C59.9097 91.1977 63.4763 89.3173 64.6348 88.5L56.6016 69.0977Z"
            "M63.6826 34.8535C66.9299 37.9411 69.1688 42.0989 69.8506 46.7695L89.8613 44.1328C89.9899 42.7205 89.8335 38.6925 85.7402 31.2959"
            "L85.1904 30.3232C80.5595 22.3024 76.9069 19.9915 75.5654 19.3721L63.6826 34.8535Z"
            "M25.04 19.373C23.7535 19.9677 20.3423 22.1177 15.9814 29.3652L15.416 30.3242C10.786 38.3437 10.6103 42.662 10.7441 44.1338"
            "L29.7383 46.6357C30.4866 41.7339 32.9513 37.4032 36.4941 34.2959L25.04 19.373Z");
        path.setUsingNonZeroWinding (false);
        return path;
    }();
    return p;
}

/** The tape's straight run from circle A to circle B, both centres on its left (in
    screen coordinates): the outer tangent the tape takes round a pack or a roller. */
std::pair<juce::Point<float>, juce::Point<float>> tapeRun (juce::Point<float> a, float ra, juce::Point<float> b, float rb)
{
    const auto d = b - a;
    const float dist = std::max (1.0f, d.getDistanceFromOrigin());
    const float theta = std::atan2 (d.y, d.x);
    const float alpha = theta - std::acos (juce::jlimit (-1.0f, 1.0f, (rb - ra) / dist));
    const juce::Point<float> n { std::cos (alpha), std::sin (alpha) };
    return { a - n * ra, b - n * rb };
}

void drawReel (juce::Graphics& g, juce::Point<float> c, float pack, float angle)
{
    g.setColour (white);
    g.fillEllipse (juce::Rectangle<float> (2 * kReelRadius, 2 * kReelRadius).withCentre (c));
    g.setColour (black);
    g.fillEllipse (juce::Rectangle<float> (2 * pack, 2 * pack).withCentre (c));

    const auto t = juce::AffineTransform::translation (-50.0f, -50.0f).rotated (angle).translated (c);
    g.setColour (gray100);
    g.fillPath (reelFace(), t);
    g.setColour (black);
    g.strokePath (reelFace(), juce::PathStrokeType (2.0f), t);

    // The hub ("Ellipse 10"): a 3 px ring.
    g.drawEllipse (juce::Rectangle<float> (24.35f, 24.35f).withCentre (c), 3.0f);
}

void drawRoller (juce::Graphics& g, juce::Point<float> c)
{
    const auto r = juce::Rectangle<float> (2 * kRollerRadius, 2 * kRollerRadius).withCentre (c);
    // Figma's conic grey (#d9d9d9 → #737373) as a light-to-dark sweep.
    g.setGradientFill (juce::ColourGradient (gray200, r.getTopLeft(), juce::Colour (0xff737373), r.getBottomRight(), false));
    g.fillEllipse (r);
    g.setColour (black);
    g.drawEllipse (r.reduced (0.5f), 1.0f);
}

void drawHead (juce::Graphics& g)
{
    const auto r = kHead;
    juce::Path shape;
    shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 8.0f, 8.0f, true, true, false, false);

    g.setColour (kBevelDark.withAlpha (0.7f));
    g.fillPath (shape, juce::AffineTransform::translation (0.0f, 2.0f));   // hard shadow, 2 px down
    g.setColour (gray000);
    g.fillPath (shape);

    // The bevel: light 3 px in from the top and left, grey from the bottom and right.
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (shape);
        juce::Path inner = shape;
        inner.applyTransform (juce::AffineTransform::translation (3.0f, 3.0f));
        juce::Path light; light.addRectangle (r.expanded (4.0f)); light.addPath (inner); light.setUsingNonZeroWinding (false);
        g.setColour (juce::Colour (0xeefffdfd));
        g.fillPath (light);
        inner = shape;
        inner.applyTransform (juce::AffineTransform::translation (-3.0f, -3.0f));
        juce::Path dark; dark.addRectangle (r.expanded (4.0f)); dark.addPath (inner); dark.setUsingNonZeroWinding (false);
        g.setColour (kHeadShade);
        g.fillPath (dark);
    }
    g.setColour (black);
    juce::Path edge;
    edge.addRoundedRectangle (r.getX() + 0.5f, r.getY() + 0.5f, r.getWidth() - 1.0f, r.getHeight() - 1.0f, 7.5f, 7.5f, true, true, false, false);
    g.strokePath (edge, juce::PathStrokeType (1.0f));
}

juce::String timeText (size_t frames, double rate)
{
    const double s = rate > 0.0 ? (double) frames / rate : 0.0;
    const int cs = (int) std::floor (s * 100.0);
    return juce::String (cs / 6000).paddedLeft ('0', 2) + ":" + juce::String ((cs / 100) % 60).paddedLeft ('0', 2)
         + ":" + juce::String (cs % 100).paddedLeft ('0', 2) + "s";
}

juce::String rateText (double rate)
{
    const double k = rate / 1000.0;
    return (std::abs (k - std::round (k)) < 0.05 ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + "khz";
}

} // namespace

MagnetophonComponent::MagnetophonComponent (se::Engine& e, se::TapeDeck& d) : engine (e), deck (d)
{
    for (auto* b : std::initializer_list<juce::Button*> { &sourceButton, &recordButton, &stopButton, &previousButton,
                                                         &playButton, &nextButton, &sendButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    sourceButton.setTextStyle (os8::TextStyle::smallLabel);

    sourceButton.onClick = [this] { if (onSourceRequested) onSourceRequested(); };
    recordButton.onClick = [this]
    {
        if (onRecordRequested) onRecordRequested(); else deck.record();
        refresh();
    };
    stopButton.onClick     = [this] { deck.stop(); refresh(); };
    previousButton.onClick = [this] { deck.previousTake(); refresh(); };
    playButton.onClick     = [this] { deck.play(); refresh(); };
    nextButton.onClick     = [this] { deck.nextTake(); refresh(); };
    sendButton.onClick     = [this] { if (onSendRequested) onSendRequested(); };
    updateButtons();
}

void MagnetophonComponent::setSourceName (const juce::String& n)
{
    if (sourceName == n) return;
    sourceName = n;
    repaint (0, kStatusTop, getWidth(), kStatusHeight);
}

void MagnetophonComponent::resized()
{
    // "Transport" 1199:16225: Source, then the six faces 29 px apart.
    sourceButton.setBounds (17, kTransportTop + 12, 50, 20);
    juce::Button* faces[] = { &recordButton, &stopButton, &previousButton, &playButton, &nextButton, &sendButton };
    for (int i = 0; i < 6; ++i)
        faces[i]->setBounds (78 + 29 * i - 1, kTransportTop + 9 - 1, os8::kTransportFace + 2, os8::kTransportFace + 2);
}

void MagnetophonComponent::updateButtons()
{
    const auto& t = deck.tape();
    const bool rec = t.mode() == se::Tape::Mode::recording || deck.finishing();
    const bool playing = t.mode() == se::Tape::Mode::playing;

    recordButton.setToggleState (rec, juce::dontSendNotification);
    stopButton.setToggleState (! rec && ! playing && ! t.empty(), juce::dontSendNotification);
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setEnabled (! rec && ! t.empty());
    previousButton.setEnabled (! rec && ! t.empty() && t.position() > 0);
    nextButton.setEnabled (! rec && t.position() < t.length());
    sendButton.setEnabled (! rec && ! t.empty() && onSendRequested != nullptr);
    sourceButton.setEnabled (! rec);
}

size_t MagnetophonComponent::binFrames() const noexcept
{
    const double rate = deck.tape().rate() > 0.0 ? deck.tape().rate() : engine.sampleRate();
    return (size_t) std::max (1.0, std::round (rate / kPixelsPerSecond));
}

float MagnetophonComponent::peakAt (int64_t bin) const noexcept
{
    return bin >= 0 && bin < (int64_t) peaks.size() ? peaks[(size_t) bin] : 0.0f;
}

void MagnetophonComponent::updatePeaks()
{
    const auto& t = deck.tape();
    const size_t len = t.length();
    const size_t bin = binFrames();
    size_t from = 0, to = 0;
    bool any = deck.takeWrittenFrames (from, to);

    if (bin != cachedBin || len < lastLen)
    {
        // A new time scale, or a cleared tape: start over.
        cachedBin = bin;
        peaks.clear();
        from = 0; to = len; any = len > 0;
    }
    if (len == 0) { peaks.clear(); return; }
    peaks.resize ((len + bin - 1) / bin, 0.0f);
    if (! any || to <= from) return;

    constexpr size_t kRead = 4096;
    std::vector<float> l (kRead), r (kRead);
    for (size_t b = from / bin; b <= (std::min (to, len) - 1) / bin && b < peaks.size(); ++b)
    {
        float p = 0.0f;
        const size_t end = std::min (len, (b + 1) * bin);
        for (size_t f = b * bin; f < end;)
        {
            const size_t n = deck.copy (f, l.data(), r.data(), std::min (kRead, end - f));
            if (n == 0) break;
            for (size_t k = 0; k < n; ++k) p = std::max (p, std::max (std::abs (l[k]), std::abs (r[k])));
            f += n;
        }
        peaks[b] = p;
    }
}

std::pair<float, float> MagnetophonComponent::packRadii() const noexcept
{
    const auto& t = deck.tape();
    const double rate = t.rate() > 0.0 ? t.rate() : engine.sampleRate();
    const double capacity = std::max ((double) t.length(), kReelSeconds * rate);
    const double p = capacity > 0.0 ? juce::jlimit (0.0, 1.0, (double) t.position() / capacity) : 0.0;
    const double span = kFullPackRadius * kFullPackRadius - kHubRadius * kHubRadius;
    const double h2 = kHubRadius * kHubRadius;
    return { (float) std::sqrt (h2 + (1.0 - p) * span), (float) std::sqrt (h2 + p * span) };
}

void MagnetophonComponent::refresh()
{
    deck.service();   // harmless twice a tick: the processor's timer services it too

    const auto& t = deck.tape();
    const size_t pos = t.position(), len = t.length();

    // Turn the reels by what the tape moved: tape speed is constant, so the smaller
    // pack turns faster. Jumps (Next / Previous) do not spin them.
    const double rate = t.rate() > 0.0 ? t.rate() : engine.sampleRate();
    const bool moving = t.mode() != se::Tape::Mode::stopped || scrubbing || lastMode != se::Tape::Mode::stopped;
    const bool jump = ! scrubbing && std::abs ((double) pos - (double) lastPos) > rate;   // more than a second in a tick
    if (pos != lastPos && moving && ! jump)
    {
        const float travel = (float) (((double) pos - (double) lastPos) / rate) * kTapeSpeed;
        const auto [rl, rr] = packRadii();
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        angleL = std::fmod (angleL - travel / rl, twoPi);   // counter-clockwise, as the tape winds left to right
        angleR = std::fmod (angleR - travel / rr, twoPi);
    }

    const bool changed = pos != lastPos || len != lastLen || t.mode() != lastMode || t.numTakes() != lastTakes;
    updatePeaks();
    if (changed)
    {
        repaint (0, 0, getWidth(), kStatusTop + kStatusHeight);
        lastPos = pos; lastLen = len; lastMode = t.mode(); lastTakes = t.numTakes();
    }
    updateButtons();
}

void MagnetophonComponent::paint (juce::Graphics& g)
{
    g.fillAll (gray000);
    paintDeck (g);
    paintStrip (g);
    paintStatus (g);
    paintTransport (g);
}

void MagnetophonComponent::paintDeck (juce::Graphics& g) const
{
    const auto [rl, rr] = packRadii();

    // The tape (2 px): left pack → under the rollers and the head → right pack. Its
    // centre line runs 1 px outside each circle it wraps.
    const auto left  = tapeRun (kLeftReel, rl + 1.0f, kLeftRoller, kRollerRadius + 1.0f);
    const auto mid   = tapeRun (kLeftRoller, kRollerRadius + 1.0f, kRightRoller, kRollerRadius + 1.0f);
    const auto right = tapeRun (kRightRoller, kRollerRadius + 1.0f, kRightReel, rr + 1.0f);
    g.setColour (black);
    for (const auto& run : { left, mid, right })
        g.drawLine ({ run.first, run.second }, 2.0f);

    drawReel (g, kLeftReel, rl, angleL);
    drawReel (g, kRightReel, rr, angleR);
    drawRoller (g, kLeftRoller);
    drawRoller (g, kRightRoller);
    drawHead (g);
}

void MagnetophonComponent::paintStrip (juce::Graphics& g) const
{
    const auto& t = deck.tape();
    const int w = getWidth();
    const int top = kStripTop, centre = kStripTop + 25;
    constexpr int kAmp = 23;

    g.setColour (white);
    g.fillRect (0, top, w, kStripHeight);
    g.setColour (black);
    g.fillRect (0, top, w, 1);
    g.fillRect (0, top + kStripHeight - 1, w, 1);
    g.fillRect (0, centre, w, 1);

    const size_t bin = std::max<size_t> (1, cachedBin);
    const int64_t posBin = (int64_t) (t.position() / bin);
    const int recTake = t.recordingTake();
    const int64_t recFromBin = recTake >= 0 ? (int64_t) (t.takeStart (recTake) / bin) : INT64_MAX;

    for (int x = 0; x < w; ++x)
    {
        const int64_t b = posBin + (x - kPlayheadX);
        const float p = peakAt (b);
        const int h = (int) std::round (std::min (1.0f, p) * (float) kAmp);
        if (h < 1) continue;
        g.setColour (b >= recFromBin ? transportRed : black);
        g.fillRect (x, centre - h, 1, 2 * h + 1);
    }

    // Take boundaries and labels. A label sticks to the left edge while its take
    // runs off it, and gives way to the next take's.
    const auto xOf = [&] (size_t frame) { return kPlayheadX + (int) ((int64_t) (frame / bin) - posBin); };
    for (int i = 0; i < t.numTakes(); ++i)
    {
        const int x = xOf (t.takeStart (i));
        const int nextX = i + 1 < t.numTakes() ? xOf (t.takeStart (i + 1)) : xOf (t.length());
        if (nextX < 0 || x >= w) continue;
        if (x >= 0)
        {
            g.setColour (gray500);
            g.fillRect (x, top + 1, 1, kStripHeight - 2);
        }
        const juce::String label = "Take " + juce::String (i + 1);
        const int lw = os8::widthOf (label, os8::TextStyle::xSmallLabel);
        const int lx = std::min (std::max (x, 0) + 4, nextX - lw - 4);
        if (lx + lw < 0 || lx < x) continue;   // no room left beside the next take
        g.setColour (black);
        os8::drawText (g, label, os8::TextStyle::xSmallLabel, { (float) lx, (float) top + 3.0f, (float) lw + 2.0f, 9.0f });
    }

    g.setColour (black);
    g.fillRect (kPlayheadX, top + 1, 2, kStripHeight - 2);
}

void MagnetophonComponent::paintStatus (juce::Graphics& g) const
{
    const auto& t = deck.tape();
    const int w = getWidth();
    g.setColour (kStatusNavy);
    g.fillRect (0, kStatusTop, w, kStatusHeight);
    g.setColour (black);
    g.fillRect (0, kStatusTop + kStatusHeight, w, 1);   // the transport's outside stroke

    const double rate = t.rate() > 0.0 ? t.rate() : engine.sampleRate();
    const juce::String parts[] = { rateText (rate), "16bits", timeText (t.position(), rate) };
    const float y = (float) kStatusTop + 3.0f;
    g.setColour (white);
    int x = w - 4;
    for (int i = 2; i >= 0; --i)
    {
        const int pw = os8::widthOf (parts[i], os8::TextStyle::smallLabel);
        x -= pw;
        os8::drawText (g, parts[i], os8::TextStyle::smallLabel, { (float) x, y, (float) pw + 2.0f, 10.0f });
        x -= 4;
    }

    // "From: <source>", shortened to fit before the readouts.
    juce::String from = "From: " + sourceName;
    const int room = x - 6 - 3;
    while (from.length() > 7 && os8::widthOf (from, os8::TextStyle::smallLabel) > room)
        from = from.dropLastCharacters (2) + juce::String::charToString (0x2026);
    os8::drawText (g, from, os8::TextStyle::smallLabel, { 3.0f, y, (float) room, 10.0f });
}

void MagnetophonComponent::paintTransport (juce::Graphics& g) const
{
    // "Transport" 1199:16225: #dddddd, a 2 px white bevel top and left, grey bottom and right.
    const juce::Rectangle<int> r { 0, kTransportTop, getWidth(), kTransportHeight };
    g.setColour (gray100);
    g.fillRect (r);
    g.setColour (white);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 2);
    g.fillRect (r.getX(), r.getY(), 2, r.getHeight());
    g.setColour (kBevelDark);
    g.fillRect (r.getX() + 2, r.getBottom() - 2, r.getWidth() - 2, 2);
    g.fillRect (r.getRight() - 2, r.getY() + 2, 2, r.getHeight() - 2);
}

void MagnetophonComponent::mouseDown (const juce::MouseEvent& e)
{
    // Wind the tape by hand: grab the strip and drag it.
    const auto& t = deck.tape();
    scrubbing = e.y >= kStripTop && e.y < kStripTop + kStripHeight
             && t.mode() != se::Tape::Mode::recording && ! deck.finishing() && ! t.empty();
    scrubFrom = t.position();
    if (scrubbing) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void MagnetophonComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! scrubbing) return;
    const int64_t to = (int64_t) scrubFrom - (int64_t) e.getDistanceFromDragStartX() * (int64_t) std::max<size_t> (1, cachedBin);
    deck.seek ((size_t) std::max<int64_t> (0, to));
    refresh();
}

void MagnetophonComponent::mouseUp (const juce::MouseEvent&)
{
    scrubbing = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}
