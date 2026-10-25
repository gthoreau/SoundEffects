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
    // Figma reports a 98% line box in whole pixels: 11.76 -> 12, 10.78 -> 11, 9.8 -> 10.
    return std::round (sizeOf (s) * kLineHeight);
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

    // Figma snaps the baseline to a whole pixel — its 1x exports of 10, 11 and 12 pt
    // labels only line up with a rounded baseline (12 pt at integer y is perfectly
    // crisp; 10.5 unrounded would smear every row). So do the same, which is also
    // what keeps a pixel face from blurring between two rows.
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (f, text, x, std::round (box.getY() + baselineOf (s)));
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

// --- paint primitives ---------------------------------------------------------

void innerShadow (juce::Graphics& g, const juce::Path& shape, float dx, float dy,
                  int blur, juce::Colour c)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (shape);

    // Everything OUTSIDE the shape, shifted: where it overlaps the shape is the band
    // the shadow paints. Even-odd winding turns "rectangle + shape" into "rectangle
    // with a shape-sized hole" without needing path booleans.
    const float pad = std::abs (dx) + std::abs (dy) + (float) blur * 3.0f + 4.0f;
    juce::Path outside;
    outside.addRectangle (shape.getBounds().expanded (pad));
    outside.addPath (shape);
    outside.setUsingNonZeroWinding (false);
    outside.applyTransform (juce::AffineTransform::translation (dx, dy));

    if (blur <= 0)
    {
        g.setColour (c);
        g.fillPath (outside);
    }
    else
    {
        juce::DropShadow (c, blur, {}).drawForPath (g, outside);
    }
}

void dropShadow (juce::Graphics& g, const juce::Path& shape, float dx, float dy, juce::Colour c)
{
    g.setColour (c);
    g.fillPath (shape, juce::AffineTransform::translation (dx, dy));
}

void strokeInside (juce::Graphics& g, juce::Rectangle<float> r, float cornerRadius,
                   float weight, juce::Colour c)
{
    const float half = weight * 0.5f;
    g.setColour (c);
    if (cornerRadius > 0.0f)
        g.drawRoundedRectangle (r.reduced (half), juce::jmax (0.0f, cornerRadius - half), weight);
    else
        g.drawRect (r, weight);
}

namespace {

juce::Path roundedRect (juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    return p;
}

// A raised (or, with `pressed`, sunk) face: the light band top-left, the dark band
// bottom-right, both hard-edged, 3 px across and 2 px down — the bevel every
// button face in the design system carries.
void bevelledFace (juce::Graphics& g, juce::Rectangle<float> r, float radius,
                   juce::Colour fill, juce::Colour light, juce::Colour dark, bool pressed,
                   juce::Colour stroke)
{
    const auto shape = roundedRect (r, radius);
    g.setColour (fill);
    g.fillPath (shape);

    // Figma lists the dark shadow first, so it is painted first.
    const float s = pressed ? -1.0f : 1.0f;
    innerShadow (g, shape, -3.0f * s, -2.0f * s, 0, dark);
    innerShadow (g, shape,  3.0f * s,  2.0f * s, 0, light);
    strokeInside (g, r, radius, 1.0f, stroke);
}

} // namespace

int buttonHeight (ButtonKind k) noexcept
{
    return k == ButtonKind::primary ? 25 : 20;
}

