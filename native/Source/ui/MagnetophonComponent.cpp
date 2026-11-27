#include "MagnetophonComponent.h"

#include <cmath>

namespace {

using namespace os8::colour;

// --- the deck, as drawn in "Reels" 1307:6692 (content coordinates) ------------
const juce::Point<float> kLeftReel  { 65.0f, 60.0f };
const juce::Point<float> kRightReel { 203.0f, 60.0f };
const juce::Point<float> kLeftRoller  { 93.0f, 123.5f };
const juce::Point<float> kRightRoller { 175.5f, 123.5f };
/** The tape packs ("Ellipse 12"): the reels are drawn as designed and only turn. */
constexpr float kLeftPack = 46.2f, kRightPack = 33.0f;
const juce::Rectangle<float> kHead { 109.0f, 115.0f, 50.0f, 22.0f };   // "heads" 1307:6705

/** How fast the tape crosses the deck, in pixels per second of audio: it sets how
    quickly the reels turn (the smaller pack the faster). */
constexpr float kTapeSpeed = 100.0f;

const juce::Colour kStatusNavy { 0xff17212b };
const juce::Colour kBevelDark  { 0xff808080 };
const juce::Colour kHeadShade  { 0xffd0d0d0 };

/** The tape from each reel, round its pinch roller and under the head ("tape-left"
    1307:6693, "tape-right" 1307:6811), as Figma exports them. */
const juce::Path& tapeRuns()
{
    static const juce::Path p = juce::Drawable::parseSVGPath (
        "M38.5 97L87.5043 126.974C89.1364 127.972 91.0124 128.5 92.9255 128.5C94.9321 128.5 96.8959 127.919 98.5794 126.827L114.5 116.5"
        "M227 81.5L180.99 126.087C179.393 127.635 177.257 128.5 175.033 128.5C173.38 128.5 171.762 128.021 170.375 127.122L154 116.5");
    return p;
}

/** The reel's face ("Subtract" 1307:6699): a disc with three windows, as Figma
    exports it, moved so that its centre is (0, 0). */
const juce::Path& reelFace()
{
    static const juce::Path p = []
    {
        auto path = juce::Drawable::parseSVGPath (
            "M65 7.2C94.1608 7.2 117.8 30.8394 117.8 60C117.8 89.1606 94.1608 112.8 65 112.8C35.8396 112.8 12.2 89.1606 12.2 60C12.2 30.8394 35.8396 7.2 65 7.2Z"
            "M72.8667 79.5927C70.3703 80.6367 67.6344 81.2147 64.7649 81.2147C62.2926 81.2147 59.9191 80.7848 57.7127 79.9987L48.324 102.674"
            "C48.3485 102.703 51.7526 106.596 65.3751 106.596C78.9652 106.596 82.3837 102.721 82.424 102.674L72.8667 79.5927Z"
            "M78.5622 43.4721C82.6126 46.9823 85.3383 52.0093 85.8787 57.6829L109.791 54.5322C109.798 54.5126 111.485 49.6218 104.663 37.8056"
            "C97.8389 25.9867 92.7591 25.0044 92.7408 25.0009L78.5622 43.4721Z"
            "M37.9256 25.0009C37.8649 25.0128 32.7998 26.0376 26.005 37.8066C19.2077 49.58 20.8568 54.4774 20.8756 54.5322L43.663 57.5347"
            "C44.2688 51.6041 47.2643 46.3895 51.6595 42.8941L37.9256 25.0009Z");
        path.setUsingNonZeroWinding (false);
        path.applyTransform (juce::AffineTransform::translation (-65.0f, -60.0f));
        return path;
    }();
    return p;
}

/** A reel ("Reel" 1307:6790): the tape pack, the face with its 2 px outside stroke
    and three windows (see-through: what lies under the reel shows in them, the tape
    included) and the hub, painted as one picture so that it casts the group's hard
    shadow, 25 % black 2 px down, as one silhouette. */
void drawReel (juce::Graphics& g, juce::Point<float> c, float pack, float angle)
{
    constexpr int kBox = 112;   // the face, its outside stroke and a pixel of air
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int px = (int) std::ceil ((float) kBox * scale);
    juce::Image img (juce::Image::ARGB, px, px, true);
    {
        juce::Graphics ig (img);
        ig.addTransform (juce::AffineTransform::scale (scale));
        const juce::Point<float> o ((float) kBox / 2.0f, (float) kBox / 2.0f);
        ig.setColour (black);
        ig.fillEllipse (juce::Rectangle<float> (2 * pack, 2 * pack).withCentre (o));
        const auto t = juce::AffineTransform::rotation (angle).translated (o);
        // An outside stroke: twice the width, then the face over its inner half.
        ig.strokePath (reelFace(), juce::PathStrokeType (4.0f), t);
        ig.setColour (gray100);
        ig.fillPath (reelFace(), t);
        ig.setColour (black);
        ig.drawEllipse (juce::Rectangle<float> (27.09f, 27.09f).withCentre (o), 3.0f);   // "Ellipse 10"
    }
    const auto dest = juce::Rectangle<float> ((float) kBox, (float) kBox).withCentre (c);
    g.setColour (black.withAlpha (0.25f));
    g.drawImage (img, dest.translated (0.0f, 2.0f), juce::RectanglePlacement::stretchToFit, true);
    g.setColour (black);
    g.drawImage (img, dest, juce::RectanglePlacement::stretchToFit, false);
}

/** A pinch roller ("pinch-roller-left" 1307:6695). */
void drawRoller (juce::Graphics& g, juce::Point<float> c)
{
    const auto r = juce::Rectangle<float> (11.0f, 11.0f).withCentre (c);
    g.setColour (gray400);
    g.fillEllipse (r);
    g.setColour (black);
    g.drawEllipse (r, 1.0f);
}

/** The heads' housing: round top corners, a 3 px bevel and a 2 px inside stroke. (Its
    shadow falls under the strip.) */
void drawHead (juce::Graphics& g)
{
    const auto r = kHead;
    juce::Path shape;
    shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 8.0f, 8.0f, true, true, false, false);

