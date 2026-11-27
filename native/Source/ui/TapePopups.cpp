#include "TapePopups.h"

#include "Os8CheckList.h"

namespace tapepopups {

juce::String durationText (size_t frames, double rate)
{
    const double s = rate > 0.0 ? (double) frames / rate : 0.0;
    const int cs = (int) std::floor (s * 100.0 + 1.0e-6);   // 32.23 s, not 32.22999
    const auto two = [] (int v) { return juce::String (v).paddedLeft ('0', 2); };
    return two (cs / 6000) + ":" + two ((cs / 100) % 60) + ":" + two (cs % 100) + " s";
}

std::unique_ptr<Os8Popup> sendToCanvas (const se::Tape& tape,
                                        std::function<void (const SendChoice&)> send)
{
    const int count = tape.numTakes();
    const bool several = count > 1;
    const double rate = tape.rate();

    std::vector<Os8CheckList::Item> items;
    for (int i = 0; i < count; ++i)
        items.push_back ({ "Take " + juce::String (i + 1),
                           { durationText (tape.takeEnd (i) - tape.takeStart (i), rate),
                             tape.takes()[(size_t) i].sent ? "sent" : "" },
                           false });

    // 1447:9158: "Select all" at 68, the list under it, both one row higher without it.
    const int listTop = several ? 92 : 68;
    const int listH = Os8CheckList::heightFor (count, 120);
    const int cleanTop = listTop + listH + 8;

    auto popup = std::make_unique<Os8Popup> ("Send to Canvas", cleanTop + 16);
    auto& p = *popup;
    p.addLabel ("Send:", 0.0f, 4.0f);
    auto& go = p.setButtons ("Cancel", "Send");

    const auto addToggle = [&p] (const juce::String& text, Os8Toggle::Shape shape, int x, int y,
                                 os8::TextStyle style = os8::TextStyle::headline)
    {
        auto t = std::make_unique<Os8Toggle> (text, shape, style);
        t->setWantsKeyboardFocus (false);
        t->setMouseClickGrabsKeyboardFocus (false);
        auto* raw = p.addContent (std::move (t), { x, y, 0, os8::kToggleSize });
        raw->setSize (raw->preferredWidth(), os8::kToggleSize);
        return raw;
    };
    auto* whole = addToggle ("Whole tape (" + durationText (tape.length(), rate) + ")",
                             Os8Toggle::Shape::radio, 5, 24);
    auto* takes = addToggle ("Individual takes", Os8Toggle::Shape::radio, 5, 44);
    whole->setRadioGroupId (1);
    takes->setRadioGroupId (1);
    Os8Toggle* all = several ? addToggle ("Select all", Os8Toggle::Shape::checkbox, 5, 72) : nullptr;

    auto* list = p.addContent (std::make_unique<Os8CheckList> (std::move (items), std::vector<int> { 238, 160 }),
                               { 0, listTop, p.contentWidth(), listH });

    // Checked at every opening.
    auto* clean = addToggle ("Clean conversion", Os8Toggle::Shape::checkbox, 0, cleanTop,
                             os8::TextStyle::smallLabel);
    clean->setToggleState (true, juce::dontSendNotification);
    whole->setToggleState (true, juce::dontSendNotification);

    auto* goButton = &go;
    auto update = [list, all, takes, goButton]
    {
        if (all != nullptr) all->setToggleState (list->numTicked() == list->size(), juce::dontSendNotification);
        goButton->setEnabled (! takes->getToggleState() || list->numTicked() > 0);
    };
    whole->onClick = update;
    takes->onClick = update;
    // Ticking a take, or all of them, means "Individual takes".
    list->onChange = [takes, update]
    {
        takes->setToggleState (true, juce::sendNotificationSync);
        update();
    };
    if (all != nullptr)
        all->onClick = [list, all, takes, update]
        {
            list->setAllTicked (all->getToggleState());
            takes->setToggleState (true, juce::sendNotificationSync);
            update();
        };
    update();

    p.onDismiss = [list, whole, clean, send] (int result)
    {
        if (result != 1 || ! send) return;
        SendChoice c;
        c.whole = whole->getToggleState();
        c.clean = clean->getToggleState();
        c.takes = list->ticked();
        if (c.whole || ! c.takes.empty()) send (c);
    };
    return popup;
}

} // namespace tapepopups