void paintButton (juce::Graphics& g, juce::Rectangle<int> bounds, ButtonKind kind,
                  ButtonState state, const juce::String& text)
{
    using namespace colour;
    const bool disabled = state == ButtonState::disabled;
    const bool active   = state == ButtonState::active;
    const bool applied  = state == ButtonState::applied;

    const auto outer = bounds.toFloat();
    const auto ring = roundedRect (outer, 3.0f);

    // Primary: an outer ring with a soft bevel of its own, the face set into it.
    if (kind == ButtonKind::primary)
    {
        g.setColour (gray100);
        g.fillPath (ring);
    }

    const auto r = kind == ButtonKind::primary ? outer.reduced (2.0f) : outer;
    const float radius = kind == ButtonKind::primary ? 2.0f
                       : (active ? 2.0f : 3.0f);

    if (disabled)
    {
        g.setColour (gray100);
        g.fillPath (roundedRect (r, radius));
        strokeInside (g, r, radius, 1.0f, gray300);
    }
    else if (active)
    {
        bevelledFace (g, r, radius, gray500, gray600, gray400, false, black);
    }
    else
    {
        // Normal is raised; Applied is the same face pressed in — the light and
        // dark swap sides, which is the whole of the "Done!" state's look.
        bevelledFace (g, r, radius, gray100, juce::Colour (0xfffffdfd), gray400, applied, black);
    }

    g.setColour (disabled ? gray400 : (active ? white : black));
    const float top = std::floor ((float) (bounds.getHeight() - sizeOf (TextStyle::headline)) * 0.5f);
    drawText (g, text, TextStyle::headline,
              { (float) bounds.getX(), (float) bounds.getY() + top, (float) bounds.getWidth(), 12.0f },
              juce::Justification::horizontallyCentred);

    // The ring's own shadows come AFTER the face: Figma paints a frame's inner
    // shadows over its children, so the soft bevel bleeds onto the face's black
    // outline (a 1x export shows that outline at #3x-#6x, not black).
    if (kind == ButtonKind::primary)
    {
        if (! disabled)
        {
            innerShadow (g, ring,  2.0f,  2.0f, 2, gray300);
            innerShadow (g, ring, -2.0f, -2.0f, 2, juce::Colour (0xff777777));
        }
        strokeInside (g, outer, 3.0f, 1.0f, disabled ? gray300 : black);
    }
}

void paintCheckbox (juce::Graphics& g, juce::Point<float> o, bool ticked, bool enabled)
{
    using namespace colour;
    const juce::Rectangle<float> box (o.x, o.y, 12.0f, 12.0f);
    juce::Path shape;
    shape.addRectangle (box);

    if (enabled)
        dropShadow (g, shape, 0.5f, 0.5f, juce::Colour (0xff262626).withAlpha (0.3f));

    g.setColour (gray100);
    g.fillRect (box);

    if (enabled)
    {
        // The box's own one-pixel bevel, inside the frame.
        g.setColour (white);
        g.fillRect (box.getX() + 1.0f, box.getY() + 1.0f, 10.0f, 1.0f);
        g.fillRect (box.getX() + 1.0f, box.getY() + 1.0f, 1.0f, 10.0f);
        g.setColour (gray500);
        g.fillRect (box.getX() + 1.0f, box.getY() + 10.0f, 10.0f, 1.0f);
        g.fillRect (box.getX() + 10.0f, box.getY() + 1.0f, 1.0f, 10.0f);
    }

    strokeInside (g, box, 0.0f, 1.0f, enabled ? black : (ticked ? gray400 : gray300));

    if (ticked)
    {
        // "Atom / Checkmark", exported from Figma as SVG: a fill plus its own outline
        // (the drawn weight comes from both). It overhangs the box top and right on
        // purpose — the tick of a Mac OS 8 checkbox breaks out of it.
        static const juce::Path tick = []
        {
            auto p = juce::Drawable::parseSVGPath (
                "M2.81819 7.32637L2.87496 7.01153C2.98154 6.42053 3.70484 6.18822 4.13561 6.60663L5.63295 8.061"
                "C5.93304 8.35246 6.41356 8.34209 6.70079 8.03794L12.4204 1.98135C12.6628 1.72467 13.0746 1.7359 13.3026 2.00541"
                "C13.4889 2.2255 13.4894 2.54774 13.304 2.76848L6.71366 10.6131C6.42752 10.9537 5.90951 10.9718 5.60027 10.652"
                "L3.01714 7.98084C2.84941 7.80739 2.77537 7.56383 2.81819 7.32637Z");
            auto outline = juce::Drawable::parseSVGPath (
                "M13.0164 2.24761C12.9328 2.14885 12.7819 2.14473 12.6931 2.23879L6.97343 8.29539C6.5426 8.75161 5.82181 8.76717 5.37168 8.32996"
                "L3.87434 6.8756C3.65895 6.66639 3.2973 6.78255 3.24402 7.07805L3.18724 7.39289C3.16583 7.51162 3.20285 7.6334 3.28672 7.72012"
                "L5.86984 10.3913C6.02446 10.5512 6.28347 10.5421 6.42654 10.3718L13.0169 2.52724C13.0848 2.44635 13.0846 2.32826 13.0164 2.24761Z"
                "M12.1478 1.72384C12.5438 1.30455 13.2164 1.3229 13.5889 1.76315C13.8931 2.12267 13.894 2.64908 13.5911 3.00967L7.00079 10.8543"
                "C6.57158 11.3652 5.79456 11.3923 5.3307 10.9127L2.74757 8.24149C2.49597 7.98131 2.38492 7.61598 2.44915 7.25979L2.50592 6.94495"
                "C2.66578 6.05846 3.75073 5.70999 4.39689 6.3376L5.89424 7.79197C6.04428 7.9377 6.28454 7.93252 6.42815 7.78044L12.1478 1.72384Z");
            outline.setUsingNonZeroWinding (false);
            p.addPath (outline);
            // The SVG's origin sits 2 px above the box (room for the overhang).
            p.applyTransform (juce::AffineTransform::translation (0.0f, -2.0f));
            return p;
        }();

        g.setColour (enabled ? black : gray400);
        g.fillPath (tick, juce::AffineTransform::translation (o));
    }
}

