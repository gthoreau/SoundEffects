#include "KeyboardArt.h"

using juce::Graphics;
using juce::Point;
using juce::Rectangle;

namespace {

/** Where `degree` sits among C D E F G A B, or -1 for the five black ones. */
int whiteIndexOf (int degree) noexcept
{
    for (int i = 0; i < 7; ++i)
        if (kbd::kWhiteDegree[i] == degree) return i;
    return -1;
}

/**
 * ICN# 128's art, cropped to the 20 x 12 it actually covers.
 *
 * The module ships a 32 x 32 icon that is empty but for this band of piano keys.
 * Drawn at 1:1 rather than scaled into a nominal 16 x 16 box, which would only
 * blur a picture whose whole character is that every mark in it is one pixel.
 */
const char* const kIcon[kbd::kIconH] = {
    "###################.",
    "##.##.#..##.##.#..##",
    "##.##.#..##.##.#..##",
    "##.##.#..##.##.#..##",
    "##.##.#..##.##.#..##",
    "##.##.#..##.##.#..##",
    "#..#..#..#..#..#..##",
    "#..#..#..#..#..#..##",
    "#..#..#..#..#..#..##",
    "#..#..#..#..#..#..##",
    "####################",
    ".###################",
};

// The palette these are drawn in. Kept local so this file needs no Sys7.h: the
// artwork is one bit deep, and one bit deep is black and white.
const juce::Colour kInk   { 0xff000000 };
const juce::Colour kWhite { 0xffffffff };

} // namespace

Rectangle<int> kbd::keyBounds (int note) noexcept
{
    if (note < kLowNote || note > kHighNote) return {};

    const int semitone = note - kLowNote;
    const int octave = semitone / 12;
    const int degree = semitone % 12;

    if (kBlackX[degree] >= 0)
        return { octave * kOctaveW + kBlackX[degree], 1, kBlackW, kBlackH };

    const int white = octave * 7 + whiteIndexOf (degree);
    return { white * kWhitePitch + 1, 1, kWhiteW, kWhiteH };
}

int kbd::noteAt (Point<int> p) noexcept
{
    if (p.x < 0 || p.x >= kPicW || p.y < 0 || p.y >= kPicH) return -1;

    const int octave = juce::jlimit (0, kNumOctaves - 1, p.x / kOctaveW);
    const int within = p.x - octave * kOctaveW;

    // Black keys first: they are drawn over the white ones, and a click in the
    // top band belongs to whichever black key covers it. None of them straddles
    // an octave boundary (the last ends at x = 51 of 56), so the octave the point
    // falls in is the only one worth asking.
    if (p.y <= kBlackH)
        for (int degree = 0; degree < 12; ++degree)
            if (kBlackX[degree] >= 0
                && within >= kBlackX[degree] && within < kBlackX[degree] + kBlackW)
                return kLowNote + octave * 12 + degree;

    const int white = juce::jlimit (0, kNumWhite - 1, p.x / kWhitePitch);
    return kLowNote + (white / 7) * 12 + kWhiteDegree[white % 7];
}

void kbd::draw (Graphics& g, Point<int> o, int held, int baseNote)
{
    const Rectangle<int> all (o.x, o.y, kPicW, kPicH);

    g.setColour (kWhite);
    g.fillRect (all);

    // The frame, and the rule between every pair of white keys. In the picture
    // these are one and the same: 57 vertical lines on an 8px pitch, the first
    // and last of which are the left and right edges.
    g.setColour (kInk);
    for (int x = 0; x <= kNumWhite * kWhitePitch; x += kWhitePitch)
        g.fillRect (o.x + x, o.y, 1, kPicH);
    g.fillRect (all.getX(), all.getY(), all.getWidth(), 1);
    g.fillRect (all.getX(), all.getBottom() - 1, all.getWidth(), 1);

    for (int octave = 0; octave < kNumOctaves; ++octave)
        for (int degree = 0; degree < 12; ++degree)
            if (kBlackX[degree] >= 0)
                g.fillRect (o.x + octave * kOctaveW + kBlackX[degree], o.y + 1, kBlackW, kBlackH);

    // The held key, inverted — System 7's one and only way of saying "this is
    // down". Inverting a WHITE key would also swallow the black keys overlapping
    // it, so those are painted back in white: that is what an XOR of the region
    // does, and it keeps the neighbours legible while the key is struck.
    if (const auto key = keyBounds (held); ! key.isEmpty())
    {
        const auto r = key + o;

        if (kBlackX[(held - kLowNote) % 12] >= 0)
        {
            // An inverted black key is a white one, which on its own would read as
            // a gap between its neighbours; the outline keeps its shape.
            g.setColour (kWhite);
            g.fillRect (r);
            g.setColour (kInk);
            g.drawRect (r, 1);
        }
        else
        {
            g.setColour (kInk);
            g.fillRect (r);

            g.setColour (kWhite);
            for (int octave = 0; octave < kNumOctaves; ++octave)
                for (int degree = 0; degree < 12; ++degree)
                {
                    if (kBlackX[degree] < 0) continue;
                    const Rectangle<int> black (o.x + octave * kOctaveW + kBlackX[degree],
                                                o.y + 1, kBlackW, kBlackH);
                    const auto overlap = black.getIntersection (r);
                    if (! overlap.isEmpty()) g.fillRect (overlap);
                }
        }
    }

    // The dot under the document's base note — the keyboard's only readout in the
    // original, and the only thing on it that moves with the document.
    if (const auto key = keyBounds (baseNote); ! key.isEmpty())
    {
        g.setColour (kInk);
        g.fillEllipse ((float) (o.x + key.getCentreX() - 1), (float) (o.y + kPicH + 2), 3.0f, 3.0f);
    }
}

juce::String kbd::noteName (int note)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };
    // Middle C is 60 and is called C4, which puts octave 0 at note 12.
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}

void kbd::drawIcon (Graphics& g, Point<int> origin)
{
    for (int y = 0; y < kIconH; ++y)
        for (int x = 0; x < kIconW; ++x)
            if (kIcon[y][x] == '#')
                g.fillRect (origin.x + x, origin.y + y, 1, 1);
}
