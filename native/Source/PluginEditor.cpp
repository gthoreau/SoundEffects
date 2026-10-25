#include "PluginEditor.h"

#include "engine/Import.h"
#include "ui/AppCommands.h"
#include "ui/AppPrefs.h"
#include "ui/Os8Icons.h"

namespace {
/** How much canvas stays visible below the floating view palette. */
constexpr int kPaletteBottomGap = 25;
/** ...and beside it, before it starts giving width back to the viewport. */
constexpr int kPaletteSideGap = 12;
/** Between the two floating strips. Small: they are one stack, not two palettes. */
constexpr int kPaletteStackGap = 6;
}

SoundEffectsAudioProcessorEditor::SoundEffectsAudioProcessorEditor (SoundEffectsAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p),
      topBar (*this, p.wrapperType == juce::AudioProcessor::wrapperType_Standalone),
      canvas (p.engine()), mixer (p.engine()), effects (p.engine()), keyboard (p.engine())
{
    formatManager.registerBasicFormats();   // WAV, AIFF, and more

    addAndMakeVisible (topBar);
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setApplicationCommandManagerToWatch (&commandManager);   // menus follow greying

    addAndMakeVisible (canvas);
    addAndMakeVisible (mixerPanel);
    mixerPanel.setTitleRule (true);
    addAndMakeVisible (effectsPanel);

    // Each panel shades to its title bar from the box in that bar, and stays so.
    mixerPanel.setCollapsed (AppPrefs::get().mixerCollapsed(), juce::dontSendNotification);
    effectsPanel.setCollapsed (AppPrefs::get().effectsCollapsed(), juce::dontSendNotification);
    mixerPanel.onCollapsedChanged = [this] (bool c) { AppPrefs::get().setMixerCollapsed (c); resized(); };
    effectsPanel.onCollapsedChanged = [this] (bool c) { AppPrefs::get().setEffectsCollapsed (c); resized(); };
    addAndMakeVisible (palette);   // last, so it floats over the canvas
    addChildComponent (keyboard);  // ...and this one starts hidden

    // The effects panel acts on the canvas's SELECTED Window — not the focused
    // one. Clicking into the panel takes keyboard focus away from the Window, and
    // what the panel operates on has to survive that.
    // The Keyboard follows the SAME selection. That is what makes it an always-on
    // effect rather than a dialog: it is aimed at whatever Window is selected at the
    // moment you press a key, so clicking between Windows re-aims it.
    canvas.onSelectionChanged = [this]
    {
        effects.setTargetWindow (canvas.selectedWindowId());
        mixer.setSelectedWindow (canvas.selectedWindowId());
        keyboard.setTargetWindow (canvas.selectedWindowId());
    };
    effects.setTargetWindow (canvas.selectedWindowId());
    mixer.setSelectedWindow (canvas.selectedWindowId());
    keyboard.setTargetWindow (canvas.selectedWindowId());

    // The mixer is a list OF the Windows: clicking a channel selects its Window and
    // brings it to the front — and, if it had been scrolled off the board, back into
    // view. Selection flows the other way through onSelectionChanged above, so the
    // framed strip and the framed Window are always the same one.
    mixer.onChannelClicked = [this] (uint64_t id) { canvas.focusWindow (id); };

    // An effect rewrites the audio IN PLACE, so the Window's cached waveform peaks
    // — keyed on track length — cannot tell anything happened. Drop them
    // explicitly. There is no undo (§1); this is the only redraw there is.
    effects.onDocumentChanged = [this] (uint64_t id)
    {
        canvas.invalidateWaveform (id);
        canvas.refresh();
    };

    // The Keyboard's Apply is an effect run like any other, so it reports where
    // every other effect reports — the panel's result line — rather than growing a
    // status line of its own on a strip that has no room for one.
    keyboard.onDocumentChanged = [this] (uint64_t id)
    {
        canvas.invalidateWaveform (id);
        canvas.refresh();
    };
    keyboard.onResult = [this] (const juce::String& message) { effects.showResult (message); };

    // Two ways in, one state: the palette's toggle and the Keyboard row in the
    // effects list. The row SHOWS rather than toggles — clicking an effect's name
    // has never meant "put that away".
    palette.onKeyboardToggled = [this] (bool shown) { setKeyboardShown (shown); };
    effects.onShowKeyboard = [this] { setKeyboardShown (true); };

    // New / Open live at the top of the mixer panel, not in any toolbar.
    mixer.onNew  = [this] { newWindow(); };
    mixer.onOpen = [this] { openFile(); };

    // A Window's record button is greyed — recording is not implemented — and
    // clicking it asks for the audio device settings instead. Only the standalone
    // app can serve that, so it forwards to a hook the app installs.
    canvas.onAudioSettingsRequested = [this] { if (onShowAudioSettings) onShowAudioSettings(); };

    // The palette and the canvas are two faces of one state: the palette asks, the
    // canvas decides — it clamps the zoom to its own range — and what it settled on
    // comes straight back to the palette's readouts, whether the palette asked for it
    // or a pinch did. Nothing loops: setZoom / setMosaicMode on the palette are silent.
    palette.onMosaicModeChanged = [this] (bool grid) { canvas.setMosaicMode (grid); };
    palette.onZoomChanged       = [this] (float z)   { canvas.setZoom (z); };
    palette.onZoomReset         = [this]             { canvas.setZoom (1.0f); };

    canvas.onViewChanged = [this]
    {
        palette.setMosaicMode (canvas.isMosaicMode());
        palette.setZoom (canvas.zoomLevel());
    };
    palette.setMosaicMode (canvas.isMosaicMode());
    palette.setZoom (canvas.zoomLevel());

    setSize (960, 600);
    setResizable (true, true);
    setWantsKeyboardFocus (true);   // so ⌘N / ⌘O still land with no Window focused
    startTimerHz (30);
}

