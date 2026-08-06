#include "ViewPalette.h"

#include "CanvasComponent.h"   // the zoom range the slider spans is the canvas's

#include <cmath>

using juce::Graphics;
using juce::Point;
using juce::Rectangle;

namespace {
constexpr int kPad     = 8;
constexpr int kRowH    = 22;
constexpr int kSegW    = 28;   // one half of the layout toggle
constexpr int kLabelW  = 40;   // "ZOOM" / the percentage readout that replaces it
constexpr int kStepW   = 12;   // the - and + marks at the ends of the travel
constexpr int kTroughH = 12;
constexpr int kThumbW  = 9;
constexpr int kResetW  = 48;

/** How far off the 100% tick the cap still snaps to it, in pixels of travel. */
constexpr int kDetent = 3;

/** One zoom step for the - / + marks, matching the canvas's own +/- keys. */
constexpr float kStepFactor = 1.25f;

/** Four cells that touch, in two rows: the grid. */
void drawGridIcon (Graphics& g, Rectangle<int> r)
{
    constexpr int s = 6, gap = 1;
    const auto block = Rectangle<int> (s * 2 + gap, s * 2 + gap).withCentre (r.getCentre());

    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 2; ++col)
            g.drawRect (Rectangle<int> (block.getX() + col * (s + gap),
                                        block.getY() + row * (s + gap), s, s), 1);
}

/** The same windows, dropped where they fell: unequal, unaligned, overlapping. */
void drawFreeIcon (Graphics& g, Rectangle<int> r)
{
    const auto c = r.getCentre();
    g.drawRect (Rectangle<int> (c.x - 8, c.y - 7, 7, 6), 1);
    g.drawRect (Rectangle<int> (c.x + 1, c.y - 4, 7, 6), 1);
    g.drawRect (Rectangle<int> (c.x - 6, c.y + 2, 8, 5), 1);
}
}

