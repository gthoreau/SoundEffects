#include "Os8.h"

#include "BinaryData.h"
#include "Os8Icons.h"

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

namespace {
// Held by a DeletedAtShutdown singleton rather than a function static, so JUCE frees
// it during shutdown — a static would outlive the leak detector and trip it.
struct TypefaceHolder : juce::DeletedAtShutdown
{
    juce::Typeface::Ptr chicago = juce::Typeface::createSystemTypefaceFor (
        BinaryData::ClassicChicago_ttf, (size_t) BinaryData::ClassicChicago_ttfSize);
    ~TypefaceHolder() override { clearSingletonInstance(); }
    JUCE_DECLARE_SINGLETON_INLINE (TypefaceHolder, true)
};
} // namespace

juce::Typeface::Ptr chicago()
{
    return TypefaceHolder::getInstance()->chicago;
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
    const float top = std::floor (((float) bounds.getHeight() - sizeOf (TextStyle::headline)) * 0.5f);
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

// --- text fields ----------------------------------------------------------------

FieldMetrics metricsOf (FieldSize size) noexcept
{
    switch (size)
    {
        case FieldSize::large:  return { 22, TextStyle::headline,    5, 5, 2 };
        case FieldSize::small:  return { 16, TextStyle::smallLabel,  5, 3, 2 };
        case FieldSize::xSmall: return { 15, TextStyle::xSmallLabel, 5, 3, 1 };
    }
    return { 16, TextStyle::smallLabel, 5, 3, 2 };
}

juce::Colour fieldTextColour (FieldSize size, FieldState state) noexcept
{
    if (state == FieldState::disabled)
        return size == FieldSize::xSmall ? colour::black : colour::gray400;
    if (state == FieldState::highlighted && size == FieldSize::xSmall)
        return colour::white;
    return colour::black;
}

void paintTextField (juce::Graphics& g, juce::Rectangle<int> bounds, FieldSize size, FieldState state)
{
    using namespace colour;
    const auto r = bounds.toFloat();
    const auto m = metricsOf (size);
    const juce::Colour halo (0xff6666cc);

    switch (state)
    {
        case FieldState::normal:
        {
            // A one-pixel sunken bevel OUTSIDE the field: grey above and left, white
            // below and right. Drawn as Figma does, four unblurred drop shadows.
            g.setColour (white);
            g.fillRect (r.translated (1.0f, 0.0f));
            g.fillRect (r.translated (0.0f, 1.0f));
            g.setColour (juce::Colour (0xff808080));
            g.fillRect (r.translated (-1.0f, 0.0f));
            g.fillRect (r.translated (0.0f, -1.0f));
            g.setColour (white);
            g.fillRect (r);
            // The x-small field (the sound window's selection readouts) has no
            // frame of its own — the bevel is its edge.
            if (size != FieldSize::xSmall)
                strokeInside (g, r, 0.0f, 1.0f, black);
            break;
        }

        case FieldState::active:
        case FieldState::highlighted:
        {
            // As Figma draws it: on large and small fields a 1 px ring with corners
            // (a spread-1 shadow) plus four copies shifted 2 px, so the outer pixel
            // of each corner stays notched; on x-small, only four 1 px shifts.
            const float w = (float) m.haloWidth;
            g.setColour (halo);
            if (size != FieldSize::xSmall)
                g.fillRect (r.expanded (1.0f));
            g.fillRect (r.translated (-w, 0.0f));
            g.fillRect (r.translated ( w, 0.0f));
            g.fillRect (r.translated (0.0f, -w));
            g.fillRect (r.translated (0.0f,  w));

            const bool inverse = state == FieldState::highlighted;
            g.setColour (! inverse ? white : (size == FieldSize::xSmall ? gray600 : selectedTextField));
            g.fillRect (r);

            // Highlighted keeps a white margin inside the frame: an inner shadow with
            // spread 3 (2 on x-small), most of which the 1 px stroke then covers.
            if (inverse)
            {
                const float band = size == FieldSize::xSmall ? 2.0f : 3.0f;
                juce::Path shape;
                shape.addRectangle (r);
                juce::Path inner;
                inner.addRectangle (r.reduced (band));
                shape.addPath (inner);
                shape.setUsingNonZeroWinding (false);
                g.setColour (white);
                g.fillPath (shape);
            }

            strokeInside (g, r, 0.0f, 1.0f, black);
            break;
        }

        case FieldState::disabled:
        {
            if (size == FieldSize::xSmall)
            {
                strokeInside (g, r, 0.0f, 1.0f, black);   // as drawn: a bare frame
            }
            else
            {
                g.setColour (gray100);
                g.fillRect (r);
                strokeInside (g, r, 0.0f, 1.0f, gray300);
            }
            break;
        }
    }
}

// --- dropdowns -------------------------------------------------------------------

namespace {

void pillFace (juce::Graphics& g, juce::Rectangle<float> r, float radius, float bevel,
               DropdownState state, bool withStroke)
{
    using namespace colour;
    const auto shape = roundedRect (r, radius);

    if (state == DropdownState::disabled)
    {
        g.setColour (gray100);
        g.fillPath (shape);
        if (withStroke) strokeInside (g, r, radius, 1.0f, gray300);
        return;
    }

    const bool active = state == DropdownState::active;
    g.setColour (active ? gray500 : gray100);
    g.fillPath (shape);
    innerShadow (g, shape, -bevel, -bevel, 0, gray400);
    innerShadow (g, shape,  bevel,  bevel, 0, active ? gray600 : juce::Colour (0xfffffdfd));
    if (withStroke) strokeInside (g, r, radius, 1.0f, black);
}

juce::Colour inkFor (DropdownState state) noexcept
{
    return state == DropdownState::active   ? colour::white
         : state == DropdownState::disabled ? colour::gray400
                                            : colour::black;
}

} // namespace

void paintDropdown (juce::Graphics& g, juce::Rectangle<int> bounds, DropdownState state,
                    const juce::String& text)
{
    const auto r = bounds.toFloat();
    pillFace (g, r, 3.0f, 2.0f, state, true);

    // The arrow well: 17 x 15, one pixel in from the right, top and bottom.
    const juce::Rectangle<float> well (r.getRight() - 18.0f, r.getY() + 1.0f, 17.0f, 15.0f);
    pillFace (g, well, 1.0f, 1.0f, state, false);

    g.setColour (inkFor (state));
    g.fillPath (icon::outline (icon::Id::dropdownDoubleArrow),
                juce::AffineTransform::translation (well.getX() + 5.0f, well.getY() + 3.0f));

    // Figma puts the label's box at (7, 3.5); drawText rounds the baseline.
    drawText (g, text, TextStyle::smallLabel,
              { r.getX() + 7.0f, r.getY() + 3.5f, well.getX() - r.getX() - 9.0f, 10.0f });
}

void paintMiniDropdown (juce::Graphics& g, juce::Point<int> origin, DropdownState state)
{
    const juce::Rectangle<float> r ((float) origin.x, (float) origin.y,
                                    (float) kMiniDropdownWidth, (float) kMiniDropdownHeight);

    if (state == DropdownState::disabled)
    {
        // As drawn in Figma: the outline and arrow, no face.
        strokeInside (g, r, 3.0f, 1.0f, colour::black);
    }
    else
    {
        pillFace (g, r, 3.0f, 2.0f, state, true);
        pillFace (g, r.reduced (1.0f), 1.0f, 1.0f, state, false);
    }

    g.setColour (state == DropdownState::active ? colour::white : colour::black);
    g.fillPath (icon::outline (icon::Id::dropdownSingleArrow), juce::AffineTransform::translation (r.getX() + 6.0f, r.getY() + 4.0f));
}

// --- blending -----------------------------------------------------------------

juce::Colour multiply (juce::Colour layer, juce::Colour under) noexcept
{
    const auto ch = [] (juce::uint8 a, juce::uint8 b) { return (juce::uint8) ((a * b + 127) / 255); };
    const auto product = juce::Colour (ch (layer.getRed(), under.getRed()),
                                       ch (layer.getGreen(), under.getGreen()),
                                       ch (layer.getBlue(), under.getBlue()));
    // A translucent layer multiplies only as much as it covers.
    return under.interpolatedWith (product, layer.getFloatAlpha());
}

// --- sliders ----------------------------------------------------------------------

void paintSliderThumb (juce::Graphics& g, juce::Point<float> o)
{
    using namespace colour;
    const juce::Rectangle<float> r (o.x, o.y, (float) kThumbWidth, (float) kSliderHeight);
    const auto shape = roundedRect (r, 2.0f);
    g.setColour (gray100);
    g.fillPath (shape);
    innerShadow (g, shape,  2.0f,  2.0f, 0, white.withAlpha (0.5f));
    innerShadow (g, shape, -2.0f, -2.0f, 0, black.withAlpha (0.25f));
    strokeInside (g, r, 2.0f, 1.0f, black);

    // Three grip lines, 11.25 px tall from y 3.375 — fractional in Figma, so drawn
    // fractional here too and left to the same anti-aliasing.
    g.setColour (black);
    for (float x : { 3.0f, 5.0f, 7.0f })
        g.fillRect (juce::Rectangle<float> (o.x + x, o.y + 3.375f, 1.0f, 11.25f));
}

void paintSlider (juce::Graphics& g, juce::Rectangle<int> bounds, SliderKind kind, const SliderLook& look)
{
    using namespace colour;
    const auto b = bounds.toFloat();
    const juce::Rectangle<float> trough (b.getX(), b.getY() + 4.0f, b.getWidth(), 10.0f);
    const float radius = kind == SliderKind::volume ? 0.0f : 3.0f;
    const auto shape = roundedRect (trough, radius);

    // The trough is a MULTIPLY layer: #c0c0c0 with a dark band (1, 2) and a light
    // band (-1, -2) inside it, the whole layer then multiplied onto what is below —
    // on a tinted mixer strip that is what turns the grey into a darker tint.
    const auto base = gray300;
    g.setColour (multiply (base, look.under));
    g.fillPath (shape);
    innerShadow (g, shape,  1.0f,  2.0f, 0, multiply (base.interpolatedWith (black, 0.3f), look.under));
    innerShadow (g, shape, -1.0f, -2.0f, 0, multiply (base.interpolatedWith (white, 0.5f), look.under));

    if (kind == SliderKind::pan && look.centreX >= 0.0f)
    {
        // How far the balance is off centre, multiplied in a darker grey.
        const float thumbCentre = look.thumbX + (float) kThumbWidth * 0.5f;
        const float x0 = juce::jmin (look.centreX, thumbCentre), x1 = juce::jmax (look.centreX, thumbCentre);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        g.setColour (multiply (gray400, multiply (base, look.under)));
        g.fillRect (juce::Rectangle<float> (b.getX() + x0, trough.getY(), x1 - x0, trough.getHeight()));
    }

    // The trough's outline: square on the fader, a 4 px radius on the others.
    strokeInside (g, trough, kind == SliderKind::volume ? 0.0f : 4.0f, 1.0f, black);

    if (kind == SliderKind::volume)
    {
        // Two VU bars, left over right, each a 5 px bevelled segment.
        const auto bar = [&] (float level, float y)
        {
            const float w = juce::jlimit (0.0f, 1.0f, level) * trough.getWidth();
            if (w < 1.0f) return;
            const juce::Rectangle<float> r (trough.getX(), y, std::round (w), 5.0f);
            juce::Path seg;
            seg.addRectangle (r);
            if (look.muted)
            {
                g.setColour (gray400);
                g.fillPath (seg);
                strokeInside (g, r, 0.0f, 1.0f, gray400);
            }
            else
            {
                g.setColour (gray500);
                g.fillPath (seg);
                innerShadow (g, seg, 0.0f, -2.0f, 0, gray600);
                innerShadow (g, seg, 0.0f,  2.0f, 0, gray600);
                strokeInside (g, r, 0.0f, 1.0f, black);
            }
        };
        bar (look.levelLeft,  trough.getY());
        bar (look.levelRight, trough.getY() + 5.0f);
    }

    if (look.markerX >= 0.0f)
    {
        g.setColour (black);
        if (kind == SliderKind::pan) g.fillRect (juce::Rectangle<float> (b.getX() + look.markerX, b.getY() + 7.0f, 1.0f, 4.0f));
        else                         g.fillRect (juce::Rectangle<float> (b.getX() + look.markerX, b.getY() + 5.0f, 1.0f, 8.0f));
    }

    paintSliderThumb (g, { b.getX() + look.thumbX, b.getY() });
}

// --- icon button and canvas toggle --------------------------------------------------

void paintIconButton (juce::Graphics& g, juce::Rectangle<int> bounds, IconButtonState state,
                      const juce::Path& icon, juce::Point<int> iconSize)
{
    using namespace colour;
    const auto r = bounds.toFloat();

    switch (state)
    {
        case IconButtonState::normal:   bevelledFace (g, r, 3.0f, gray100, juce::Colour (0xfffffdfd), gray400, false, black); break;
        case IconButtonState::active:   bevelledFace (g, r, 2.0f, gray500, gray600, gray400, false, black); break;
        case IconButtonState::disabled:
            g.setColour (gray100);
            g.fillPath (roundedRect (r, 3.0f));
            strokeInside (g, r, 3.0f, 1.0f, gray300);
            break;
    }

    g.setColour (state == IconButtonState::active ? white : state == IconButtonState::disabled ? gray400 : black);
    const auto origin = bounds.getCentre() - iconSize / 2;
    g.fillPath (icon, juce::AffineTransform::translation (origin.toFloat()));
}

void paintCanvasToggle (juce::Graphics& g, juce::Point<int> o, bool gridActive)
{
    using namespace colour;
    const auto half = [&] (juce::Rectangle<float> r, bool left, bool active)
    {
        juce::Path shape;
        shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 3.0f, 3.0f,
                                   left, ! left, left, ! left);
        g.setColour (active ? gray500 : gray100);
        g.fillPath (shape);

        // The bevel offsets differ per half, as drawn: the left half's dark band is
        // 1 px wide (its right edge is the seam), the right half's light band
        // narrows to 1 px when it is not the pressed one.
        innerShadow (g, shape, left ? -1.0f : -2.0f, -2.0f, 0, gray400);
        innerShadow (g, shape, (left || active) ? 2.0f : 1.0f, 2.0f, 0,
                     active ? gray600 : juce::Colour (0xfffffdfd));

        // Outline on three sides only: neither half strokes the seam.
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion ((left ? r.withTrimmedRight (1.0f) : r.withTrimmedLeft (1.0f)).toNearestInt());
        const auto inset = r.reduced (0.5f);
        juce::Path edge;   // an "inside" stroke: the path half a pixel in
        edge.addRoundedRectangle (inset.getX(), inset.getY(), inset.getWidth(), inset.getHeight(),
                                  2.5f, 2.5f, left, ! left, left, ! left);
        g.setColour (black);
        g.strokePath (edge, juce::PathStrokeType (1.0f));
    };

    const juce::Rectangle<float> left ((float) o.x, (float) o.y, 22.0f, 20.0f);
    const auto right = left.translated (22.0f, 0.0f);
    half (left, true, gridActive);
    half (right, false, ! gridActive);

    g.setColour (gridActive ? white : black);
    g.fillPath (icon::outline (icon::Id::canvasGrid), juce::AffineTransform::translation (left.getX() + 5.0f, left.getY() + 4.0f));
    g.setColour (gridActive ? black : white);
    g.fillPath (icon::outline (icon::Id::canvasFree), juce::AffineTransform::translation (right.getX() + 4.0f, right.getY() + 3.0f));
}

} // namespace os8
