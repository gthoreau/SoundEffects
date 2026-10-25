#include "Os8Gallery.h"

#include "Os8.h"

namespace {

constexpr int kMargin = 16;

struct Specimen
{
    os8::TextStyle style;
    const char* name;
};

const Specimen kSpecimens[] = {
    { os8::TextStyle::largeLabel,  "Large Label 14" },
    { os8::TextStyle::headline,    "Headlines 12" },
    { os8::TextStyle::menuItem,    "App Menu items 11" },
    { os8::TextStyle::smallLabel,  "Small Label 10" },
    { os8::TextStyle::xSmallLabel, "X-small label 9" },
};

void sectionTitle (juce::Graphics& g, const juce::String& text, int x, int y)
{
    g.setColour (os8::colour::black);
    os8::drawText (g, text, os8::TextStyle::largeLabel, { (float) x, (float) y, 400.0f, 14.0f });
}

} // namespace

Os8Gallery::Os8Gallery()
{
    liveApply.onClick = [this] { liveApply.flashApplied(); };
    liveCancel.onClick = [this] { liveCancel.flashApplied(); };
    liveRadioA.setRadioGroupId (1);
    liveRadioB.setRadioGroupId (1);
    liveRadioA.setToggleState (true, juce::dontSendNotification);
    liveDisabled.setEnabled (false);

    liveLarge.setNumeric (1.0, 0, 0.0, 999.0);
    liveLarge.setValue (10.0);
    liveSmall.setNumeric (Os8TextField::stepFor (Os8TextField::Unit::percent), 0, 0.0, 200.0);
    liveSmall.setValue (75.0);
    liveXSmall.setNumeric (Os8TextField::stepFor (Os8TextField::Unit::seconds),
                           Os8TextField::decimalsFor (Os8TextField::Unit::seconds), 0.0, 9999.0);
    liveXSmall.setValue (1.456);

    const juce::StringArray units { "Milliseconds", "Seconds", "Samples", "Bytes", "Meters", "Feet" };
    liveUnits.setItems (units);
    liveUnits.setSelectedIndex (0);
    liveMini.setItems ({ "Milliseconds", "Seconds", "Samples" }, { "ms", "s.", "samp" });
    liveMini.setSelectedIndex (0);

    for (auto* c : std::initializer_list<juce::Component*> { &liveApply, &liveCancel, &liveCheck,
                                                            &liveRadioA, &liveRadioB, &liveDisabled,
                                                            &liveLarge, &liveSmall, &liveXSmall,
                                                            &liveUnits, &liveMini })
        addAndMakeVisible (c);

    setSize (920, 680);
}

