#include "FilePopups.h"

#include "Os8CheckList.h"

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

std::unique_ptr<Os8Popup> exportSound (std::function<void (ExportFormat)> doExport)
{
    constexpr int kContent = 322;
    auto popup = std::make_unique<Os8Popup> ("Export Sound", 25, kContent);
    auto& p = *popup;
    p.addLabel ("Export format:", 0.0f, 6.0f);

    auto menu = std::make_unique<Os8Dropdown> (Os8Dropdown::Style::standard);
    menu->setItems ({ ".SEFX file", ".WAV stereo mixdown", ".WAV mono stems" });
    menu->setSelectedIndex ((int) ExportFormat::mixdown);
    menu->setWantsKeyboardFocus (false);
    auto* format = p.addContent (std::move (menu), { 107, 4, 162, os8::kDropdownHeight });

    p.setButtons ("Cancel", "Export...");
    p.onDismiss = [format, doExport] (int result)
    {
        if (result == 1 && doExport) doExport ((ExportFormat) format->getSelectedIndex());
    };
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
