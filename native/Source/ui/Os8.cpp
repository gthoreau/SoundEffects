#include "Os8.h"

#include "BinaryData.h"

#include <cmath>

namespace os8 {

float sizeOf (TextStyle s) noexcept
{
    switch (s)
    {
        case TextStyle::largeLabel:  return 14.0f;
        case TextStyle::headline:    return 12.0f;
        case TextStyle::menuItem:    return 11.0f;
        case TextStyle::smallLabel:  return 10.0f;
        case TextStyle::xSmallLabel: return 9.0f;
    }
    return 12.0f;
}

juce::Typeface::Ptr chicago()
{
    // Made once and kept: a Typeface built from memory is not cheap, and every
    // label in the app asks for it.
    static juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::ClassicChicago_ttf, (size_t) BinaryData::ClassicChicago_ttfSize);
    return typeface;
}

juce::Font font (TextStyle s)
{
    // Point height, not JUCE height: Figma's font size is the em size, and JUCE's
    // plain height is ascent + descent — 1.25 em for this face (1536 + 384 units on
    // a 1536 em). Using withHeight would draw every label 20% small.
    return juce::Font (juce::FontOptions (chicago()).withPointHeight (sizeOf (s)));
}

namespace {
constexpr float kLineHeight = 0.98f;   // every Figma text style
constexpr float kContentEm  = 1.25f;   // (ascent 1536 + descent 384) / 1536 em
}

float lineHeightOf (TextStyle s) noexcept
{
    return sizeOf (s) * kLineHeight;
}

float baselineOf (TextStyle s) noexcept
{
    const float size = sizeOf (s);
    return (lineHeightOf (s) - size * kContentEm) * 0.5f + size;
}

int widthOf (const juce::String& text, TextStyle s)
{
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (s), text) - 0.01f);
}

void drawText (juce::Graphics& g, const juce::String& text, TextStyle s,
               juce::Rectangle<float> box, juce::Justification just)
{
    const auto f = font (s);
    const float w = juce::GlyphArrangement::getStringWidth (f, text);

    float x = box.getX();
    if (just.testFlags (juce::Justification::horizontallyCentred)) x = box.getCentreX() - w * 0.5f;
    else if (just.testFlags (juce::Justification::right))          x = box.getRight() - w;

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (f, text, x, box.getY() + baselineOf (s));
    glyphs.draw (g);
}

juce::Colour themeColour (int index) noexcept
{
    if (index < 0 || index >= kNumThemes) return colour::gray100;   // Placeholder-gray-100
    return juce::Colour (kThemes[index].argb);
}

juce::String themeName (int index)
{
    if (index < 0 || index >= kNumThemes) return {};
    return kThemes[index].name;
}

} // namespace os8