void Os8Gallery::paint (juce::Graphics& g)
{
    g.fillAll (os8::colour::white);

    int y = kMargin;
    sectionTitle (g, "Type", kMargin, y);
    y += 28;

    for (const auto& s : kSpecimens)
    {
        // Each specimen over a pale line box, so the baseline rule is visible.
        const juce::Rectangle<float> box ((float) kMargin, (float) y, 560.0f, os8::lineHeightOf (s.style));
        g.setColour (os8::colour::selectedTextField);
        g.fillRect (box);
        g.setColour (os8::colour::black);
        os8::drawText (g, juce::String (s.name) + "  -  Apply  Untitled Sound  22.1khz 8bits 2 tracks 4.52s",
                       s.style, box);
        y += 24;
    }

    y += 12;
    sectionTitle (g, "Window themes", kMargin, y);
    y += 28;

    constexpr int kCols = 6, kSwatchW = 140, kSwatchH = 40, kGap = 8;
    for (int i = 0; i < os8::kNumThemes; ++i)
    {
        const juce::Rectangle<int> swatch (kMargin + (i % kCols) * (kSwatchW + kGap),
                                           y + (i / kCols) * (kSwatchH + kGap), kSwatchW, kSwatchH);
        g.setColour (os8::themeColour (i));
        g.fillRect (swatch);
        g.setColour (os8::colour::black);
        g.drawRect (swatch, 1);
        os8::drawText (g, juce::String (i).paddedLeft ('0', 2) + "  " + os8::themeName (i),
                       os8::TextStyle::xSmallLabel, swatch.toFloat().reduced (6.0f, 7.0f));
        os8::drawText (g, "#" + os8::themeColour (i).toDisplayString (false),
                       os8::TextStyle::xSmallLabel, swatch.toFloat().reduced (6.0f, 7.0f).translated (0.0f, 14.0f));
    }

    // UI Elements, laid out as on the Figma page so the two can be compared side by
    // side: buttons in two rows (primary over secondary), then checkboxes and radios
    // in columns, top to bottom in Figma's variant order.
    const auto ui = uiElementsArea();
    sectionTitle (g, "UI elements", ui.getX(), ui.getY() - 28);
    sectionTitle (g, "Live", ui.getX() + 360, ui.getY() - 28);

    const os8::ButtonState states[] = { os8::ButtonState::normal, os8::ButtonState::applied,
                                        os8::ButtonState::active, os8::ButtonState::disabled };
    for (int i = 0; i < 4; ++i)
    {
        const auto text = states[i] == os8::ButtonState::applied ? "Done!" : "Apply";
        os8::paintButton (g, { ui.getX() + 8 + i * 63, ui.getY() + 8, 53, 25 },
                          os8::ButtonKind::primary, states[i], text);
        os8::paintButton (g, { ui.getX() + 10 + i * 63, ui.getY() + 41, 49, 20 },
                          os8::ButtonKind::secondary, states[i], text);
    }

    const struct { bool on, enabled; } toggles[] = { { false, true }, { true, true },
                                                     { false, false }, { true, false } };
    for (int i = 0; i < 4; ++i)
    {
        os8::paintCheckbox (g, { (float) ui.getX() + 272.0f, (float) ui.getY() + 10.0f + (float) i * 18.0f },
                            toggles[i].on, toggles[i].enabled);
        os8::paintRadio (g, { (float) ui.getX() + 296.0f, (float) ui.getY() + 10.0f + (float) i * 18.0f },
                         toggles[i].on, toggles[i].enabled);
    }

    // Text fields: the three sizes down, the four states across; then dropdowns.
    const auto fa = fieldsArea();
    sectionTitle (g, "Fields and dropdowns", fa.getX(), fa.getY() - 28);
    const os8::FieldSize sizes[] = { os8::FieldSize::large, os8::FieldSize::small, os8::FieldSize::xSmall };
    const os8::FieldState fieldStates[] = { os8::FieldState::normal, os8::FieldState::active,
                                            os8::FieldState::highlighted, os8::FieldState::disabled };
    for (int row = 0; row < 3; ++row)
    {
        const auto m = os8::metricsOf (sizes[row]);
        const int w = sizes[row] == os8::FieldSize::large ? 24 : (sizes[row] == os8::FieldSize::small ? 21 : 20);
        for (int col = 0; col < 4; ++col)
        {
            const juce::Rectangle<int> field (fa.getX() + 8 + col * 36, fa.getY() + 6 + row * 28, w, m.height);
            os8::paintTextField (g, field, sizes[row], fieldStates[col]);
            g.setColour (os8::fieldTextColour (sizes[row], fieldStates[col]));
            os8::drawText (g, "10", m.style, { (float) (field.getX() + m.textLeft), (float) (field.getY() + m.textTop), 20.0f, 12.0f });
        }
    }

    const os8::DropdownState dd[] = { os8::DropdownState::normal, os8::DropdownState::active, os8::DropdownState::disabled };
    for (int i = 0; i < 3; ++i)
    {
        os8::paintDropdown (g, { fa.getX() + 170, fa.getY() + 6 + i * 24, 100, os8::kDropdownHeight }, dd[i], "Milliseconds");
        os8::paintMiniDropdown (g, { fa.getX() + 290, fa.getY() + 8 + i * 24 }, dd[i]);
        g.setColour (os8::colour::black);
        os8::drawText (g, "ms", os8::TextStyle::xSmallLabel, { (float) fa.getX() + 313.0f, (float) (fa.getY() + 8 + i * 24) + 1.5f, 30.0f, 9.0f });
    }
}

