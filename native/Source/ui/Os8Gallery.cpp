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
    setSize (720, 520);
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
}

void Os8Gallery::resized() {}

bool Os8Gallery::writeSnapshot (const juce::File& png, float scale)
{
    Os8Gallery gallery;
    const auto image = gallery.createComponentSnapshot (gallery.getLocalBounds(), true, scale);

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
