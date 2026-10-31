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

#include "Os8Icons.h"

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

/** The line height Figma gives every style: 98% of its size, in whole pixels. */
float lineHeightOf (TextStyle) noexcept;

/** Where Figma puts the baseline, measured down from the top of a text box.

    Figma centres the font's natural content box (ascent + descent, 1.25 em for this
    face) inside the line box. The line box is the smaller of the two, so the
    half-leading is negative and the baseline lands about 0.875 em down — not at the
    ascent. drawText() then rounds the absolute baseline to a whole pixel, as Figma
    does. Any other rule puts labels a pixel off their Figma position. */
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

// --- window themes: the Window-themes/* styles ------------------------------
//
// The 24 colours a Sound can wear. ONE table, in index order, because the index is
// what a .sefx stores: a Window saved with theme N comes back in colour N, so
// reordering this list repaints every saved canvas. Renaming or retuning an entry
// is free. The names are user-facing (Window ▸ Color: [name]).
//
// The 0.3 palette had 12 unnamed pastels; a 0.3 canvas's index N simply becomes
// colour N here, as decided with the design.

struct Theme
{
    const char* name;
    juce::uint32 argb;
};

inline constexpr Theme kThemes[] = {
    { "Candy orchid",  0xffff79d5 },   // 00
    { "Watermelon",    0xfffa808f },   // 01
    { "Soft butter",   0xfff6e58d },   // 02
    { "Vanilla",       0xfffff1b2 },   // 03
    { "Seashell",      0xfffec4b9 },   // 04
    { "Apricot",       0xfffdbe97 },   // 05
    { "Powder blush",  0xfff2a9a6 },   // 06
    { "Wisteria",      0xffd6b5ee },   // 07
    { "Soft lilac",    0xffc7aadd },   // 08
    { "Clear sky",     0xff9de0e3 },   // 09
    { "Glacier",       0xffb8f3f4 },   // 10
    { "Mint veil",     0xffd4f4dc },   // 11
    { "Denim air",     0xff97c2d3 },   // 12
    { "Rose milk",     0xfff6e5dd },   // 13
    { "Peach voile",   0xffefceb9 },   // 14
    { "Coral bloom",   0xffed8479 },   // 15
    { "Nectar cream",  0xfffbc375 },   // 16
    { "Marigold",      0xffeec33e },   // 17
    { "Meadow dust",   0xffc8cb81 },   // 18
    { "Citron haze",   0xffdbdd8e },   // 19
    { "Spring sprout", 0xffc3edcb },   // 20
    { "Garden sage",   0xff96cba0 },   // 21
    { "Tidal glass",   0xff90d5d7 },   // 22
    { "Lagoon mist",   0xff81dfe4 },   // 23
};

inline constexpr int kNumThemes = (int) (sizeof (kThemes) / sizeof (kThemes[0]));

/** The colour of theme `index`. Out of range — including "no theme" (-1), which a
    Window carries until it is painted — gives Placeholder-gray-100, the slot the
    design system reserves for exactly that. */
juce::Colour themeColour (int index) noexcept;

/** The user-facing name of theme `index`, or an empty string out of range. */
juce::String themeName (int index);

// --- paint primitives ---------------------------------------------------------
//
// Figma effects, reproduced. Every bevel in the design system is a stack of these:
// a fill, then inner shadows in the order Figma lists them (first listed = painted
// first), then the stroke on top.

/** A Figma inner shadow on `shape`. With `blur` 0 it is exact: the part of the
    shape left uncovered when a copy is shifted by (dx, dy) — a hard bevel band.
    With blur > 0 it is a Gaussian of that band (Figma's radius ≈ 2 sigma). */
void innerShadow (juce::Graphics&, const juce::Path& shape, float dx, float dy,
                  int blur, juce::Colour);

/** A Figma drop shadow with no blur: `shape` shifted by (dx, dy), painted beneath. */
void dropShadow (juce::Graphics&, const juce::Path& shape, float dx, float dy, juce::Colour);

/** A Figma "inside" stroke of `weight` on a rounded rectangle. */
void strokeInside (juce::Graphics&, juce::Rectangle<float>, float cornerRadius,
                   float weight, juce::Colour);

// --- the UI Elements components (page "Design System", frame 636:21143) ------

