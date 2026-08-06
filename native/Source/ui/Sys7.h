// Sys7 — the shared faux-System 7 vocabulary the side panels are drawn in.
//
// Everything is hand-painted in the same idiom as the Windows on the canvas: flat
// grey face, 1px black frames, no gradients, inverted fill for the "on" state, and
// BORDER WEIGHT rather than colour to carry hover. Extracted from MixerComponent
// when the effects panel needed the same buttons and the same scrollbar — two
// panels drawn from one vocabulary, not two that merely resemble each other.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace sys7 {

// The palette, shared with the canvas Windows.
inline const juce::Colour kFace  { 0xffbfbfbf };
inline const juce::Colour kInk   { 0xff000000 };
inline const juce::Colour kWhite { 0xffffffff };
inline const juce::Colour kDim   { 0xff7a7a7a };   // disabled / secondary text

// The two side panels' own ground. Distinct from kFace, which is the CHROME grey a
// control is made of — a button, a scrollbar thumb, a strip with no tint. Keeping
// the ground apart from the things standing on it is what lets those things read as
// controls now that the panels are near-white.
inline const juce::Colour kPanel { 0xfff4f4f4 };

/** Frame weight carries state everywhere in this UI: plain, hovered, active. */
inline constexpr int kBorderIdle = 1;
inline constexpr int kBorderHover = 2;

// The twelve pastels a Window is coated in, one per hue clock position. They live
// here rather than on the canvas because the mixer wears them too: a channel strip
// carries its Window's tint, so the eye pairs strip to Window without reading a
// single name.
inline constexpr int kNumTints = 12;
juce::Colour tintFromPalette (int index) noexcept;

} // namespace sys7

/** A System 7 push button: rounded 1px black outline, inverted while held. */
class Sys7Button : public juce::Button
{
public:
    explicit Sys7Button (const juce::String& text) : juce::Button (text) { setButtonText (text); }

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;
};

/** Only what the panels actually borrow from JUCE: the list's scrollbar. */
class Sys7LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
};
