// EffectsPanel — the destructive effects, down the right-hand side.
//
// The list is the recovered Effects Folder (§7.1): top-level items A–Z, then each
// group in its recovered order. Clicking an effect expands its parameters inline
// beneath it; Apply runs it. Only one effect is ever open at a time — this is a
// column ~250px wide, and two open forms would mean neither is readable.
//
// ## What it acts on
//
//   - no Window selected, or the Window has no audio → everything is greyed;
//   - a Window selected, nothing selected inside it   → all the audio in the Window;
//   - audio selected inside the Window                → just that selection.
//
// The middle rule is not implemented here. An empty selection has always meant
// everything (Gain.c), so the effects skeleton already does it; EffectRunner only
// widens the TRACK range to match. See EffectRunner.h.
//
// ## Why the scope readout exists
//
// There is no undo and there must never be one (§1). Apply is final, so the panel
// states what it is about to eat — which tracks, how much audio — immediately next
// to the button that eats it.
//
// The parameter forms are generated from each module's schema (§7.5), not written
// per module. That indirection is the whole point of the original's ModSetups.h:
// a module hands the host an item list and the host runs the dialog. One panel,
// 36 dialogs.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Sys7.h"
#include "effects/EffectRunner.h"
#include "effects/Module.h"
#include "effects/Registry.h"
#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <vector>

/**
 * One parameter control, generated from a schema item.
 *
 * Paints its own label and, depending on the item type, either hosts a text editor
 * or hand-paints a checkbox / radio stack / units popup. One class rather than
 * five because the original is an array of identical ItemInfoRecs where the type
 * tag decides which fields mean anything, and a panel that walks it wants exactly
 * that shape.
 */
class ParamRow : public juce::Component
{
public:
    ParamRow (const se::SchemaItem& item, se::Settings& settings, const se::SoundDocument* doc);

    /** How tall this item needs to be — a radio stack grows with its options. */
    static int heightFor (const se::SchemaItem&);

    /** Value changed; the panel refreshes anything derived (the units readout). */
    std::function<void()> onChanged;

    /** Re-read the settings into the control, after a units change rescales a
        duration field. */
    void refreshFromSettings();

    /** Push whatever is sitting in the editor into the settings. Apply calls this:
        clicking the button moves focus and so commits on its own, but relying on
        that would make a typed value depend on how it lost focus. */
    void commitPendingText() { commitText(); }

    /** The trailing text a DITL puts beside a field ("%", "Hz"). Kept on the field
        rather than on its own line, which is where the schema's separate Ignore
        item would otherwise put it. */
    void setSuffix (const std::string& text);

    /** Would `text` actually fit beside this field? A unit does; a sentence does
        not, and would be silently clipped. */
    bool canFitSuffix (const std::string& text) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> controlBounds() const;
    juce::Rectangle<int> optionBounds (int index) const;
    void commitText();

    se::SchemaItem item;
    juce::String suffix;
    se::Settings& settings;
    const se::SoundDocument* doc;   // for unit conversion; may be null

    std::unique_ptr<juce::TextEditor> editor;

    static constexpr int kLabelW = 104;
    static constexpr int kFieldH = 18;
    static constexpr int kOptionH = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRow)
};

class EffectsPanelComponent : public juce::Component
{
public:
    explicit EffectsPanelComponent (se::Engine& engine);
    ~EffectsPanelComponent() override;

    /** The panel never draws wider than this. */
    static constexpr int kMaxWidth = 250;

    /** The canvas's selected Window, 0 for none. Cheap to call repeatedly. */
    void setTargetWindow (uint64_t windowId);

    /** Re-read what the target Window looks like now (its selection may have been
        dragged, its audio edited). Driven by the editor's timer. */
    void refresh();

    /** An effect rewrote `windowId`'s audio — the canvas must drop that Window's
        cached waveform peaks, which are keyed on LENGTH and so cannot notice an
        effect that rewrote every sample in place. Any spawned documents have
        already become Windows by the time this fires. */
    std::function<void (uint64_t windowId)> onDocumentChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** One line in the scrolling list: a group heading, or an effect. */
    struct Row
    {
        enum class Kind { GroupHeader, Effect } kind = Kind::Effect;
        const se::EffectModule* module = nullptr;
        juce::String label;
        int y = 0;
        int height = 0;
        /** Set for effects that cannot run on the current target. */
        se::Applicability applicability = se::Applicability::Ok;
    };

    /** The list's content — painted here rather than as one component per row, so
        group headings and the expanded form are laid out in one pass. */
    class RowList : public juce::Component
    {
    public:
        explicit RowList (EffectsPanelComponent& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        EffectsPanelComponent& owner;
        int hoverRow = -1;
    };

    void rebuildRows();
    void layoutRows();
    void openEffect (const se::EffectModule* module);
    void buildParamControls();
    void applyEffect();
    void updateScope();

    int rowAt (int y) const;
    /** Height of the expanded parameter form under the open effect, 0 if none. */
    int formHeight() const;

    const se::SoundDocument* targetDoc() const;

    se::Engine& engine;
    se::EffectRunner runner;

    Sys7LookAndFeel lookAndFeel;   // declared first: outlives every child using it

    uint64_t targetId = 0;

    std::vector<Row> rows;
    const se::EffectModule* openModule = nullptr;
    se::Settings working;             // the open module's live settings
    bool allowExtend = false;         // answers AskDoExtend() for modules that grow

    juce::Viewport viewport;
    RowList rowList { *this };
    std::vector<std::unique_ptr<ParamRow>> params;
    std::unique_ptr<Sys7Button> applyButton;
    std::unique_ptr<juce::ToggleButton> extendToggle;

    // What the header and the scope line say now.
    juce::String scopeLine;
    juce::String statusLine;
    // What the last Apply did. Kept apart from statusLine because the 30 Hz
    // refresh recomputes the scope — and an outcome the user never gets to read
    // is the same as no outcome at all.
    juce::String resultLine;
    se::Applicability openApplicability = se::Applicability::NoWindow;

    // Last state the scope was computed from, so refresh() can skip the work when
    // nothing the readout depends on has moved.
    uint64_t lastScopeWindow = 0;
    size_t   lastSelSt = 0, lastSelEnd = 0, lastMaxSize = 0;
    int      lastFirstTrack = 0, lastLastTrack = 0, lastNumTracks = 0;

    static constexpr int kPad = 4;
    static constexpr int kHeaderH = 15;
    static constexpr int kScopeH = 28;
    static constexpr int kRowH = 18;
    static constexpr int kGroupH = 17;
    static constexpr int kButtonH = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanelComponent)
};