enum class ButtonKind  { primary, secondary };            // Buttons 87:8
enum class ButtonState { normal, applied, active, disabled };

/** Buttons 87:8. Primary is the 25 px default button (a ring round a face);
    secondary the 20 px plain one. `applied` is the brief "Done!" state — the
    caller passes the text it wants shown. */
void paintButton (juce::Graphics&, juce::Rectangle<int>, ButtonKind, ButtonState,
                  const juce::String& text, TextStyle = TextStyle::headline);

/** The height Figma gives each kind of button. */
int buttonHeight (ButtonKind) noexcept;

/** Checkbox 87:1401 — a 12 px box with its top-left at `origin`. */
void paintCheckbox (juce::Graphics&, juce::Point<float> origin, bool ticked, bool enabled);

/** Radiobutton 292:4290 — a 12 px disc with its top-left at `origin`. */
void paintRadio (juce::Graphics&, juce::Point<float> origin, bool selected, bool enabled);

inline constexpr int kToggleSize = 12;   // checkbox and radio

/** Text field 327:4482. Three sizes, four looks: Default (idle), Active (being
    edited), Highlighted (focused with its whole content selected — what a field
    shows the moment you tab or click into it) and Disabled. */
enum class FieldSize  { large, small, xSmall };
enum class FieldState { normal, active, highlighted, disabled };

struct FieldMetrics
{
    int height;          // the field's own height
    TextStyle style;     // the type inside it
    int textLeft;        // text box offset from the field's left edge
    int textTop;         // ...and from its top
    int haloWidth;       // the #6666cc focus ring, outside the field
};

FieldMetrics metricsOf (FieldSize) noexcept;

/** The field's frame and fill, plus everything Figma draws OUTSIDE it: the 1 px
    sunken bevel when idle, the focus halo when active. Callers must leave room for
    that (or paint unclipped) — the halo is 2 px wide on large and small fields. */
void paintTextField (juce::Graphics&, juce::Rectangle<int>, FieldSize, FieldState);

/** The text colour each state uses. */
juce::Colour fieldTextColour (FieldSize, FieldState) noexcept;

/** The popup's resting look ("Dropdown" 328:5750): a bevelled pill, the label left,
    the double arrow in its own little bevelled well at the right. */
enum class DropdownState { normal, active, disabled };
inline constexpr int kDropdownHeight = 17;
void paintDropdown (juce::Graphics&, juce::Rectangle<int>, DropdownState, const juce::String& text);

/** "mini-dropdown" 502:4002: the 19 x 12 button only. Its label (the unit, "ms")
    is ordinary 9 pt text the caller sets 4 px to its right. */
inline constexpr int kMiniDropdownWidth = 19, kMiniDropdownHeight = 12;
void paintMiniDropdown (juce::Graphics&, juce::Point<int> origin, DropdownState);

/** Figma's MULTIPLY blend of `layer` onto a flat `under` colour. JUCE has no blend
    modes, but every multiplied layer in the design system sits on a known flat
    colour (a strip's theme, a panel's grey), so the product can be worked out. */
juce::Colour multiply (juce::Colour layer, juce::Colour under) noexcept;

// --- sliders ("Sliders" 354:12896, "slider-button" 354:12903, "VU-segment") ---

enum class SliderKind { volume, pan, amplify };

inline constexpr int kSliderHeight = 18;
inline constexpr int kThumbWidth = 11;

struct SliderLook
{
    float thumbX = 0.0f;       // the thumb's left edge, relative to the slider
    float markerX = -1.0f;     // the tick (0 dB / centre / 100 %), < 0 for none
    float centreX = -1.0f;     // pan only: where the deviation bar starts
    float levelLeft = 0.0f;    // volume only: VU bars, 0..1 of the trough width
    float levelRight = 0.0f;
    bool muted = false;        // a muted strip: pale trough, grey outline and VU
    juce::Colour under = colour::white;   // what the multiplied trough sits on
};

/** The trough, its contents and the thumb, in a `kSliderHeight`-tall rectangle. */
void paintSlider (juce::Graphics&, juce::Rectangle<int>, SliderKind, const SliderLook&);

/** The 11 x 18 thumb with its three grip lines. */
void paintSliderThumb (juce::Graphics&, juce::Point<float> origin);

