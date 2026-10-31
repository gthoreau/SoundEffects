// EffectsPanel — the destructive effects, down the right-hand side, as the Figma
// "Effects" side panel (636:14867) draws them.
//
// From the top: the Sound-info header (636:15319), then six groups of buttons
// (Effects-group 527:5980). A button either applies at once (a parameterless effect,
// with Auto-apply on) or opens that effect's settings tray, which is inserted under
// its group and pushes the groups below it down. Only one tray is ever open.
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
// ## Where each control sits
//
// The tray's controls come from the effect's schema (§7.5) for their keys, units and
// ranges, and from EffectLayouts for their position: the design draws every effect's
// tray by hand, and a pixel-perfect port follows it.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AppPrefs.h"
#include "EffectLayouts.h"
#include "Os8.h"
#include "Os8Popup.h"
#include "Os8Widgets.h"
#include "effects/EffectRunner.h"
#include "effects/Module.h"
#include "effects/Registry.h"
#include "effects/Setups.h"
#include "engine/Engine.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class EffectsPanelComponent : public juce::Component,
                              private AppPrefs::Listener
{
public:
    explicit EffectsPanelComponent (se::Engine& engine);
    ~EffectsPanelComponent() override;

    /** The canvas's selected Window, 0 for none. Cheap to call repeatedly. */
    void setTargetWindow (uint64_t windowId);

    /** Re-read what the target Window looks like now (its selection may have been
        dragged, its audio edited). Driven by the editor's timer. */
    void refresh();

    /** For the snapshot tool: show the Save Setup popup with `text` typed (or the
        Delete Setup popup for the setup named `text`) over the open tray, not modally,
        and say where. clearSnapshotPopups() takes them away again. */
    juce::Rectangle<int> snapshotSetupPopup (bool deleting, const juce::String& text);
    void clearSnapshotPopups();

    /** Say what an effect run did, in the header's status line. Public because the
        Keyboard palette runs effects too and has nowhere to put an outcome. */
    void showResult (const juce::String& message);

    /** An effect rewrote `windowId`'s audio — the canvas must drop that Window's
        cached waveform peaks, which are keyed on LENGTH and so cannot notice an
        effect that rewrote every sample in place. Any spawned documents have
        already become Windows by the time this fires. */
    std::function<void (uint64_t windowId)> onDocumentChanged;

    /** Run the open effect, as Apply does. Returns false when there is nothing to
        run — no tray open, or one that cannot run on the current target — so a
        Return pressed elsewhere in the editor keeps travelling.

        This is the keyboard's way in: a form you have just finished typing into
        should end with the key that ends every other form. It stays irreversible
        either way (§1), which is why an inapplicable effect refuses here exactly as
        its greyed button does. */
    bool applyFromKeyboard();

    /** Open the tray of the module called `moduleName` (as the registry names it) and
        say where it sits, for the snapshot tool; empty if there is no such effect. */
    juce::Rectangle<int> openEffectNamed (const juce::String& moduleName);

    /** Where group `index` and the header sit, in this component's coordinates. */
    juce::Rectangle<int> groupFrame (int index) const;
    int numGroups() const noexcept { return (int) groups.size(); }

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content;
    friend class Content;

    struct Entry;
    struct Group;

    void appPrefChanged (const juce::String& key) override;

    void rebuildButtons();
    void layoutAll();
    void openEffect (const se::EffectModule* module);
    void closeTray();
    void updateLatchedButtons();
    /** A group button was pressed. */
    void effectClicked (Entry&);
    void applyOpen();
    /** Run `module` with `settings` and say what happened. */
    bool runModule (const se::EffectModule& module, const se::Settings& settings);
    void updateStatus();

    // --- the open tray's controls (EffectLayouts says where, the schema says what) ---
    struct TrayPart
    {
        std::unique_ptr<juce::Component> comp;
        juce::Rectangle<int> rel;              // inside the tray's content box
    };
    void buildTray();
    void clearTray();
    void refreshTray();
    void placeTray();
    /** Is the control gated on `key` live? An empty key always is. */
    bool gateOn (const std::string& key) const;
    const se::SchemaItem* schemaItem (const std::string& key) const;
    void prefillSpanFields();
    /** The opening values the design gives a tray where it depends on the Sound:
        Waveform Generator's length is the selection's when there is one. */
    void seedFromSelection();
    /** Setup ▸ Custom: the tray's opening values again. */
    void resetToDefaults();

    // --- the Setup: popup (disassembly/Setups.md) ---
    /** The target's rate and sample size, or the presets' 44.1 kHz 16-bit without one. */
    std::pair<double, int> targetFormat() const;
    /** Which saved setup `working` is, or -1 for "Custom". */
    int currentSetup() const;
    void loadSetups();
    void storeSetups();
    void refreshSetupMenu (Os8Dropdown&);
    void setupMenuPicked (int item);
    void chooseSetup (int index);
    void saveSetupAs();
    void deleteSetup (int index);
    /** Where the Save / Delete popups go: over the tray, just under its Setup row. */
    juce::Point<int> setupPopupOrigin() const;
    std::unique_ptr<Os8Popup> makeSavePopup (Os8TextField*& field);
    /** The saved setup called `name`, or -1. */
    int setupNamed (const juce::String& name) const;
    std::unique_ptr<Os8Popup> makeDeletePopup (const std::string& name);

    const se::SoundDocument* targetDoc() const;

    se::Engine& engine;
    se::EffectRunner runner;

    uint64_t targetId = 0;

    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    std::vector<std::unique_ptr<Group>> groups;

    const se::EffectModule* openModule = nullptr;
    se::Settings working;            // the open module's live settings
    std::unique_ptr<Os8Button> applyButton, cancelButton;
    std::vector<TrayPart> trayParts;
    std::vector<std::function<void()>> trayRefreshers;
    se::SettingsSchema openSchema;
    fxlayout::Layout openLayout;             // where the open tray's controls sit
    bool openIsOneClick = false;             // the "Apply X." tray of a parameterless effect
    std::vector<juce::String> warningLines;  // Weird Tempo's apology, wrapped to the tray
    std::set<std::string> presetKeys;        // keys the author's starting values (or last time's) set
    std::set<std::string> touchedKeys;       // scope-defaulted fields the user typed over
    bool reformatFields = false;             // a commit is settling: re-seat every field's text
    bool allowExtend = false;                // answers AskDoExtend() for modules that grow
    std::map<std::string, se::Settings> remembered;   // last-used values, per module, this session
    std::vector<se::Setup> openSetups;       // the open module's saved setups (empty: it has none)
    se::SetupKeys setupKeys;                 // what one of them holds
    bool openHasSetups = false;              // the design gives this tray a Setup row
    int preferredSetup = -1;                 // the one last chosen or saved, should another match too
    std::vector<int> setupMenuItems;         // what each popup item does: a setup's index, or one of below
    static constexpr int kSetupCustom = -1, kSetupSave = -2, kSetupDelete = -3;
    se::Applicability openApplicability = se::Applicability::NoWindow;

    // What the header's right half says: the scope Apply would eat, or — after a run —
    // what the run did. Kept apart because the 30 Hz refresh recomputes the scope, and
    // an outcome the user never gets to read is the same as no outcome at all.
    juce::String scopeLine, resultLine;

    // Last state the scope was computed from, so refresh() can skip the work when
    // nothing the readout depends on has moved.
    uint64_t lastScopeWindow = 0;
    size_t   lastSelSt = 0, lastSelEnd = 0, lastMaxSize = 0;
    int      lastFirstTrack = 0, lastLastTrack = 0, lastNumTracks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanelComponent)
};
