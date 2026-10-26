#include "ViewPalette.h"

#include "CanvasComponent.h"   // the zoom range the slider spans is the canvas's

#include <cmath>

using juce::Graphics;
using juce::Point;
using juce::Rectangle;

namespace {
// The frame's content box starts 9 px in; every position below is Figma's, within it.
constexpr int kPad     = 9;
constexpr int kThumbW  = os8::kThumbWidth;

/** How far off the 100% tick the cap still snaps to it, in pixels of travel. */
constexpr int kDetent = 3;

/** One zoom step for the - / + marks, matching the canvas's own +/- keys. */
constexpr float kStepFactor = 1.25f;
}

ViewPalette::ViewPalette()
{
    // The palette must never take the keyboard away from the canvas: H / V / 0 and
    // the Windows' own ⌘-keys all go through whatever holds focus, and reaching for
    // the zoom slider is not a reason to lose them.
    setWantsKeyboardFocus (false);
    resetButton.setWantsKeyboardFocus (false);
    resetButton.setMouseClickGrabsKeyboardFocus (false);
    resetButton.setGreyed (true);   // at 100% to begin with
    resetButton.onClick = [this] { if (onZoomReset) onZoomReset(); };
    addAndMakeVisible (resetButton);
}

// --- the zoom scale ----------------------------------------------------------

float ViewPalette::posToZoom (float pos) noexcept
{
    const float lo = std::log2 (CanvasComponent::kMinZoom);
    const float hi = std::log2 (CanvasComponent::kMaxZoom);
    return std::exp2 (lo + (hi - lo) * juce::jlimit (0.0f, 1.0f, pos));
}

float ViewPalette::zoomToPos (float zoom) noexcept
{
    const float lo = std::log2 (CanvasComponent::kMinZoom);
    const float hi = std::log2 (CanvasComponent::kMaxZoom);
    return juce::jlimit (0.0f, 1.0f, (std::log2 (zoom) - lo) / (hi - lo));
}

int ViewPalette::xForPos (float pos) const noexcept
{
    const int x0 = troughRect.getX() + kThumbW / 2;
    const int x1 = troughRect.getRight() - 1 - kThumbW / 2;
    return x0 + juce::roundToInt (juce::jlimit (0.0f, 1.0f, pos) * (float) (x1 - x0));
}

float ViewPalette::posForX (int x) const noexcept
{
    const int x0 = troughRect.getX() + kThumbW / 2;
    const int x1 = troughRect.getRight() - 1 - kThumbW / 2;
    if (x1 <= x0) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (float) (x - x0) / (float) (x1 - x0));
}

// --- state -------------------------------------------------------------------

void ViewPalette::setMosaicMode (bool grid)
{
    if (mosaic == grid) return;
    mosaic = grid;
    repaint();
}

void ViewPalette::setKeyboardShown (bool shown)
{
    if (keyboardShown == shown) return;
    keyboardShown = shown;
    repaint();
}

void ViewPalette::setZoom (float newZoom)
{
    if (std::abs (newZoom - zoom) < 1.0e-4f) return;
    zoom = newZoom;
    // The design shows the 100% button greyed while the view already is at 100%.
    resetButton.setGreyed (std::abs (zoom - 1.0f) < 1.0e-3f);
    repaint();
}

// --- layout ------------------------------------------------------------------

Rectangle<int> ViewPalette::body() const noexcept
{
    return getLocalBounds().withTrimmedBottom (kShadow);
}

void ViewPalette::resized()
{
    const auto o = body().getPosition() + Point<int> (kPad, kPad);
    gridSeg   = { o.x,       o.y, 22, 20 };
    freeSeg   = { o.x + 22,  o.y, 22, 20 };
    zoomLabel = { o.x + 52,  o.y + 5, 36, 10 };
    minusRect = { o.x + 91,  o.y + 4, 13, 12 };
    troughRect = { o.x + 112, o.y + 1, 94, 18 };
    plusRect  = { o.x + 214, o.y + 4, 12, 12 };
    resetButton.setBounds (o.x + 234, o.y, 45, 20);
    keysSeg   = { o.x + 287, o.y, 38, 20 };
}

// --- paint -------------------------------------------------------------------