// --- Icon-button 527:5725 and Canvas-toggle 527:5564 -------------------------

enum class IconButtonState { normal, active, disabled };

/** A secondary-style bevelled face with a one-colour icon centred in it. */
void paintIconButton (juce::Graphics&, juce::Rectangle<int>, IconButtonState, const juce::Path& icon,
                      juce::Point<int> iconSize);

/** The two-segment grid | free switch; `gridActive` picks the pressed half. */
void paintCanvasToggle (juce::Graphics&, juce::Point<int> origin, bool gridActive);
inline constexpr int kCanvasToggleWidth = 44, kCanvasToggleHeight = 20;

// --- transport ("Transport-button" 761:17286, "Combo-play-button" 795:16809) --

enum class TransportState { normal, active, disabled };
inline constexpr int kTransportFace = 25;   // plus a 1 px outside stroke all round

/** One 25 x 25 transport face with `face` as its top-left; the outside stroke lands
    one pixel beyond it. The icon is centred and red: with a hard drop shadow when
    resting, without when pressed, grey when disabled. */
void paintTransportButton (juce::Graphics&, juce::Point<int> face, icon::Id, TransportState);

/** The 8 x 27 strip beside Play that opens the direction menu (740:15137). */
void paintPlayDirectionButton (juce::Graphics&, juce::Point<int> origin, TransportState);
inline constexpr int kPlayDirectionWidth = 8, kPlayDirectionHeight = 27;

/** The direction menu (795:22732): three 24 px cells — backward, ping-pong,
    forward. `current` is drawn greyed (it is where you already are), `hover` (or -1)
    in the menu highlight. */
void paintDirectionMenu (juce::Graphics&, juce::Point<int> origin, int current, int hover);
inline constexpr int kDirectionMenuWidth = 72, kDirectionMenuHeight = 27, kDirectionCellWidth = 24;

/** The frame every Mac OS 8 menu and popup panel wears: #dddddd, 2 px light and dark
    inner bands, a black inside stroke and a hard 1 px drop shadow. */
void paintMenuFrame (juce::Graphics&, juce::Rectangle<float>);

// --- title bars ("window-bar" 293:8807, "bar-handle" 522:4634) ----------------

inline constexpr int kTitleBarHeight = 21;

/** What an 11 x 11 title-bar box shows. */
enum class TitleBox { close, mute, solo, hidePanel, plain };

/** The engraved stripes of a title bar: 7 one-pixel lines, 2 px apart, starting 4 px
    down, within `area` (whose height should be the bar's, 21 px). */
void paintTitleStripes (juce::Graphics&, juce::Rectangle<int> area);

/** A title bar: stripes either side of a centred title, `leftInset` / `rightInset`
    px kept clear at the ends for buttons. Inactive bars have no stripes, as on the
    Mac. */
void paintTitleBar (juce::Graphics&, juce::Rectangle<int> bar, const juce::String& title,
                    bool active, int leftInset, int rightInset);

/** An 11 x 11 title-bar box ("window-button" 203:4347, revised 2026-10-31):
    bevelled face, 1 px black stroke outside it, drawn with its face's top-left at
    `origin`. Close has a gradient face, M and S a #dddddd one with their letter,
    hide-panel a gradient face with a window-shade glyph. `on` is the Pressed
    variant: a #333333 face, the bevel reversed and the mark in white. */
void paintTitleBarBox (juce::Graphics&, juce::Point<int> origin, bool on, TitleBox = TitleBox::plain);

/** A 16 px zoom-bar button ("zoom-controls" 589:8166): the transport-button bevel at
    a smaller size, the outside stroke grown into by the bevel. */
void paintSmallBevelButton (juce::Graphics&, juce::Rectangle<int> face, bool pressed);

// --- canvas palettes ("Frames" Canvas-palette 534:11202) ---------------------------

/** The frame the floating canvas palettes wear: #eeeeee, 8 px corners, a black inside
    stroke, a grey bevel on each side and a soft grey shadow 2 px below. `r` is the
    frame itself; the shadow lands 2 px under it, so callers keep that room. */
void paintPaletteFrame (juce::Graphics&, juce::Rectangle<int> r);
inline constexpr int kPaletteShadow = 2;

} // namespace os8
