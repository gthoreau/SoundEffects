// SetupSheet — "setups" (1447:9215): Save setup as and Manage Setup, as a sheet that
// slides down from the top of the open effect tray over its settings.
//
// While it is out, a black veil at 25 % covers the rest of the tray (1458:12238) and
// takes the clicks meant for it; nothing else in the app is blocked. It is not modal
// and it is not a window: the Effects panel owns it, lays it over its tray and throws
// it away when the tray goes.

#pragma once

#include "Os8Widgets.h"

#include <functional>

class SetupSheet : public juce::Component,
                   private juce::Timer
{
public:
    enum class Kind { saveAs, manage };

    /** `name` fills the field (the setup being managed; empty for Save setup as).
        `nameTaken` says whether saving under a name would replace another setup,
        which turns Save into Replace. */
    SetupSheet (Kind, const juce::String& name, std::function<bool (const juce::String&)> nameTaken);
    ~SetupSheet() override;

    /** Save (or Replace) with a non-empty, trimmed name; Delete (manage only). Each
        is called as its button is pressed, and the sheet then slides away. */
    std::function<void (const juce::String&)> onSave;
    std::function<void()> onDelete;
    /** The sheet has slid away: the owner deletes it. */
    std::function<void()> onGone;

    /** Over `tray` (the parent's coordinates), then slide down. */
    void layOver (juce::Rectangle<int> tray);
    void slideDown();
    /** Slide back up and be gone; also Cancel and Escape. */
    void dismiss();
    /** Press Save, if it can be pressed (Return from elsewhere in the app). */
    void pressSave();

    Os8TextField& field() noexcept { return nameField; }

    /** For the off-screen snapshots: fully out, at once. */
    void showAtOnce();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kSheetH = 116, kShadow = 2;
    static constexpr int kSlideMs = 150;

private:
    struct Sheet : juce::Component
    {
        explicit Sheet (const juce::String& t) : title (t) {}
        void paint (juce::Graphics&) override;
        juce::String title;
    };

    void timerCallback() override;
    void place();
    void nameChanged();
    void layoutButtons();

    Kind kind;
    std::function<bool (const juce::String&)> taken;
    Sheet sheet;
    Os8TextField nameField { os8::FieldSize::small };
    Os8Button save { "Save", os8::ButtonKind::primary };
    Os8Button cancel { "Cancel", os8::ButtonKind::secondary };
    Os8Button remove { "Delete", os8::ButtonKind::secondary };
    int trayH = 0;
    float out = 0.0f;               // 0 hidden above the tray, 1 fully down
    bool leaving = false;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupSheet)
};
