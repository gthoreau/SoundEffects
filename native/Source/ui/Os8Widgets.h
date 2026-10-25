// Os8Widgets — the interactive controls of the Mac OS 8 vocabulary. Each one is a
// thin JUCE component around a painter in Os8.h, so the gallery and the panels draw
// from exactly the same code.

#pragma once

#include "Os8.h"

/** Buttons 87:8, as a JUCE button.

    Held down it shows Active. `flashApplied()` shows Applied ("Done!") for
    `kAppliedMs`, the spec's ~0.5 s of feedback after Apply — tied to nothing but the
    clock, because applying an effect is near-instant and the flash is there to be
    seen, not to measure anything. */
class Os8Button : public juce::Button,
                  private juce::Timer
{
public:
    explicit Os8Button (const juce::String& text, os8::ButtonKind = os8::ButtonKind::secondary);

    /** How long the Applied state stays up. One constant, to be tuned by eye. */
    static constexpr int kAppliedMs = 500;

    /** Show the Applied state (and its text) for kAppliedMs. */
    void flashApplied();

    void setAppliedText (const juce::String& t) { appliedText = t; repaint(); }
    void setKind (os8::ButtonKind k)            { kind = k; repaint(); }
    os8::ButtonKind getKind() const noexcept    { return kind; }

    bool isShowingApplied() const noexcept      { return applied; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    void timerCallback() override;

    os8::ButtonKind kind;
    juce::String appliedText { "Done!" };
    bool applied = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Button)
};

/** Checkbox 87:1401 or Radiobutton 292:4290 with its label, laid out as every
    instance in the design system is: the 12 px control, a 4 px gap, then the label
    one pixel lower than the control's top. Radios become exclusive through JUCE's
    own radio groups (setRadioGroupId). */
class Os8Toggle : public juce::ToggleButton
{
public:
    enum class Shape { checkbox, radio };

    Os8Toggle (const juce::String& label, Shape, os8::TextStyle = os8::TextStyle::smallLabel);

    /** Width that fits the control and its whole label. */
    int preferredWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    static constexpr int kLabelGap = 4;

private:
    Shape shape;
    os8::TextStyle style;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Toggle)
};

/** The Mac OS 8 look for what JUCE draws itself — for now, popup menus ("dropdown
    menus" 339:11140 and "Dropdown-menu-items" 607:6687).

    JUCE lays a menu's items from x = 0 and adds the border on both sides, so the
    window is 2 px wider than the frame Figma draws; the frame is painted inside it
    and the spare pixels are left transparent, which is also where the 1 px drop
    shadow goes. Items are 16 px with a 1 px gap below. */
class Os8LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Os8LookAndFeel();

    /** One shared instance; menus outlive the components that opened them. */
    static Os8LookAndFeel& instance();

    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height,
                                             const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area,
                                       bool isHighlighted, const juce::PopupMenu::Item&,
                                       const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                               int standardMenuItemHeight, int& idealWidth,
                                               int& idealHeight, const juce::PopupMenu::Options&) override;
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return kBorder; }
    juce::Font getPopupMenuFont() override { return os8::font (os8::TextStyle::smallLabel); }

    static constexpr int kBorder = 2;
    static constexpr int kItemHeight = 16, kItemGap = 1;
    static constexpr int kSeparatorHeight = 6;
};

/** Text field 327:4482 around a JUCE TextEditor.

    Gaining focus selects everything (Highlighted); typing turns it Active. Return
    or losing focus commits, Escape reverts. With setNumeric(), ↑/↓ step the value
    and ⇧↑/⇧↓ step it ten times as far — in every field, as the spec asks.

    The focus halo and the idle bevel sit OUTSIDE the field's bounds, so the
    component paints unclipped; keep 2 px clear around it. */
class Os8TextField : public juce::Component,
                     private juce::TextEditor::Listener,
                     private juce::FocusChangeListener
{
public:
    explicit Os8TextField (os8::FieldSize = os8::FieldSize::small);
    ~Os8TextField() override;

    void setText (const juce::String&, juce::NotificationType = juce::dontSendNotification);
    juce::String getText() const { return editor.getText(); }

    /** Make the field numeric: ↑/↓ add or subtract `step` (×10 with ⇧), the result
        is clamped to [minimum, maximum] and shown with `decimals` places. */
    void setNumeric (double step, int decimals, double minimum, double maximum);
    double getValue() const;
    void setValue (double, juce::NotificationType = juce::dontSendNotification);

    /** Called with the field's text when an edit is committed. */
    std::function<void (const juce::String&)> onCommit;

    os8::FieldState currentState() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void enablementChanged() override;

    /** The steps that suit a unit, so "commensurate with the context" is one
        decision made once rather than one per field. */
    enum class Unit { milliseconds, seconds, samples, bytes, percent, hertz, kilohertz, bits, count };
    static double stepFor (Unit) noexcept;
    static int decimalsFor (Unit) noexcept;

private:
    struct Editor : juce::TextEditor
    {
        explicit Editor (Os8TextField& f) : owner (f) {}
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override;
        Os8TextField& owner;
    };

    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void globalFocusChanged (juce::Component*) override;

    bool step (int direction, bool big);
    void commit();
    void restyle();
    juce::String format (double) const;

    os8::FieldSize size;
    Editor editor { *this };
    juce::String committed;

    bool numeric = false;
    double stepSize = 1.0, minValue = 0.0, maxValue = 0.0;
    int decimals = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8TextField)
};

/** "Dropdown" 328:5750 or "mini-dropdown" 502:4002: a popup choice.

    The mini form is the 19 px button plus a short label to its right (the unit:
    "ms"), and its menu lists the long names ("Milliseconds"). */
class Os8Dropdown : public juce::Component
{
public:
    enum class Style { standard, mini };

    explicit Os8Dropdown (Style = Style::standard);

    /** `shortLabels` is only used by the mini style; leave it empty to show the
        item text itself. */
    void setItems (const juce::StringArray& items, const juce::StringArray& shortLabels = {});
    void setSelectedIndex (int, juce::NotificationType = juce::dontSendNotification);
    int getSelectedIndex() const noexcept { return selected; }

    std::function<void (int)> onChange;

    /** For the mini style: the button plus a 4 px gap plus the label. */
    int preferredWidth() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void enablementChanged() override { repaint(); }

    static constexpr int kMiniLabelGap = 4;

private:
    void showMenu();

    Style style;
    juce::StringArray items, shortLabels;
    int selected = -1;
    bool menuOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Dropdown)
};