bool SoundEffectsAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    // Return applies the open effect from anywhere in the editor — the usual move is
    // to drag a selection in a Window and then run the form that is already open, and
    // that leaves focus on the Window, not on the panel. It only fires when there IS
    // something runnable; otherwise the key falls through untouched. A Window being
    // renamed keeps its own Return (WindowComponent's name editor consumes it).
    return k.getKeyCode() == juce::KeyPress::returnKey
        && ! k.getModifiers().isAnyModifierKeyDown()
        && effects.applyFromKeyboard();
}

// --- commands ---------------------------------------------------------------------

namespace {
// Answered by the standalone when it hosts us; greyed placeholders in a plugin.
const juce::CommandID kHostCommands[] = { cmd::newCanvas, cmd::openCanvas, cmd::saveCanvas,
                                          cmd::saveCanvasAs, cmd::revertCanvas, cmd::audioSettings };
}

void SoundEffectsAudioProcessorEditor::setHostCommandTarget (juce::ApplicationCommandTarget* t)
{
    hostTarget = t;
    commandManager.clearCommands();
    commandManager.registerAllCommandsForTarget (this);
    if (hostTarget != nullptr) commandManager.registerAllCommandsForTarget (hostTarget);
    commandManager.commandStatusChanged();
}

void SoundEffectsAudioProcessorEditor::setCanvasName (const juce::String& name)
{
    topBar.setCanvasName (name);
}

bool SoundEffectsAudioProcessorEditor::isTyping()
{
    return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
}

void SoundEffectsAudioProcessorEditor::getAllCommands (juce::Array<juce::CommandID>& c)
{
    for (const auto& spec : cmd::table())
    {
        const bool hostOnly = std::find (std::begin (kHostCommands), std::end (kHostCommands), spec.id)
                              != std::end (kHostCommands);
        if (! hostOnly || hostTarget == nullptr)
            c.add (spec.id);
    }
}

