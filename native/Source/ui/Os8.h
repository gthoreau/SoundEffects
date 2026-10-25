// Os8 — the Mac OS 8 vocabulary of SoundEffects 0.4.0, drawn from the Figma design
// system (file QmG2Chvizn1QXGPwQVBgnn, page "Design System").
//
// This is the successor to Sys7. The two live side by side while the panels move
// over one at a time; once nothing includes Sys7.h any more, it goes. Every value
// here is a value read off a Figma style or component, so when the design changes,
// this file is where the change lands — and nothing outside it should hard-code a
// colour or a font that has a name in Figma.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace os8 {

// --- type --------------------------------------------------------------------
//
// The five Figma text styles. All are Classic Chicago — a pixel-exact rebuild of
// the system font, embedded in the binary so the app never depends on what the
// user has installed — at a 98% line height.

enum class TextStyle
{
    largeLabel,    // 14 — "Large Label - Chicago"
    headline,      // 12 — "Headlines - Chicago": buttons, titles, the standard size
    menuItem,      // 11 — "App Menu items - Chicago"
    smallLabel,    // 10 — "Small Label - Chicago"
    xSmallLabel    //  9 — "X-small label - Chicago"
};

/** The Figma font size of a style, in points (Figma's px at 1x). */
float sizeOf (TextStyle) noexcept;

/** The embedded Classic Chicago typeface, created once on first use. */
juce::Typeface::Ptr chicago();

/** A ready-to-use font for one of the Figma text styles. */
juce::Font font (TextStyle);

/** The line height Figma gives every style: 98% of its size. */
float lineHeightOf (TextStyle) noexcept;

/** Where Figma puts the baseline, measured down from the top of a text box.

    Figma centres the font's natural content box (ascent + descent, 1.25 em for this
    face) inside the line box (0.98 em). The line box is the smaller of the two, so
    the half-leading is negative and the baseline lands 0.865 em down — not at the
    ascent. Placing text by any other rule puts every label a pixel or two off its
    Figma position, which is exactly the error "pixel-perfect" rules out. */
float baselineOf (TextStyle) noexcept;

/** Draw single-line text the way Figma lays out a text node whose box (top-left and
    width, as Figma reports them) is `box`. Horizontal placement follows `just`;
    vertical placement is always Figma's top-aligned line box. */
void drawText (juce::Graphics&, const juce::String&, TextStyle,
               juce::Rectangle<float> box, juce::Justification just = juce::Justification::left);

/** The width Figma would give an auto-width text node holding `text`: the advance
    width, rounded up to whole pixels (checked against 15 strings across all five
    styles — every one matched). */
int widthOf (const juce::String& text, TextStyle);

// --- colours: the System/* styles -------------------------------------------

namespace colour {
inline const juce::Colour selectedTextField { 0xffccccff };
inline const juce::Colour selectedMenuItem  { 0xff333399 };
inline const juce::Colour transportRed      { 0xfffd0000 };
inline const juce::Colour white             { 0xffffffff };
inline const juce::Colour gray000           { 0xffeeeeee };
inline const juce::Colour gray100           { 0xffdddddd };
inline const juce::Colour gray200           { 0xffd9d9d9 };
inline const juce::Colour gray300           { 0xffc0c0c0 };
inline const juce::Colour gray400           { 0xff9d9d9d };
inline const juce::Colour gray500           { 0xff666666 };
inline const juce::Colour gray600           { 0xff333333 };
inline const juce::Colour black             { 0xff000000 };
} // namespace colour

} // namespace os8