juce::Rectangle<int> Os8Gallery::fieldsArea()
{
    return { kMargin, 580, 700, 90 };
}

juce::Rectangle<int> Os8Gallery::uiElementsArea()
{
    return { kMargin, 440, 320, 90 };
}

void Os8Gallery::resized()
{
    // The 1-click tray's button row (543:9781), then a radio pair and a disabled box.
    const auto area = uiElementsArea().translated (360, 0);
    liveCheck.setBounds (area.getX(), area.getY() + 14, liveCheck.preferredWidth(), 12);
    liveCancel.setBounds (area.getX() + 131, area.getY() + 10, 56, 20);
    liveApply.setBounds (area.getX() + 197, area.getY() + 8, 53, 25);
    liveRadioA.setBounds (area.getX(), area.getY() + 44, liveRadioA.preferredWidth(), 20);
    liveRadioB.setBounds (area.getX(), area.getY() + 64, liveRadioB.preferredWidth(), 20);
    liveDisabled.setBounds (area.getX() + 120, area.getY() + 44, liveDisabled.preferredWidth(), 20);

    const auto f = fieldsArea().translated (360, 0);
    liveLarge.setBounds (f.getX(), f.getY() + 8, 40, 22);
    liveSmall.setBounds (f.getX() + 56, f.getY() + 11, 30, 16);
    liveXSmall.setBounds (f.getX() + 102, f.getY() + 12, 40, 15);
    liveUnits.setBounds (f.getX() + 160, f.getY() + 10, 100, os8::kDropdownHeight);
    liveMini.setBounds (f.getX() + 276, f.getY() + 12, liveMini.preferredWidth(), os8::kMiniDropdownHeight);
}

bool Os8Gallery::writeSnapshot (const juce::File& png, float scale, juce::Rectangle<int> area)
{
    Os8Gallery gallery;
    const auto image = gallery.createComponentSnapshot (area.isEmpty() ? gallery.getLocalBounds() : area,
                                                        true, scale);

    png.deleteFile();
    juce::FileOutputStream out (png);
    if (! out.openedOk()) return false;

    juce::PNGImageFormat format;
    if (! format.writeImageToStream (image, out)) return false;

    // Text widths beside the image, for checking the type against Figma's own
    // measurements of the same strings (auto-width text nodes report their width).
    struct Probe { os8::TextStyle style; const char* text; };
    const Probe probes[] = {
        { os8::TextStyle::xSmallLabel, "1.45602" }, { os8::TextStyle::xSmallLabel, "2.797.00" },
        { os8::TextStyle::xSmallLabel, "99" },      { os8::TextStyle::xSmallLabel, "1" },
        { os8::TextStyle::smallLabel,  "Resampling" }, { os8::TextStyle::smallLabel, "Milliseconds" },
        { os8::TextStyle::smallLabel,  "Linear - 100%" }, { os8::TextStyle::smallLabel, "Save setup as..." },
        { os8::TextStyle::menuItem,    "New Sound" }, { os8::TextStyle::menuItem, "Extend to Beginning" },
        { os8::TextStyle::menuItem,    "Save Canvas as..." }, { os8::TextStyle::menuItem, "Audio/Midi Setup" },
        { os8::TextStyle::headline,    "Apply" }, { os8::TextStyle::headline, "Untitled Sound" },
        { os8::TextStyle::largeLabel,  "Sound Windows" },
    };
    juce::String report;
    for (const auto& p : probes)
    {
        const auto f = os8::font (p.style);
        report << os8::sizeOf (p.style) << "  " << p.text << "  w="
               << juce::GlyphArrangement::getStringWidth (f, p.text)
               << "  h=" << f.getHeight() << "  asc=" << f.getAscent() << "\n";
    }
    return png.withFileExtension ("txt").replaceWithText (report);
}