void SoundEffectsAudioProcessorEditor::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& r)
{
    cmd::fill (r, id);

    auto* w = targetWindow();
    const auto can = [w] (WindowComponent::Action a) { return w != nullptr && w->canPerform (a); };
    auto* engineWindow = processorRef.engine().windowById (canvas.selectedWindowId());

    // Single keys are letters a text field wants; while one is being typed in, they
    // stand down (and a disabled command lets the key through to the field).
    const bool singleKey = ! r.defaultKeypresses.isEmpty()
                        && ! r.defaultKeypresses.getReference (0).getModifiers().isAnyModifierKeyDown();
    const bool typing = singleKey && isTyping();

    bool active = false;
    if (const int slot = cmd::slotOf (id); slot >= 0)
    {
        const auto sid = processorRef.engine().windowInSlot (slot);
        r.setTicked (sid != 0 && sid == canvas.selectedWindowId());
        r.setActive (sid != 0 && ! typing);
        return;
    }
    switch (id)
    {
        case cmd::newSound: case cmd::openSound: active = true; break;
        case cmd::closeSound:    active = w != nullptr; break;

        case cmd::cut:           active = can (WindowComponent::Action::cut); break;
        case cmd::copy:          active = can (WindowComponent::Action::copy); break;
        case cmd::paste:         active = can (WindowComponent::Action::paste); break;
        case cmd::clear:         active = can (WindowComponent::Action::clear); break;
        case cmd::selectAll:     active = can (WindowComponent::Action::selectAll); break;
        case cmd::crop:          active = can (WindowComponent::Action::crop); break;
        case cmd::insertSilence: active = can (WindowComponent::Action::insertSilence); break;

        case cmd::stop:          active = w != nullptr; break;
        case cmd::play:          active = w != nullptr && ! w->isPlaying(); break;
        case cmd::pause:         active = w != nullptr && w->isPlaying(); break;
        case cmd::loop:          active = w != nullptr; r.setTicked (w != nullptr && w->isLoopOn()); break;
        case cmd::fastForward:   active = w != nullptr; r.setTicked (w != nullptr && w->isAdvanceOn()); break;
        case cmd::mute:
            active = engineWindow != nullptr;
            r.setTicked (engineWindow != nullptr && engineWindow->muted.load (std::memory_order_relaxed));
            break;
        case cmd::solo:
            active = engineWindow != nullptr;
            r.setTicked (engineWindow != nullptr && engineWindow->soloed.load (std::memory_order_relaxed));
            break;
        case cmd::playForward:
            active = w != nullptr; r.setTicked (w != nullptr);   // the only direction until step 8
            break;

        case cmd::changeName:    active = w != nullptr; break;
        case cmd::toggleSidePanels:
            active = true;
            r.shortName = AppPrefs::get().sidePanelsShown() ? "Hide Side Panels" : "Show Side Panels";
            break;
        case cmd::toggleKeyboard:
            active = true;
            r.shortName = keyboard.isVisible() ? "Hide Keyboard" : "Show Keyboard";
            break;
        case cmd::toggleGrid:
            active = true;
            r.shortName = canvas.isMosaicMode() ? "Free Canvas" : "Grid Canvas";
            break;
        case cmd::zoom100:       active = true; break;

        // Undo is always inactive (that's the joke). Everything else here is a
        // command whose feature lands in a later step: listed, greyed.
        default:                 active = false; break;
    }

    r.setActive (active && ! typing);
}

bool SoundEffectsAudioProcessorEditor::perform (const InvocationInfo& info)
{
    auto* w = targetWindow();
    const auto act = [w] (WindowComponent::Action a) { return w != nullptr && w->perform (a); };
    auto* engineWindow = processorRef.engine().windowById (canvas.selectedWindowId());

    // Keys 1..0: focus the sound in that mixer slot — its Window on the canvas,
    // which selects its strip in the mixer too.
    if (const int slot = cmd::slotOf (info.commandID); slot >= 0)
    {
        const auto sid = processorRef.engine().windowInSlot (slot);
        if (sid == 0) return false;
        canvas.focusWindow (sid);
        return true;
    }

    switch (info.commandID)
    {
        case cmd::newSound:      newWindow(); return true;
        case cmd::openSound:     openFile(); return true;
        case cmd::closeSound:    closeFocusedWindow(); return true;

        case cmd::cut:           return act (WindowComponent::Action::cut);
        case cmd::copy:          return act (WindowComponent::Action::copy);
        case cmd::paste:         return act (WindowComponent::Action::paste);
        case cmd::clear:         return act (WindowComponent::Action::clear);
        case cmd::selectAll:     return act (WindowComponent::Action::selectAll);
        case cmd::crop:          return act (WindowComponent::Action::crop);
        case cmd::insertSilence: return act (WindowComponent::Action::insertSilence);

        // Play and Pause share Space: whichever is active is the one that runs, and
        // both are the same toggle the transport button uses.
        case cmd::play: case cmd::pause:
                                 return act (WindowComponent::Action::playPause);
        case cmd::stop:          return act (WindowComponent::Action::stop);
        case cmd::loop:          return act (WindowComponent::Action::toggleLoop);
        case cmd::fastForward:   return act (WindowComponent::Action::toggleAdvance);
        case cmd::changeName:    return act (WindowComponent::Action::rename);

        case cmd::mute:
            if (engineWindow == nullptr) return false;
            engineWindow->muted.store (! engineWindow->muted.load (std::memory_order_relaxed), std::memory_order_relaxed);
            return true;
        case cmd::solo:
            if (engineWindow == nullptr) return false;
            engineWindow->soloed.store (! engineWindow->soloed.load (std::memory_order_relaxed), std::memory_order_relaxed);
            return true;

        case cmd::toggleSidePanels:
            AppPrefs::get().setSidePanelsShown (! AppPrefs::get().sidePanelsShown());
            resized();
            return true;
        case cmd::toggleKeyboard: setKeyboardShown (! keyboard.isVisible()); return true;
        case cmd::toggleGrid:     canvas.setMosaicMode (! canvas.isMosaicMode()); return true;
        case cmd::zoom100:        canvas.setZoom (1.0f); return true;
        default: break;
    }
    return false;
}