    g.setColour (gray000);
    g.fillPath (shape);

    // The bevel: grey 3 px in from the bottom and right, light from the top and left.
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (shape);
        const auto band = [&] (float d, juce::Colour c)
        {
            juce::Path inner = shape;
            inner.applyTransform (juce::AffineTransform::translation (d, d));
            juce::Path p; p.addRectangle (r.expanded (4.0f)); p.addPath (inner); p.setUsingNonZeroWinding (false);
            g.setColour (c);
            g.fillPath (p);
        };
        band (-3.0f, kHeadShade);
        band ( 3.0f, juce::Colour (0xedfffdfd));
    }
    g.setColour (black);
    juce::Path edge;
    edge.addRoundedRectangle (r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, r.getHeight() - 2.0f, 7.0f, 7.0f, true, true, false, false);
    g.strokePath (edge, juce::PathStrokeType (2.0f));
}

juce::String timeText (size_t frames, double rate)
{
    const double s = rate > 0.0 ? (double) frames / rate : 0.0;
    const int cs = (int) std::floor (s * 100.0);
    return juce::String (cs / 6000).paddedLeft ('0', 2) + ":" + juce::String ((cs / 100) % 60).paddedLeft ('0', 2)
         + ":" + juce::String (cs % 100).paddedLeft ('0', 2);
}

/** Chicago's digits are not all as wide (the 1 is narrow), so a running counter
    would shimmy: every digit is drawn centred in a cell as wide as a 0. The cells
    are fractions of a pixel wide, as the advances in a run of text are. */
