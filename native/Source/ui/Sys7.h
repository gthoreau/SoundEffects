// Sys7 — the shared faux-System 7 vocabulary the side panels are drawn in.
//
// Everything is hand-painted in the same idiom as the Windows on the canvas: flat
// grey face, 1px black frames, no gradients, inverted fill for the "on" state, and
// BORDER WEIGHT rather than colour to carry hover. Extracted from MixerComponent
// when the effects panel needed the same buttons and the same scrollbar — two
// panels drawn from one vocabulary, not two that merely resemble each other.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

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

    /** The heavy ring System 7 draws around the button Return presses. Purely the
        look — the key itself is handled by whoever owns the dialog. The ring is
        drawn INSIDE the button's bounds, so a default button needs the room:
        `kDefaultRingInset` px on every side. */
    void setDefaultButton (bool shouldBeDefault);

    /** How much a default button's ring eats on each side. */
    static constexpr int kDefaultRingInset = 4;

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    bool defaultButton = false;
};

/**
 * A System 7 modal alert: a title, an optional checkbox, and two buttons.
 *
 * Exists because Apply is irreversible (§1) and the parameterless effects have no
 * form to pause on — their button IS the whole interaction, so without a stop
 * between the click and the edit there is nothing at all between a mis-aimed
 * pointer and a rewritten sound.
 *
 * Return picks the default button, Escape picks Cancel — and a click outside the
 * box picks neither. A modal alert whose backdrop dismisses it is a modal alert
 * that can be dismissed by accident, which for this one means running the effect
 * or losing the question depending on where the pointer happened to land.
 */
class Sys7AlertDialog : public juce::Component
{
public:
    /**
     * Cover `host`'s window with the alert and go modal.
     *
     * `checkboxText` empty means no checkbox. It starts unticked every time — it is
     * a question about this dialog's future, not a setting the dialog remembers.
     *
     * `onChoice (confirmed, ticked)`: `confirmed` is true for the default button,
     * false for Cancel. `ticked` is the checkbox as the dialog closed, and is only
     * meaningful when confirmed — Cancel discards the whole dialog, checkbox
     * included, because a box ticked inside a dialog you then cancelled is not an
     * answer. The callback runs after the dialog has gone.
     */
    static void show (juce::Component& host,
                      const juce::String& title,
                      const juce::String& checkboxText,
                      const juce::String& confirmText,
                      const juce::String& cancelText,
                      std::function<void (bool confirmed, bool ticked)> onChoice);

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}   // the backdrop is not a Cancel

private:
    Sys7AlertDialog (const juce::String& title, const juce::String& checkboxText,
                     const juce::String& confirmText, const juce::String& cancelText);

    /** Where the box sits inside the full-window backdrop. */
    juce::Rectangle<int> boxBounds() const;

    juce::String title;
    Sys7Button confirm, cancel;
    std::unique_ptr<juce::ToggleButton> checkbox;

    static constexpr int kBoxW = 300;
    static constexpr int kCheckH = 20;
    static constexpr int kButtonH = 22;
    static constexpr int kPad = 14;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sys7AlertDialog)
};

/** Only what the panels actually borrow from JUCE: the list's scrollbar. */
class Sys7LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
};