// --- menus --------------------------------------------------------------------------

juce::StringArray SoundEffectsAudioProcessorEditor::getMenuBarNames()
{
    return { "File", "Edit", "Selection", "Transport", "Window" };
}

juce::PopupMenu SoundEffectsAudioProcessorEditor::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    const auto add = [&m, this] (std::initializer_list<juce::CommandID> ids)
    {
        for (auto id : ids) m.addCommandItem (&commandManager, id);
    };
    // A command item whose row carries an icon, as the Transport menu's first rows do.
    const auto addWithIcon = [&m, this] (juce::CommandID id, os8::icon::Id icon)
    {
        juce::ApplicationCommandInfo info (id);
        commandManager.getTargetForCommand (id, info);
        juce::PopupMenu::Item item (info.shortName);
        item.setID (id);
        item.setEnabled ((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0);
        item.setTicked ((info.flags & juce::ApplicationCommandInfo::isTicked) != 0);
        item.commandManager = &commandManager;
        item.setImage (os8::icon::makeDrawable (icon));
        m.addItem (std::move (item));
    };

    switch (index)
    {
        case 0:   // File
            add ({ cmd::newSound, cmd::openSound, cmd::closeSound });
            m.addSeparator();
            add ({ cmd::importSound, cmd::exportSound });
            m.addSeparator();
            add ({ cmd::newCanvas, cmd::openCanvas, cmd::saveCanvas, cmd::saveCanvasAs, cmd::revertCanvas });
            m.addSeparator();
            add ({ cmd::audioSettings });
            break;

        case 1:   // Edit
            add ({ cmd::undo });
            m.addSeparator();
            add ({ cmd::cut, cmd::copy, cmd::paste, cmd::duplicate });
            m.addSeparator();
            add ({ cmd::leaveSilence, cmd::shiftSound });
            m.addSeparator();
            add ({ cmd::clear });
            break;

        case 2:   // Selection
            add ({ cmd::selectAll });
            m.addSeparator();
            add ({ cmd::extendToBeginning, cmd::extendToEnd, cmd::extendAbove, cmd::extendBelow });
            m.addSeparator();
            add ({ cmd::insertSilence, cmd::crop });
            m.addSeparator();
            add ({ cmd::setLoop, cmd::selectLoop });
            m.addSeparator();
            add ({ cmd::selectExactly, cmd::selectPeak, cmd::selectSilence });
            break;

        case 3:   // Transport
            addWithIcon (cmd::record, os8::icon::Id::record);
            add ({ cmd::selectAudioSource });
            m.addSeparator();
            addWithIcon (cmd::stop, os8::icon::Id::stop);
            addWithIcon (cmd::play, os8::icon::Id::playForward);
            addWithIcon (cmd::pause, os8::icon::Id::pause);
            addWithIcon (cmd::loop, os8::icon::Id::loop);
            addWithIcon (cmd::fastForward, os8::icon::Id::fastForward);
            m.addSeparator();
            add ({ cmd::playForward, cmd::playBackward, cmd::playPingPong });
            m.addSeparator();
            add ({ cmd::mute, cmd::solo });
            break;

        case 4:
            return windowMenu();

        default: break;
    }
    return m;
}