float cellOf (juce::juce_wchar ch)
{
    const auto of = juce::CharacterFunctions::isDigit (ch) ? juce::juce_wchar ('0') : ch;
    return (float) os8::widthOf (juce::String::repeatedString (juce::String::charToString (of), 20),
                                 os8::TextStyle::smallLabel) / 20.0f;
}

float monoWidthOf (const juce::String& s)
{
    float w = 0.0f;
    for (auto ch : s) w += cellOf (ch);
    return w;
}

void drawMono (juce::Graphics& g, const juce::String& s, float x, float y)
{
    for (auto ch : s)
    {
        const auto one = juce::String::charToString (ch);
        const auto cw = (float) os8::widthOf (one, os8::TextStyle::smallLabel);
        const float cell = cellOf (ch);
        const float inset = ch == '1' ? (cell - cw) * 0.5f : 0.0f;
        os8::drawText (g, one, os8::TextStyle::smallLabel, { x + inset, y, cw + 2.0f, 10.0f });
        x += cell;
    }
}

juce::String rateText (double rate)
{
    const double k = rate / 1000.0;
    return (std::abs (k - std::round (k)) < 0.05 ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + "khz";
}

/** A take's label in its strip, ringed in white so that it reads over the waveform. */
void drawTakeLabel (juce::Graphics& g, const juce::String& s, int x, int y, juce::Colour colour)
{
    const auto at = [&] (int dx, int dy)
    {
        os8::drawText (g, s, os8::TextStyle::xSmallLabel,
                       { (float) (x + dx), (float) (y + dy), (float) os8::widthOf (s, os8::TextStyle::xSmallLabel) + 2.0f, 9.0f });
    };
    g.setColour (white);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx != 0 || dy != 0) at (dx, dy);
    g.setColour (colour);
    at (0, 0);
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
    // "Transport" 1307:6717: Source, then the six faces 29 px apart.
    sourceButton.setBounds (9, kTransportTop + 12, 50, 20);
    juce::Button* faces[] = { &recordButton, &stopButton, &previousButton, &playButton, &nextButton, &sendButton };
    for (int i = 0; i < 6; ++i)
        faces[i]->setBounds (69 + 29 * i, kTransportTop + 9, os8::kTransportFace + 2, os8::kTransportFace + 2);
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

int MagnetophonComponent::xOfFrame (size_t frame) const noexcept
{
    const size_t bin = std::max<size_t> (1, cachedBin);
    return kPlayheadX + (int) ((int64_t) (frame / bin) - (int64_t) (deck.tape().position() / bin));
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

void MagnetophonComponent::refresh()
{
    deck.service();   // harmless twice a tick: the processor's timer services it too

    const auto& t = deck.tape();
    const size_t pos = t.position(), len = t.length();

    // Turn the reels by what the tape moved: tape speed is constant, so the reel with
    // the smaller pack turns faster. Jumps (Next / Previous) do not spin them.
    const double rate = t.rate() > 0.0 ? t.rate() : engine.sampleRate();
    const bool moving = t.mode() != se::Tape::Mode::stopped || scrubbing || lastMode != se::Tape::Mode::stopped;
    const bool jump = ! scrubbing && std::abs ((double) pos - (double) lastPos) > rate;   // more than a second in a tick
    if (pos != lastPos && moving && ! jump)
    {
        const float travel = (float) (((double) pos - (double) lastPos) / rate) * kTapeSpeed;
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        angleL = std::fmod (angleL - travel / kLeftPack, twoPi);   // counter-clockwise, as the tape winds left to right
        angleR = std::fmod (angleR - travel / kRightPack, twoPi);
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
    g.setColour (black);
    g.strokePath (tapeRuns(), juce::PathStrokeType (2.0f));
    drawRoller (g, kLeftRoller);
    drawRoller (g, kRightRoller);
    drawHead (g);
    drawReel (g, kLeftReel, kLeftPack, angleL);
    drawReel (g, kRightReel, kRightPack, angleR);
}

void MagnetophonComponent::paintStrip (juce::Graphics& g) const
{
    // "tape" 1307:6706: what has passed the head in black, what is still to come grey.
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
        g.setColour (x >= kPlayheadX ? gray300 : b >= recFromBin ? transportRed : black);
        g.fillRect (x, centre - h, 1, 2 * h + 1);
    }

    // Take boundaries and labels. A label sticks to the left edge while its take
    // runs off it, and shortens ("Take 2", "T..2", "2") to what its take has room for.
    for (int i = 0; i < t.numTakes(); ++i)
    {
        const int x = xOfFrame (t.takeStart (i));
        // The last take's label may run on past the end of the tape (blank there).
        const bool last = i + 1 >= t.numTakes();
        const int nextX = last ? w + 1000 : xOfFrame (t.takeStart (i + 1));
        if ((last ? xOfFrame (t.length()) : nextX) < 0 || x >= w) continue;
        if (x > 0)
        {
            g.setColour (black);
            g.fillRect (x, top + 1, 1, kStripHeight - 2);
        }

        const int left = std::max (x, 0), room = nextX - left;
        const juce::String number (i + 1);
        const juce::String forms[] = { "Take " + number, "T.." + number, number };
        for (const auto& label : forms)
        {
            const int lw = os8::widthOf (label, os8::TextStyle::xSmallLabel);
            if (lw + 8 > room) continue;
            drawTakeLabel (g, label, left + 4, top + 3, t.takeStart (i) < t.position() ? black : gray500);
            break;
        }
    }

    g.setColour (black);
    g.fillRect (kPlayheadX - 1, top + 1, 2, kStripHeight - 2);
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
    // "Info" 1307:6711: the source, the counter centred on the playhead, then the
    // rate and depth at the right.
    const float y = (float) kStatusTop + 3.0f;
    g.setColour (white);
    int x = w - 4;
    const juce::String parts[] = { rateText (rate), "16bits" };
    for (int i = 1; i >= 0; --i)
    {
        const int pw = os8::widthOf (parts[i], os8::TextStyle::smallLabel);
        x -= pw;
        os8::drawText (g, parts[i], os8::TextStyle::smallLabel, { (float) x, y, (float) pw + 2.0f, 10.0f });
        x -= 4;
    }
    const auto counter = timeText (t.position(), rate);   // it runs: fixed-width digits
    const int counterX = juce::roundToInt ((float) kPlayheadX - monoWidthOf (counter) * 0.5f);
    drawMono (g, counter, (float) counterX, y);

    // "From: <source>", shortened to fit before the counter.
    juce::String from = "From: " + sourceName;
    const int room = counterX - 5 - 4;
    while (from.length() > 7 && os8::widthOf (from, os8::TextStyle::smallLabel) > room)
        from = from.dropLastCharacters (2) + juce::String::charToString (0x2026);
    os8::drawText (g, from, os8::TextStyle::smallLabel, { 4.0f, y, (float) room + 2.0f, 10.0f });
}

void MagnetophonComponent::paintTransport (juce::Graphics& g) const
{
    // "Transport" 1307:6717: #dddddd, a 2 px white bevel top and left, grey bottom and right.
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
    // Wind the tape by hand, heard: grab the strip and drag it (a scrub). A playing
    // tape stops while held and plays on from where it is let go.
    scrubbing = e.y >= kStripTop && e.y < kStripTop + kStripHeight && deck.scrubBegin();
    scrubFrom = deck.tape().position();
    if (scrubbing) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void MagnetophonComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! scrubbing) return;
    const int64_t to = (int64_t) scrubFrom - (int64_t) e.getDistanceFromDragStartX() * (int64_t) std::max<size_t> (1, cachedBin);
    deck.scrubTo ((size_t) std::max<int64_t> (0, to));
    refresh();
}

void MagnetophonComponent::mouseUp (const juce::MouseEvent&)
{
    if (scrubbing) deck.scrubEnd();
    scrubbing = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    refresh();
}
