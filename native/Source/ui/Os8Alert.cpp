#include "Os8Alert.h"

#include "AppPrefs.h"

namespace os8alert {

namespace {

constexpr int kWidth = 350, kInset = 14, kTextW = kWidth - 2 * kInset;
constexpr int kLine = 15;       // a Headlines paragraph's line pitch
constexpr int kSpacer = 8;

/** `text` broken into lines no wider than the alert: at its own newlines, at spaces,
    and inside a word only when the word alone does not fit (a long path). */
juce::StringArray wrapped (const juce::String& text)
{
    const auto fits = [] (const juce::String& s) { return os8::widthOf (s, os8::TextStyle::headline) <= kTextW; };
    juce::StringArray lines;
    for (const auto& paragraph : juce::StringArray::fromLines (text))
    {
        juce::String line;
        for (auto word : juce::StringArray::fromTokens (paragraph, " ", {}))
        {
            if (word.isEmpty()) continue;
            const auto next = line.isEmpty() ? word : line + " " + word;
            if (fits (next)) { line = next; continue; }
            if (line.isNotEmpty()) lines.add (line);
            while (! fits (word))
            {
                int n = word.length() - 1;
                while (n > 1 && ! fits (word.substring (0, n))) --n;
                lines.add (word.substring (0, n));
                word = word.substring (n);
            }
            line = word;
        }
        lines.add (line);
    }
    return lines;
}

Os8Popup* show (juce::Component& over, std::unique_ptr<Os8Popup> popup)
{
    auto* p = popup.release();
    p->showOver (over, { (over.getWidth() - p->getWidth()) / 2, (over.getHeight() - p->getHeight()) / 2 });
    return p;
}

} // namespace

std::unique_ptr<Os8Popup> make (const Question& q, std::function<void()> go)
{
    const auto heading = wrapped (q.heading), message = wrapped (q.message);
    const int messageTop = 20 + (heading.size() - 1) * kLine + kSpacer;
    const int rows = messageTop + message.size() * kLine;

    auto popup = std::make_unique<Os8Popup> ("Alert", rows, kTextW, kInset);
    auto& p = *popup;
    p.setAlertLook();

    for (int i = 0; i < heading.size(); ++i) p.addLabel (heading[i], 0.0f, 4.0f + (float) (i * kLine));
    for (int i = 0; i < message.size(); ++i) p.addLabel (message[i], 0.0f, (float) (messageTop + 1 + i * kLine));

    if (q.action.isEmpty())
    {
        p.setButtons ({}, "OK", 1);
        return popup;
    }

    Os8Toggle* hide = nullptr;
    if (q.hiddenKey.isNotEmpty())
    {
        auto box = std::make_unique<Os8Toggle> (juce::String::fromUTF8 ("Don\xe2\x80\x99t show again"),
                                                Os8Toggle::Shape::checkbox);
        box->setWantsKeyboardFocus (false);
        box->setMouseClickGrabsKeyboardFocus (false);
        const int w = box->preferredWidth();
        hide = p.addContent (std::move (box), { 0, rows + kSpacer + 11, w, os8::kToggleSize });
    }

    // Cancel is the primary button: the action is the one that has to be aimed at.
    p.setButtons (q.action, "Cancel", 1);
    p.onDismiss = [hide, key = q.hiddenKey, then = std::move (go)] (int result)
    {
        if (result != 0) return;
        // `hide` belongs to the popup, which outlives this call.
        if (hide != nullptr && hide->getToggleState()) AppPrefs::get().setAlertHidden (key);
        if (then) then();
    };
    return popup;
}

Os8Popup* ask (juce::Component& over, const Question& q, std::function<void()> go)
{
    if (q.hiddenKey.isNotEmpty() && AppPrefs::get().alertHidden (q.hiddenKey))
    {
        if (go) go();
        return nullptr;
    }
    return show (over, make (q, std::move (go)));
}

Os8Popup* tell (juce::Component& over, const juce::String& heading, const juce::String& message)
{
    return show (over, make ({ heading, message, {} }, {}));
}

} // namespace os8alert