juce::PopupMenu SoundEffectsAudioProcessorEditor::windowMenu()
{
    juce::PopupMenu m;
    auto& engine = processorRef.engine();
    const auto selected = canvas.selectedWindowId();
    const auto nameOf = [&engine] (uint64_t id) -> juce::String
    {
        auto* w = engine.windowById (id);
        return w != nullptr && w->uiDoc != nullptr ? juce::String (w->uiDoc->name) : juce::String();
    };

    // The selected sound, ticked and inert: it says what the next two items act on.
    if (selected != 0)
    {
        m.addItem (juce::PopupMenu::Item (nameOf (selected)).setTicked (true).setEnabled (false));

        // Color: [name] ▸ — the 24 themes, each with its swatch.
        auto* w = engine.windowById (selected);
        const int current = w != nullptr ? w->tint : -1;
        juce::PopupMenu colours;
        for (int i = 0; i < os8::kNumThemes; ++i)
        {
            auto swatch = std::make_unique<juce::DrawablePath>();
            juce::Path dot;
            dot.addEllipse (0.0f, 0.0f, 10.0f, 10.0f);
            swatch->setPath (dot);
            swatch->setFill (os8::themeColour (i));

            juce::PopupMenu::Item item (os8::themeName (i));
            item.setTicked (i == current);
            item.setImage (std::move (swatch));
            item.setAction ([this, selected, i]
            {
                if (auto* win = processorRef.engine().windowById (selected))
                    win->tint = i;
                canvas.refresh();
            });
            colours.addItem (std::move (item));
        }

        auto colourDot = std::make_unique<juce::DrawablePath>();
        juce::Path dot;
        dot.addEllipse (0.0f, 0.0f, 10.0f, 10.0f);
        colourDot->setPath (dot);
        colourDot->setFill (os8::themeColour (current));
        m.addSubMenu ("Color: " + os8::themeName (current), colours, true, std::move (colourDot));
    }
    m.addCommandItem (&commandManager, cmd::changeName);
    m.addSeparator();

    // Every sound, in mixer-slot order; picking one brings it into focus. Slots 1-10
    // are the focusSlot commands, so their rows carry the 1..0 key in the shortcut
    // column; a sound further down is a plain row.
    for (int slot = 0; slot <= engine.highestSlot(); ++slot)
    {
        const auto id = engine.windowInSlot (slot);
        if (id == 0) continue;
        if (slot < 10)
            m.addCommandItem (&commandManager, cmd::focusSlot1 + slot, nameOf (id));
        else
            m.addItem (juce::PopupMenu::Item (nameOf (id)).setTicked (id == selected)
                           .setAction ([this, id] { canvas.focusWindow (id); }));
    }
    if (! engine.windowIds().empty()) m.addSeparator();

    m.addCommandItem (&commandManager, cmd::toggleSidePanels);
    m.addCommandItem (&commandManager, cmd::toggleKeyboard);
    m.addCommandItem (&commandManager, cmd::toggleGrid);
    m.addSeparator();
    m.addCommandItem (&commandManager, cmd::zoom100);
    m.addCommandItem (&commandManager, cmd::zoomToFit);
    return m;
}

SoundEffectsAudioProcessorEditor::~SoundEffectsAudioProcessorEditor()
{
    stopTimer();
}

void SoundEffectsAudioProcessorEditor::timerCallback()
{
    // Muted unless something says otherwise: in a plugin there is no hook to ask,
    // and the record button should be grey there too.
    canvas.setRecordEnabled (isAudioInputActive != nullptr && isAudioInputActive());

    canvas.refresh();
    mixer.refresh();
    effects.refresh();
    if (keyboard.isVisible()) keyboard.refresh();
}

void SoundEffectsAudioProcessorEditor::setKeyboardShown (bool shown)
{
    if (keyboard.isVisible() == shown) return;

    keyboard.setVisible (shown);   // hiding it silences whatever it was playing
    palette.setKeyboardShown (shown);
    resized();                     // the stack's height changed; the View palette moves
}

void SoundEffectsAudioProcessorEditor::newWindow()
{
    se::DocumentInit init;
    init.name = "untitled sound";
    init.numTracks = 1;
    processorRef.engine().addWindow (std::make_unique<se::SoundDocument> (init));
}