ViewPalette::ViewPalette()
{
    // The palette must never take the keyboard away from the canvas: H / V / 0 and
    // the Windows' own ⌘-keys all go through whatever holds focus, and reaching for
    // the zoom slider is not a reason to lose them.
    setWantsKeyboardFocus (false);
    resetButton.setWantsKeyboardFocus (false);
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

void ViewPalette::setZoom (float newZoom)
{
    if (std::abs (newZoom - zoom) < 1.0e-4f) return;
    zoom = newZoom;
    repaint();
}

// --- layout ------------------------------------------------------------------

Rectangle<int> ViewPalette::body() const noexcept
{
    return getLocalBounds().withTrimmedRight (kShadow).withTrimmedBottom (kShadow);
}

void ViewPalette::resized()
{
    auto row = body().reduced (kPad, 0).withSizeKeepingCentre (body().getWidth() - 2 * kPad, kRowH);

    gridSeg = row.removeFromLeft (kSegW);
    freeSeg = row.removeFromLeft (kSegW);
    row.removeFromLeft (12);

    zoomLabel = row.removeFromLeft (kLabelW);
    row.removeFromLeft (4);
    minusRect = row.removeFromLeft (kStepW);
    row.removeFromLeft (2);

    // The trough takes what the fixed parts leave, so the slider is what grows if
    // these metrics are ever nudged.
    auto tail = row.removeFromRight (kResetW);
    resetButton.setBounds (tail);
    row.removeFromRight (10);
    plusRect = row.removeFromRight (kStepW);
    row.removeFromRight (2);

    troughRect = row.withSizeKeepingCentre (row.getWidth(), kTroughH);
}

// --- paint -------------------------------------------------------------------

void ViewPalette::paint (Graphics& g)
{
    const auto face = body().toFloat();

    // System 7 drew a solid black shadow under what floats above a window — a menu,
    // a palette — and nothing under what is docked in one. It is the only thing
    // saying this strip is over the canvas rather than part of it.
    g.setColour (sys7::kInk.withAlpha (0.35f));
    g.fillRoundedRectangle (face.translated ((float) kShadow, (float) kShadow), 5.0f);

    g.setColour (sys7::kPanel);
    g.fillRoundedRectangle (face, 5.0f);
    g.setColour (sys7::kInk);
    g.drawRoundedRectangle (face.reduced (0.5f), 5.0f, 1.0f);

    // The layout toggle: two icons in one frame, the live one on the chrome grey and
    // the other on white. Which arrangement you are in is the one lit, and the
    // pictures say what "grid" and "free" mean without spending a word each.
    const auto group = gridSeg.getUnion (freeSeg).toFloat();
    g.setColour (sys7::kWhite);
    g.fillRoundedRectangle (group, 3.0f);

    // The lit half is a plain rectangle clipped to the group's rounded outline —
    // filled straight it would push a square corner out through the round one.
    {
        juce::Path rounded;
        rounded.addRoundedRectangle (group, 3.0f);
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (rounded);
        g.setColour (sys7::kFace);
        g.fillRect (mosaic ? gridSeg : freeSeg);
    }

    g.setColour (sys7::kInk);
    g.drawVerticalLine (freeSeg.getX(), (float) group.getY() + 1.0f, (float) group.getBottom() - 1.0f);
    g.drawRoundedRectangle (group.reduced (0.5f), 3.0f, 1.0f);
    if (hovered == Part::grid || hovered == Part::free)
        g.drawRect (hovered == Part::grid ? gridSeg : freeSeg, sys7::kBorderHover);

    drawGridIcon (g, gridSeg);
    drawFreeIcon (g, freeSeg);

    // The slider's name, and — while it is being dragged — the number it is on. The
    // mixer's faders do the same: the label beside a control you are moving is the
    // one place a readout can appear without adding a control to read.
    g.setColour (dragging ? sys7::kInk : sys7::kDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (dragging ? juce::String (juce::roundToInt (zoom * 100.0f)) + "%" : juce::String ("ZOOM"),
                zoomLabel, juce::Justification::centredLeft, false);

    // - and + mark the ends of the travel, and step the zoom when clicked. Weight
    // carries the hover here as it does everywhere else in this UI.
    const auto drawStep = [&g] (Rectangle<int> r, bool plus, bool over)
    {
        const auto c = r.getCentre();
        const float t = over ? 2.0f : 1.0f;
        g.setColour (sys7::kInk);
        g.fillRect ((float) c.x - 4.0f, (float) c.y - t * 0.5f, 8.0f, t);
        if (plus) g.fillRect ((float) c.x - t * 0.5f, (float) c.y - 4.0f, t, 8.0f);
    };
    drawStep (minusRect, false, hovered == Part::minus);
    drawStep (plusRect,  true,  hovered == Part::plus);

    g.setColour (sys7::kWhite);
    g.fillRect (troughRect);

    // The 100% tick, the one place on the travel worth finding again — and the cap
    // snaps to it, so it can be found by feel and not only by eye.
    g.setColour (sys7::kInk);
    g.drawVerticalLine (xForPos (zoomToPos (1.0f)), (float) troughRect.getY(), (float) troughRect.getBottom());
    g.drawRect (troughRect, 1);

    // The cap: the mixer's fader handle, notched down the middle, riding this trough
    // instead of a meter.
    const int cx = xForPos (zoomToPos (zoom));
    const Rectangle<int> cap (cx - kThumbW / 2, troughRect.getY() - 3, kThumbW, troughRect.getHeight() + 6);
    g.setColour (sys7::kFace);
    g.fillRect (cap);
    g.setColour (sys7::kInk);
    g.drawRect (cap, hovered == Part::trough ? sys7::kBorderHover : sys7::kBorderIdle);
    g.drawVerticalLine (cx, (float) cap.getY() + 3.0f, (float) cap.getBottom() - 3.0f);
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
    if (troughRect.expanded (2, kRowH).contains (p)) return Part::trough;
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
