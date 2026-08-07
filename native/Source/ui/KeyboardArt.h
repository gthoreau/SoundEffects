// KeyboardArt — the Keyboard module's own artwork, recovered and redrawn. [RES]
//
// Split out from the widget that uses it because the View palette needs the icon
// too, and a palette of view controls has no business pulling in the engine.
// Nothing here knows about documents, transports or windows: it is the picture.
//
// ## Where it comes from
//
// The shipping module's resource fork survives in
// `SoundEffects 0.9.2 ƒ/Effects Folder/Keyboard`:
//
//   PICT 128   the keyboard — 449 × 39, one bit deep
//   ICN# 128   the piano-keys icon, 32 × 32 with a 20 × 12 band of art in it
//
// The PICT was decoded and MEASURED rather than embedded: a one-bit picture
// stretched to whatever a modern display calls a point is a smeared keyboard,
// whereas the measurements below reproduce it pixel for pixel and stay crisp at
// any integer scale. Every rule in the original is exactly one pixel, and every
// number here was read off the decoded bitmap.

#pragma once

#include <juce_graphics/juce_graphics.h>

/**
 * The recovered geometry of PICT 128, in the picture's own pixels.
 *
 * The octave pattern is byte-identical across all eight octaves in the original,
 * which is why one set of offsets describes the whole keyboard.
 */
namespace kbd {

/** White keys are on an 8px pitch: a 7px body and the 1px rule to its right. */
inline constexpr int kWhitePitch = 8;
/** 56 white keys — exactly eight octaves. 1 + 56 × 8 = 449, the picture's width. */
inline constexpr int kNumWhite = 56;
inline constexpr int kNumOctaves = kNumWhite / 7;
/** One octave is 56px wide, and the picture is eight copies of it. */
inline constexpr int kOctaveW = 7 * kWhitePitch;

inline constexpr int kPicW = 1 + kNumWhite * kWhitePitch;   // 449
inline constexpr int kPicH = 39;

/** A white key runs the full height between the frame rules; a black key stops at
    y = 20, which is where the octave pattern gives way to plain separators. */
inline constexpr int kWhiteH = kPicH - 2;   // 37
inline constexpr int kWhiteW = kWhitePitch - 1;
inline constexpr int kBlackH = 20;
inline constexpr int kBlackW = 5;

/** Left edge of each black key within its 56px octave, indexed by semitone; −1
    for the seven white degrees. Each is centred on the rule between its
    neighbours: 6..10 straddles the rule at 8, 14..18 the one at 16, and so on. */
inline constexpr int kBlackX[12] = { -1, 6, -1, 14, -1, -1, 30, -1, 38, -1, 46, -1 };

/** Which semitone each of C D E F G A B is, within the octave. */
inline constexpr int kWhiteDegree[7] = { 0, 2, 4, 5, 7, 9, 11 };

/**
 * The MIDI note the leftmost key plays.
 *
 * The original marks the document's base note with a dot under the keyboard, and
 * in the recovered screenshot that dot sits under the 22nd white key while the
 * document carries the default base note of 60. 60 − 36 semitones = 24, so the
 * eight octaves run C1..B8 and middle C sits three octaves up. Confirmed in the
 * running app: the dotted key reads C4.
 */
inline constexpr int kLowNote = 24;
inline constexpr int kHighNote = kLowNote + kNumOctaves * 12 - 1;

/** Where `note`'s key sits, in picture pixels. Empty if it is off the keyboard. */
juce::Rectangle<int> keyBounds (int note) noexcept;

/** Which note is under `p` (picture pixels), or −1 outside the picture. Black keys
    win: they are drawn over the white ones, so they are hit-tested over them too. */
int noteAt (juce::Point<int> p) noexcept;

/** Draw the keyboard at 1:1 with its top-left at `origin`. `held` is inverted and
    `baseNote` gets the dot beneath it; −1 for neither. */
void draw (juce::Graphics&, juce::Point<int> origin, int held, int baseNote);

/** "C4", "F#3" — the note a key plays. */
juce::String noteName (int note);

// --- ICN# 128 ---------------------------------------------------------------

/** The icon's art, cropped to the band it actually covers. */
inline constexpr int kIconW = 20, kIconH = 12;

/** Draw ICN# 128 at 1:1 with its top-left at `origin`, in the current colour.
    Used by the View palette's show/hide toggle. */
void drawIcon (juce::Graphics&, juce::Point<int> origin);

} // namespace kbd