void SoundEffectsAudioProcessorEditor::closeFocusedWindow()
{
    // Exactly what the Window's own close box does, aimed at whatever is focused.
    if (const auto id = canvas.selectedWindowId(); id != 0)
        processorRef.engine().removeWindow (id);
}

bool SoundEffectsAudioProcessorEditor::hasFocusedWindow() const
{
    return canvas.selectedWindowId() != 0;
}

void SoundEffectsAudioProcessorEditor::openFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a sound", juce::File(),
                                                   formatManager.getWildcardForAllFormats());
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr) return;

        const int numCh = (int) reader->numChannels;
        const int frames = (int) reader->lengthInSamples;
        if (numCh <= 0 || frames <= 0) return;

        juce::AudioBuffer<float> buf (numCh, frames);
        reader->read (&buf, 0, frames, 0, true, true);

        // Planar floats → one Track per channel.
        std::vector<std::vector<float>> channels ((size_t) numCh);
        for (int c = 0; c < numCh; ++c)
            channels[(size_t) c].assign (buf.getReadPointer (c), buf.getReadPointer (c) + frames);

        const int bps = reader->bitsPerSample >= 16 ? 16 : 8;
        auto doc = se::makeDocumentFromFloat (channels, reader->sampleRate, bps,
                                              file.getFileNameWithoutExtension().toStdString());
        processorRef.engine().addWindow (std::move (doc));
    });
}

void SoundEffectsAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xffbfbfbf));
}

void SoundEffectsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (AppTopBar::kHeight + 1));

    // The side panels are 270 px columns. Hidden (⇥) they are gone; shaded to their
    // title bar they float over the canvas's top corner and give it the column.
    const bool panels = AppPrefs::get().sidePanelsShown();
    mixerPanel.setVisible (panels);
    effectsPanel.setVisible (panels);

    // Never more than a third of the editor each, so a small host window still
    // leaves a canvas between them.
    const int w = juce::jmin (SidePanel::kWidth, area.getWidth() / 3);
    const auto column = [&area, w] (SidePanel& panel, bool left)
    {
        if (panel.isCollapsed())
        {
            const int x = left ? area.getX() : area.getRight() - w;
            panel.setBounds (x, area.getY(), w, SidePanel::kCollapsedHeight);
            return;
        }
        panel.setBounds (left ? area.removeFromLeft (w) : area.removeFromRight (w));
    };

    if (panels)
    {
        column (mixerPanel, true);
        column (effectsPanel, false);
    }

    canvas.setBounds (area);
    mixerPanel.toFront (false);     // over the canvas when shaded
    effectsPanel.toFront (false);
    palette.toFront (false);
    keyboard.toFront (false);
    layoutPalettes (area);
}

void SoundEffectsAudioProcessorEditor::layoutPalettes (juce::Rectangle<int> area)
{
    // The palette hangs over the canvas — centred on the VIEWPORT, not on the
    // editor, so it stays over the board it drives rather than drifting under a
    // panel as the panels take their share. It narrows with the viewport too, down
    // to the width below which its slider stops being a slider.
    const int paletteW = juce::jlimit (ViewPalette::kMinWidth, ViewPalette::kMaxWidth,
                                       area.getWidth() - 2 * kPaletteSideGap);

    // The floating stack is laid out from the BOTTOM up: the Keyboard takes the
    // bottom slot and the View palette rides above it, so showing the Keyboard
    // lifts the palette rather than covering it. With the Keyboard hidden the
    // palette lands exactly where it always did.
    int floor = area.getBottom() - kPaletteBottomGap;

    if (keyboard.isVisible())
    {
        // The Keyboard is a fixed width — it is a picture of a keyboard, and a
        // keyboard that has been squeezed is a different instrument. On a canvas
        // too narrow for it, it overhangs rather than distorts.
        keyboard.setBounds (area.getCentreX() - KeyboardPalette::kWidth / 2,
                            floor - KeyboardPalette::kHeight,
                            KeyboardPalette::kWidth, KeyboardPalette::kHeight);
        floor -= KeyboardPalette::kHeight + kPaletteStackGap;
    }

    palette.setBounds (area.getCentreX() - paletteW / 2,
                       floor - ViewPalette::kHeight,
                       paletteW, ViewPalette::kHeight);
}