void ViewPalette::paint (Graphics& g)
{
    using namespace os8::colour;
    os8::paintPaletteFrame (g, body());

    // The layout toggle: grid | free, the half you are in pressed.
    os8::paintCanvasToggle (g, gridSeg.getPosition(), mosaic);

    // The slider's name, and — while it is being dragged — the number it is on.
    g.setColour (black);
    os8::drawText (g, dragging ? juce::String (juce::roundToInt (zoom * 100.0f)) + "%" : juce::String ("Zoom"),
                   os8::TextStyle::smallLabel, zoomLabel.toFloat());

    // - and + mark the ends of the travel, and step the zoom when clicked. The design
    // sets them as 12 pt text (an em dash and a full-width plus); drawn as lines the
    // same size so they do not depend on the face having those glyphs.
    const auto mark = [&g] (Rectangle<int> r, bool plus)
    {
        const auto c = r.getCentre();
        g.fillRect (c.x - 4, c.y, 9, 1);
        if (plus) g.fillRect (c.x, c.y - 4, 1, 9);
    };
    mark (minusRect, false);
    mark (plusRect, true);

    // The slider is the pan one — a bar from the 100% tick to the cap.
    os8::SliderLook look;
    look.thumbX = (float) (xForPos (zoomToPos (zoom)) - troughRect.getX() - kThumbW / 2);
    look.centreX = (float) (xForPos (zoomToPos (1.0f)) - troughRect.getX());
    look.markerX = look.centreX - 0.5f;
    os8::paintSlider (g, troughRect, os8::SliderKind::pan, look);

    // The Keyboard toggle: pressed while its palette is up.
    os8::paintIconButton (g, keysSeg, keyboardShown ? os8::IconButtonState::active : os8::IconButtonState::normal,
                          os8::icon::outline (os8::icon::Id::keyboard), os8::icon::sizeOf (os8::icon::Id::keyboard));
}

// --- input -------------------------------------------------------------------

ViewPalette::Part ViewPalette::partAt (Point<int> p) const
{
    if (gridSeg.contains (p)) return Part::grid;
    if (freeSeg.contains (p)) return Part::free;
    if (minusRect.expanded (2).contains (p)) return Part::minus;
    if (plusRect.expanded (2).contains (p))  return Part::plus;
    // The whole row height grabs the slider: a 12px trough is a mean target, and the
    // palette has nothing else along that stretch to hit by mistake.
    if (keysSeg.contains (p)) return Part::keys;
    if (troughRect.expanded (2, 4).contains (p)) return Part::trough;
    return Part::none;
}

void ViewPalette::setHovered (Part p)
{
    if (hovered == p) return;
    hovered = p;
    repaint();
}

void ViewPalette::zoomFromDrag (int x)
{
    float z = posToZoom (posForX (x));

    // Snap to 100% within a few pixels of its tick, so the detent can be felt.
    if (std::abs (x - xForPos (zoomToPos (1.0f))) <= kDetent) z = 1.0f;

    if (onZoomChanged) onZoomChanged (z);
}

void ViewPalette::mouseDown (const juce::MouseEvent& e)
{
    switch (partAt (e.getPosition()))
    {
        // Clicking the segment you are already in is not a no-op in grid mode: it
        // re-flows the board, which is what the old Mosaic button did and the only
        // way to tidy Windows back into their cells.
        case Part::grid:  if (onMosaicModeChanged) onMosaicModeChanged (true);  break;
        case Part::free:  if (onMosaicModeChanged) onMosaicModeChanged (false); break;
        case Part::minus: if (onZoomChanged) onZoomChanged (zoom / kStepFactor); break;
        case Part::plus:  if (onZoomChanged) onZoomChanged (zoom * kStepFactor); break;
        case Part::trough:
            dragging = true;
            zoomFromDrag (e.getPosition().x);
            repaint();
            break;
        case Part::keys:
            // The palette answers by pushing the new state back through
            // setKeyboardShown, so this button never disagrees with what is on
            // screen — the same one-way street the zoom readout is on.
            if (onKeyboardToggled) onKeyboardToggled (! keyboardShown);
            break;
        case Part::none: break;
    }
}

void ViewPalette::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) zoomFromDrag (e.getPosition().x);
}

void ViewPalette::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;   // the label goes back to saying ZOOM
    repaint();
}

void ViewPalette::mouseMove (const juce::MouseEvent& e)
{
    setHovered (partAt (e.getPosition()));
}

void ViewPalette::mouseExit (const juce::MouseEvent&)
{
    setHovered (Part::none);
}
