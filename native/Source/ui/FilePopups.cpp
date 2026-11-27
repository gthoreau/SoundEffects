#include "FilePopups.h"

#include "Os8CheckList.h"
#include "engine/SampleFormat.h"

namespace filepopups {

namespace {

/** Fit `text` into `width` px, ending in an ellipsis when it must be cut. */
juce::String fitted (juce::String text, int width, os8::TextStyle style)
{
    if (os8::widthOf (text, style) <= width) return text;
    while (text.length() > 1
           && os8::widthOf (text + juce::String::fromUTF8 ("\xE2\x80\xA6"), style) > width)
        text = text.dropLastCharacters (1);
    return text + juce::String::fromUTF8 ("\xE2\x80\xA6");
}

} // namespace

juce::String summaryOf (const se::WindowState& s)
{
    size_t longest = 0;
    for (const auto& t : s.tracks) longest = std::max (longest, t.data.size());
    const double seconds = s.rateHz > 0.0
        ? (double) (longest / (size_t) std::max (1, se::frameSizeFor (s.bps))) / s.rateHz : 0.0;

    return juce::String (s.tracks.size()) + (s.tracks.size() == 1 ? " track" : " tracks")
         + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (s.rateHz / 1000.0, 1) + " kHz"
         + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (s.bps) + " bit"
         + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (seconds, 2) + " s";
}

std::unique_ptr<Os8Popup> exportSound (const se::WindowState& sound)
{
    auto popup = std::make_unique<Os8Popup> ("Export Sound", 62);
    auto& p = *popup;
    p.addLabel ("Save this Sound as a .sefx file of its own:", 0.0f, 4.0f, os8::TextStyle::smallLabel);
    p.addLabel (fitted (juce::String (sound.name), Os8Popup::kContentW, os8::TextStyle::smallLabel), 0.0f, 22.0f, os8::TextStyle::smallLabel);
    p.addLabel (summaryOf (sound), 0.0f, 36.0f, os8::TextStyle::smallLabel);
    p.addLabel ("It opens like any canvas, audio included.", 0.0f, 50.0f, os8::TextStyle::smallLabel);
    p.setButtons ("Cancel", juce::String::fromUTF8 ("Export\xE2\x80\xA6"));
    return popup;
}

std::unique_ptr<Os8Popup> importSound (const juce::String& fileName,
                                       const std::vector<se::WindowState>& sounds,
                                       std::function<void (std::vector<size_t>)> doImport)
{
    const int count = (int) sounds.size();
    const bool several = count > 1;

    std::vector<Os8CheckList::Item> items;
    for (const auto& s : sounds)
        items.push_back ({ juce::String::fromUTF8 (s.name.c_str()),
                           { juce::String (s.tracks.size()) + (s.tracks.size() == 1 ? " track" : " tracks"),
                             juce::String (s.rateHz / 1000.0, 1) + " kHz" },
                           ! several });

    const int listTop = several ? 52 : 28;
    const int listH = Os8CheckList::heightFor (count, 180);
    constexpr int kContent = 322;

    auto popup = std::make_unique<Os8Popup> ("Import Sound", listTop + listH + 4, kContent);
    auto& p = *popup;
    // A long file name is cut in the middle of the sentence, which keeps its end.
    const juce::String lead = "Select sound(s) from ", tail = ":";
    const int room = kContent - os8::widthOf (lead + tail, os8::TextStyle::headline);
    p.addLabel (lead + fitted (fileName, room, os8::TextStyle::headline) + tail, 0.0f, 4.0f);

    auto& go = p.setButtons ("Cancel", "Import");

    Os8Toggle* all = nullptr;
    if (several)
    {
        auto t = std::make_unique<Os8Toggle> ("Select all", Os8Toggle::Shape::checkbox, os8::TextStyle::headline);
        t->setWantsKeyboardFocus (false);
        t->setMouseClickGrabsKeyboardFocus (false);
        all = p.addContent (std::move (t), { 5, 32, 0, os8::kToggleSize });
        all->setSize (all->preferredWidth(), os8::kToggleSize);
    }

    auto* list = p.addContent (std::make_unique<Os8CheckList> (std::move (items), std::vector<int> { 235, 308 }),
                               { 0, listTop, kContent, listH });

    auto* goButton = &go;
    auto update = [list, all, goButton]
    {
        if (all != nullptr) all->setToggleState (list->numTicked() == list->size(), juce::dontSendNotification);
        goButton->setEnabled (list->numTicked() > 0);
    };
    list->onChange = update;
    if (all != nullptr)
        all->onClick = [list, all, update]
        {
            list->setAllTicked (all->getToggleState());
            update();
        };
    update();

    p.onDismiss = [list, doImport] (int result)
    {
        if (result != 1 || ! doImport) return;
        std::vector<size_t> picked;
        for (int i : list->ticked()) picked.push_back ((size_t) i);
        if (! picked.empty()) doImport (std::move (picked));
    };
    return popup;
}

} // namespace filepopups