void paintRadio (juce::Graphics& g, juce::Point<float> o, bool selected, bool enabled)
{
    using namespace colour;
    const juce::Rectangle<float> disc (o.x, o.y, 12.0f, 12.0f);
    juce::Path shape;
    shape.addEllipse (disc);

    if (! enabled)
    {
        g.setColour (gray100);
        g.fillPath (shape);
        g.setColour (gray300);
        g.drawEllipse (disc.reduced (0.5f), 1.0f);
        if (selected)
        {
            g.setColour (gray400);
            g.fillEllipse (disc.reduced (3.0f));
        }
        return;
    }

    // Radial gradients from the SVG export: the highlight sits up-left on the empty
    // disc (a bead catching the light) and down-right on the selected one (a dimple).
    const auto bead = [&] (juce::Point<float> centre, float radius, float inner, float outer)
    {
        juce::ColourGradient grad (juce::Colour (0xfffffdfd), o + centre,
                                   juce::Colour (0xffa1a1a1), o + centre + juce::Point<float> (radius, 0.0f), true);
        grad.addColour (inner, juce::Colour (0xfffffdfd));
        grad.addColour (outer, juce::Colour (0xffa1a1a1));
        g.setGradientFill (grad);
        g.fillPath (shape);
    };

    if (selected)
    {
        bead ({ 10.0f, 10.0f }, 9.92472f, 0.336538f, 0.764493f);
        innerShadow (g, shape, 3.0f, 3.0f, 0, black.withAlpha (0.1f));
    }
    else
    {
        dropShadow (g, shape, 0.5f, 0.5f, black.withAlpha (0.25f));
        bead ({ 4.15385f, 4.15385f }, 8.16545f, 0.336538f, 1.0f);
        innerShadow (g, shape, -2.0f, -2.0f, 0, juce::Colour (0xffb7b7b7));
    }

    g.setColour (black);
    g.drawEllipse (disc.reduced (0.5f), 1.0f);

    if (selected)
        g.fillEllipse (disc.reduced (3.0f));
}

} // namespace os8
