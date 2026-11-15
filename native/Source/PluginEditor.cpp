#include "PluginEditor.h"

#include "engine/Import.h"
#include "ui/AppCommands.h"
#include "engine/Project.h"
#include "ui/AppPrefs.h"
#include "ui/FilePopups.h"
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
    addAndMakeVisible (magnetophonPanel);

    // Each panel shades to its title bar from the box in that bar, and stays so.
    mixerPanel.setCollapsed (AppPrefs::get().mixerCollapsed(), juce::dontSendNotification);
    effectsPanel.setCollapsed (AppPrefs::get().effectsCollapsed(), juce::dontSendNotification);
    mixerPanel.onCollapsedChanged = [this] (bool c) { AppPrefs::get().setMixerCollapsed (c); resized(); };
    effectsPanel.onCollapsedChanged = [this] (bool c) { AppPrefs::get().setEffectsCollapsed (c); resized(); };
    magnetophonPanel.setCollapsed (AppPrefs::get().magnetophonCollapsed(), juce::dontSendNotification);
    magnetophonPanel.onCollapsedChanged = [this] (bool c) { AppPrefs::get().setMagnetophonCollapsed (c); resized(); };

    // The Magnetophon records from its own source (Main by default), chosen in the
    // same popup a Sound uses; Record first checks that source can be heard.
    magnetophon.onSourceRequested = [this] { openTapeSourcePopup(); };
    magnetophon.onRecordRequested = [this]
    {
        auto& deck = processorRef.tapeDeck();
        if (deck.tape().mode() != se::Tape::Mode::recording)
        {
            if (! sourceReady (tapeChoice.kind))
            {
                if (onShowAudioSettings) onShowAudioSettings();
                return false;
            }
            applyTapeSource();
        }
        return deck.record();
    };
    applyTapeSource();
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

    // The Keyboard is the palette's own toggle (and Window ▸ Keyboard): the effects
    // panel of 0.4.0 has no row for it.
    palette.onKeyboardToggled = [this] (bool shown) { setKeyboardShown (shown); };

    // New / Open live at the top of the mixer panel, not in any toolbar.
    mixer.onNew  = [this] { newWindow(); };
    mixer.onOpen = [this] { openFile(); };

    // A Window's record button is greyed — recording is not implemented — and
    // clicking it asks for the audio device settings instead. Only the standalone
    // app can serve that, so it forwards to a hook the app installs.
    // Recording: the processor owns the recorder (a take outlives the editor); the
    // Window asks it, and the canvas redraws what it writes.
    canvas.setRecording (&p.recorder(), [this] (uint64_t window, se::RecordSetup& s) { return recordSetupFor (window, s); },
                         [this] (uint64_t window) { return recordReadyFor (window); });
    p.recorder().onChanged = [this] (uint64_t window)
    {
        // Every tick of a take: redraw only the frames it wrote. Anything else the
        // recorder changes (a take starting: tracks added or cleared) redraws all.
        auto* wc = canvas.windowComponentFor (window);
        if (wc == nullptr) return;
        size_t from = 0, to = 0;
        if (processorRef.recorder().takeWrittenFrames (from, to))
            wc->invalidateWaveformFrames (processorRef.recorder().targetTracks(), from, to);
        else
            wc->invalidateWaveform();
    };
    p.recorder().onFinished = [this] (uint64_t window)
    {
        if (auto* wc = canvas.windowComponentFor (window)) { wc->invalidateWaveform(); wc->takeFinished(); }
    };

    canvas.onSourceRequested = [this] (uint64_t window) { openSourcePopup (window); };

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
    // F is Fast Forward, held: the command hears its down and up (keyStateChanged,
    // wantsKeyUpDownCallbacks), so its key press, and every auto-repeat of it, is
    // taken here rather than left to fall through to the system's beep.
    if (k.getKeyCode() == 'F' && ! k.getModifiers().isAnyModifierKeyDown() && ! isTyping())
        return true;
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
        case cmd::importSound:   active = true; break;
        case cmd::exportSound:   active = w != nullptr; break;

        case cmd::cut:           active = can (WindowComponent::Action::cut); break;
        case cmd::copy:          active = can (WindowComponent::Action::copy); break;
        case cmd::paste:         active = can (WindowComponent::Action::paste); break;
        case cmd::duplicate:     active = can (WindowComponent::Action::duplicate); break;
        case cmd::leaveSilence:  active = can (WindowComponent::Action::leaveSilence); break;
        case cmd::shiftSound:    active = can (WindowComponent::Action::shiftSound); break;
        case cmd::clear:         active = can (WindowComponent::Action::clear); break;
        case cmd::selectAll:     active = can (WindowComponent::Action::selectAll); break;
        case cmd::extendToBeginning: active = can (WindowComponent::Action::extendToBeginning); break;
        case cmd::extendToEnd:   active = can (WindowComponent::Action::extendToEnd); break;
        case cmd::extendAbove:   active = can (WindowComponent::Action::extendAbove); break;
        case cmd::extendBelow:   active = can (WindowComponent::Action::extendBelow); break;
        case cmd::selectExactly: active = can (WindowComponent::Action::selectExactly); break;
        case cmd::selectPeak:    active = can (WindowComponent::Action::selectPeak); break;
        case cmd::selectSilence: active = can (WindowComponent::Action::selectSilence); break;
        case cmd::setLoop:       active = can (WindowComponent::Action::setLoop); break;
        case cmd::unsetLoop:     active = can (WindowComponent::Action::unsetLoop); break;
        case cmd::selectLoop:    active = can (WindowComponent::Action::selectLoop); break;
        case cmd::crop:          active = can (WindowComponent::Action::crop); break;
        case cmd::insertSilence: active = can (WindowComponent::Action::insertSilence); break;

        case cmd::soundColour:   active = engineWindow != nullptr && os8::colorThemeShown(); break;
        case cmd::rename:        active = w != nullptr; break;
        case cmd::selectAudioSource: active = can (WindowComponent::Action::selectAudioSource); break;
        case cmd::armRecord:     active = can (WindowComponent::Action::record); r.setTicked (w != nullptr && w->isArmed()); break;
        case cmd::directRecording: active = can (WindowComponent::Action::directRecord); break;
        case cmd::stop:          active = w != nullptr; break;
        case cmd::play:          active = w != nullptr && ! w->isPlaying(); break;
        case cmd::pause:         active = w != nullptr && w->isPlaying(); break;
        case cmd::loop:          active = can (WindowComponent::Action::toggleLoop); r.setTicked (w != nullptr && w->isLoopOn()); break;
        case cmd::fastForward:
            active = can (WindowComponent::Action::toggleAdvance); r.setTicked (w != nullptr && w->isAdvanceOn());
            r.flags |= juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks;   // momentary: F down and F up
            break;
        case cmd::mute:
            active = engineWindow != nullptr;
            r.setTicked (engineWindow != nullptr && engineWindow->muted.load (std::memory_order_relaxed));
            break;
        case cmd::solo:
            active = engineWindow != nullptr;
            r.setTicked (engineWindow != nullptr && engineWindow->soloed.load (std::memory_order_relaxed));
            break;
        case cmd::playForward:
            active = can (WindowComponent::Action::playForward); r.setTicked (w != nullptr && w->direction() == se::PlayDirection::forward);
            break;
        case cmd::playBackward:
            active = can (WindowComponent::Action::playBackward); r.setTicked (w != nullptr && w->direction() == se::PlayDirection::backward);
            break;
        case cmd::playPingPong:
            active = can (WindowComponent::Action::playPingPong); r.setTicked (w != nullptr && w->direction() == se::PlayDirection::pingPong);
            break;
        case cmd::flipDirection: active = can (WindowComponent::Action::cycleDirection); break;

        case cmd::toggleColourTheme:
            active = true;
            r.shortName = os8::colorThemeShown() ? "Hide Color Theme" : "Show Color Theme";
            break;
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
        case cmd::zoomSoundIn:   active = can (WindowComponent::Action::zoomIn); break;
        case cmd::zoomSoundOut:  active = can (WindowComponent::Action::zoomOut); break;
        case cmd::fitWindow:     active = can (WindowComponent::Action::fitWindow); break;
        case cmd::fitSelection:  active = can (WindowComponent::Action::fitSelection); break;
        case cmd::zoomCanvasIn: case cmd::zoomCanvasOut:
        case cmd::zoom100:       active = true; break;
        case cmd::zoomToFit:     active = canvas.hasWindows(); break;

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
        case cmd::importSound:   importSound(); return true;
        case cmd::exportSound:   exportSound(); return true;

        case cmd::cut:           return act (WindowComponent::Action::cut);
        case cmd::copy:          return act (WindowComponent::Action::copy);
        case cmd::paste:         return act (WindowComponent::Action::paste);
        case cmd::duplicate:     return act (WindowComponent::Action::duplicate);
        case cmd::leaveSilence:  return act (WindowComponent::Action::leaveSilence);
        case cmd::shiftSound:    return act (WindowComponent::Action::shiftSound);
        case cmd::clear:         return act (WindowComponent::Action::clear);
        case cmd::selectAll:     return act (WindowComponent::Action::selectAll);
        case cmd::extendToBeginning: return act (WindowComponent::Action::extendToBeginning);
        case cmd::extendToEnd:   return act (WindowComponent::Action::extendToEnd);
        case cmd::extendAbove:   return act (WindowComponent::Action::extendAbove);
        case cmd::extendBelow:   return act (WindowComponent::Action::extendBelow);
        case cmd::selectExactly: return act (WindowComponent::Action::selectExactly);
        case cmd::selectPeak:    return act (WindowComponent::Action::selectPeak);
        case cmd::selectSilence: return act (WindowComponent::Action::selectSilence);
        case cmd::setLoop:       return act (WindowComponent::Action::setLoop);
        case cmd::unsetLoop:     return act (WindowComponent::Action::unsetLoop);
        case cmd::selectLoop:    return act (WindowComponent::Action::selectLoop);
        case cmd::crop:          return act (WindowComponent::Action::crop);
        case cmd::insertSilence: return act (WindowComponent::Action::insertSilence);

        // Play and Pause share Space: whichever is active is the one that runs, and
        // both are the same toggle the transport button uses.
        case cmd::play: case cmd::pause:
                                 return act (WindowComponent::Action::playPause);
        case cmd::soundColour:   return showColourMenu();
        case cmd::rename:        return act (WindowComponent::Action::rename);
        case cmd::selectAudioSource: return act (WindowComponent::Action::selectAudioSource);
        case cmd::stop:          return act (WindowComponent::Action::stop);
        case cmd::armRecord:     return act (WindowComponent::Action::record);
        case cmd::directRecording: return act (WindowComponent::Action::directRecord);
        case cmd::loop:          return act (WindowComponent::Action::toggleLoop);
        case cmd::fastForward:
            if (info.invocationMethod == InvocationInfo::fromKeyPress)
            {
                // Held F: down engages, up lets go — of the Window it went down on.
                if (info.isKeyDown)
                {
                    if (w == nullptr) return false;
                    fastForwardWindow = canvas.selectedWindowId();
                    w->fastForward (true);
                }
                else if (auto* held = canvas.windowComponentFor (fastForwardWindow))
                {
                    held->fastForward (false);
                    fastForwardWindow = 0;
                }
                return true;
            }
            return act (WindowComponent::Action::toggleAdvance);   // the menu row: engage / let go
        case cmd::playForward:   return act (WindowComponent::Action::playForward);
        case cmd::playBackward:  return act (WindowComponent::Action::playBackward);
        case cmd::playPingPong:  return act (WindowComponent::Action::playPingPong);
        case cmd::flipDirection: return act (WindowComponent::Action::cycleDirection);

        case cmd::mute:
            if (engineWindow == nullptr) return false;
            engineWindow->muted.store (! engineWindow->muted.load (std::memory_order_relaxed), std::memory_order_relaxed);
            return true;
        case cmd::solo:
            if (engineWindow == nullptr) return false;
            engineWindow->soloed.store (! engineWindow->soloed.load (std::memory_order_relaxed), std::memory_order_relaxed);
            return true;

        case cmd::toggleColourTheme:
            // Every themed element and the canvas read it through os8::themeColourFor
            // and canvasColourFor; refresh now rather than on the next tick.
            AppPrefs::get().setColorThemeShown (! os8::colorThemeShown());
            canvas.refresh();
            mixer.refresh();
            effects.refresh();
            keyboard.refresh();
            return true;
        case cmd::toggleSidePanels:
            AppPrefs::get().setSidePanelsShown (! AppPrefs::get().sidePanelsShown());
            resized();
            return true;
        case cmd::toggleKeyboard: setKeyboardShown (! keyboard.isVisible()); return true;
        case cmd::toggleGrid:     canvas.setMosaicMode (! canvas.isMosaicMode()); return true;
        case cmd::zoomSoundIn:    return act (WindowComponent::Action::zoomIn);
        case cmd::zoomSoundOut:   return act (WindowComponent::Action::zoomOut);
        case cmd::fitWindow:      return act (WindowComponent::Action::fitWindow);
        case cmd::fitSelection:   return act (WindowComponent::Action::fitSelection);
        case cmd::zoomCanvasIn:   canvas.zoomStep (true);  return true;
        case cmd::zoomCanvasOut:  canvas.zoomStep (false); return true;
        case cmd::zoom100:        canvas.setZoom (1.0f); return true;
        case cmd::zoomToFit:
            // The palettes float over the canvas's foot: keep what they hide out of the fit.
            canvas.zoomToFit (canvas.getBottom() - std::min (palette.getY(), keyboard.isVisible() ? keyboard.getY() : palette.getY()));
            return true;
        default: break;
    }
    return false;
}

// --- menus --------------------------------------------------------------------------

juce::StringArray SoundEffectsAudioProcessorEditor::getMenuBarNames()
{
    return { "File", "Edit", "Selection", "Sound", "Canvas" };
}

juce::PopupMenu SoundEffectsAudioProcessorEditor::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    const auto add = [&m, this] (std::initializer_list<juce::CommandID> ids)
    {
        for (auto id : ids) m.addCommandItem (&commandManager, id);
    };
    // A command item whose row carries an icon, as the Transport menu's first rows do.
    const auto addWithIcon = [this] (juce::PopupMenu& menu, juce::CommandID id, os8::icon::Id icon)
    {
        juce::ApplicationCommandInfo info (id);
        commandManager.getTargetForCommand (id, info);
        juce::PopupMenu::Item item (info.shortName);
        item.setID (id);
        item.setEnabled ((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0);
        item.setTicked ((info.flags & juce::ApplicationCommandInfo::isTicked) != 0);
        item.commandManager = &commandManager;
        item.setImage (os8::icon::makeDrawable (icon));
        Os8AppMenuLookAndFeel::markIconRow (item);
        menu.addItem (std::move (item));
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
            add ({ cmd::setLoop, cmd::unsetLoop, cmd::selectLoop });
            m.addSeparator();
            add ({ cmd::selectExactly, cmd::selectPeak, cmd::selectSilence });
            break;

        case 3:   // Sound
            m.addItem (colourRow());
            add ({ cmd::rename });
            m.addSeparator();
            add ({ cmd::armRecord, cmd::directRecording, cmd::selectAudioSource });
            m.addSeparator();
            addWithIcon (m, cmd::stop, os8::icon::Id::stop);
            addWithIcon (m, cmd::play, os8::icon::Id::playForward);
            addWithIcon (m, cmd::pause, os8::icon::Id::pause);
            add ({ cmd::flipDirection });
            addWithIcon (m, cmd::fastForward, os8::icon::Id::fastForward);
            addWithIcon (m, cmd::loop, os8::icon::Id::loop);
            m.addSeparator();
            add ({ cmd::mute, cmd::solo });
            m.addSeparator();
            {
                juce::PopupMenu zoom;
                addWithIcon (zoom, cmd::zoomSoundIn, os8::icon::Id::zoomIn);
                addWithIcon (zoom, cmd::zoomSoundOut, os8::icon::Id::zoomOut);
                addWithIcon (zoom, cmd::fitWindow, os8::icon::Id::zoomReset);
                zoom.addCommandItem (&commandManager, cmd::fitSelection);
                m.addSubMenu ("Zoom Sound", zoom);
            }
            break;

        case 4:
            return canvasMenu();

        default: break;
    }
    return m;
}

void SoundEffectsAudioProcessorEditor::menuBarActivated (bool isActive)
{
    if (isActive) return;
    // After the item has run: a popup it opened (Shift Sound…) keeps the focus it took.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SoundEffectsAudioProcessorEditor> (this)]
    {
        if (safe == nullptr || juce::Component::getCurrentlyFocusedComponent() != nullptr
            || juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0) return;
        safe->canvas.grabKeyboardFocus();
    });
}

juce::PopupMenu SoundEffectsAudioProcessorEditor::colourMenu()
{
    // The 24 themes, each with its swatch; the selected Sound's is ticked.
    juce::PopupMenu colours;
    const auto selected = canvas.selectedWindowId();
    auto* w = processorRef.engine().windowById (selected);
    const int current = w != nullptr ? w->tint : -1;
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
        Os8AppMenuLookAndFeel::markIconRow (item);
        item.setAction ([this, selected, i]
        {
            if (auto* win = processorRef.engine().windowById (selected))
                win->tint = i;
            canvas.refresh();
        });
        colours.addItem (std::move (item));
    }
    return colours;
}

juce::PopupMenu::Item SoundEffectsAudioProcessorEditor::colourRow()
{
    // Color: [name] ▸ with the theme's dot. With no Sound selected the row stays,
    // greyed and nameless, so the menu keeps its shape.
    auto* w = processorRef.engine().windowById (canvas.selectedWindowId());
    if (w == nullptr)
        return juce::PopupMenu::Item ("Color").setEnabled (false);

    auto colourDot = std::make_unique<juce::DrawablePath>();
    juce::Path dot;
    dot.addEllipse (0.0f, 0.0f, 10.0f, 10.0f);
    colourDot->setPath (dot);
    colourDot->setFill (os8::themeColour (w->tint));
    juce::PopupMenu::Item row ("Color: " + os8::themeName (w->tint));
    // With the Color Theme hidden the row stays, named and with the colour that
    // will come back, but greyed and with no submenu (the spec's note).
    if (os8::colorThemeShown())
        row.subMenu = std::make_unique<juce::PopupMenu> (colourMenu());
    else
        row.setEnabled (false);
    row.setImage (std::move (colourDot));
    Os8AppMenuLookAndFeel::markIconRow (row);
    return row;
}

bool SoundEffectsAudioProcessorEditor::showColourMenu()
{
    // ⌘T: the Color submenu, dropped down under the selected Window's title bar.
    auto* w = targetWindow();
    if (w == nullptr || processorRef.engine().windowById (canvas.selectedWindowId()) == nullptr
        || ! os8::colorThemeShown())
        return false;
    auto menu = colourMenu();
    menu.setLookAndFeel (&Os8AppMenuLookAndFeel::instance());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (w)
                                                  .withTargetScreenArea (w->getScreenBounds().withHeight (os8::kTitleBarHeight)));
    return true;
}

juce::PopupMenu SoundEffectsAudioProcessorEditor::canvasMenu()
{
    juce::PopupMenu m;
    auto& engine = processorRef.engine();
    const auto selected = canvas.selectedWindowId();
    const auto nameOf = [&engine] (uint64_t id) -> juce::String
    {
        auto* w = engine.windowById (id);
        return w != nullptr && w->uiDoc != nullptr ? juce::String (w->uiDoc->name) : juce::String();
    };

    m.addCommandItem (&commandManager, cmd::toggleColourTheme);
    m.addSeparator();

    // Every sound, in mixer-slot order; picking one brings it into focus, and the
    // selected one is ticked. Slots 1-10 are the focusSlot commands, so their rows
    // carry the 1..0 key in the shortcut column; a sound further down is a plain row.
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

    m.addCommandItem (&commandManager, cmd::toggleGrid);
    m.addCommandItem (&commandManager, cmd::toggleKeyboard);
    m.addCommandItem (&commandManager, cmd::toggleSidePanels);
    m.addSeparator();
    juce::PopupMenu zoom;
    for (auto id : { cmd::zoomCanvasIn, cmd::zoomCanvasOut, cmd::zoom100, cmd::zoomToFit })
        zoom.addCommandItem (&commandManager, id);
    m.addSubMenu ("Zoom Canvas", zoom);
    return m;
}

SoundEffectsAudioProcessorEditor::~SoundEffectsAudioProcessorEditor()
{
    stopTimer();
    processorRef.recorder().onChanged = nullptr;     // the take (if any) carries on without a UI
    processorRef.recorder().onFinished = nullptr;
}

int SoundEffectsAudioProcessorEditor::numInputChannels() const
{
    return processorRef.getTotalNumInputChannels();
}

bool SoundEffectsAudioProcessorEditor::isPluginHost() const
{
    return processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone;
}

bool SoundEffectsAudioProcessorEditor::recordReadyFor (uint64_t window) const
{
    const auto it = sourceChoices.find (window);
    return sourceReady (it != sourceChoices.end() ? it->second.kind : se::AudioSourceKind::external);
}

bool SoundEffectsAudioProcessorEditor::sourceReady (se::AudioSourceKind kind) const
{
    switch (kind)
    {
        case se::AudioSourceKind::canvas:   return true;   // the engine's own output: always there
        case se::AudioSourceKind::daw:      return isPluginHost();
        case se::AudioSourceKind::external:
            // The standalone mutes its input until "Mute audio input" is cleared in the
            // audio settings; a plug-in has no such switch (the host decides).
            return numInputChannels() > 0 && (isPluginHost() || (isAudioInputActive != nullptr && isAudioInputActive()));
    }
    return false;
}

bool SoundEffectsAudioProcessorEditor::recordSetupFor (uint64_t window, se::RecordSetup& s)
{
    const auto& c = choiceFor (window);
    auto& engine = processorRef.engine();
    s.source = {};
    s.source.kind = c.kind;
    s.mode = c.clean ? se::ConvertMode::clean : se::ConvertMode::raw;

    switch (c.kind)
    {
        case se::AudioSourceKind::external:
        {
            const auto list = audiosource::externalSources (numInputChannels());
            if (list.empty()) return false;
            const auto& src = list[(size_t) std::clamp (c.externalIndex, 0, (int) list.size() - 1)];
            s.source.firstChannel = src.firstChannel;
            s.source.stereo = src.stereo;
            s.incomingStereo = src.stereo;
            break;
        }
        case se::AudioSourceKind::canvas:
            s.source.tapWindow = c.canvasWindow;   // 0 = the main mix, minus the Window recorded into
            s.incomingStereo = true;               // "always stereo"
            break;
        case se::AudioSourceKind::daw:
            s.source.stereo = true;
            s.incomingStereo = true;
            break;
    }

    // Input gain and monitoring belong to the Window's choice; the engine has one of
    // each, so they are set as its take begins. Monitoring a Canvas source would only
    // double what is audible already.
    engine.setInputGain ((float) (c.gainPercent / 100.0));
    engine.setMonitor (c.monitor && c.kind != se::AudioSourceKind::canvas);
    return true;
}

void SoundEffectsAudioProcessorEditor::openSourcePopup (uint64_t window)
{
    auto* wc = canvas.windowComponentFor (window);
    if (wc == nullptr) return;

    audiosource::Context ctx;
    ctx.engine = &processorRef.engine();
    ctx.window = window;
    ctx.numInputChannels = numInputChannels();
    ctx.dawAvailable = isPluginHost();
    if (onShowAudioSettings) ctx.openAudioSettings = [this] { onShowAudioSettings(); };

    auto built = audiosource::build (ctx, choiceFor (window),
        [this, window] (const audiosource::SourceChoice& c) { choiceFor (window) = c; },
        [this, window] (const audiosource::SourceChoice& c)
        {
            // The kind changed: keep what was picked and show the popup in its new shape.
            choiceFor (window) = c;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SoundEffectsAudioProcessorEditor> (this), window]
            {
                if (safe != nullptr) safe->openSourcePopup (window);
            });
        });
    auto* popup = built.popup.release();
    popup->showOver (*wc, { (wc->getWidth() - popup->getWidth()) / 2, (wc->getHeight() - popup->getHeight()) / 2 });
}

void SoundEffectsAudioProcessorEditor::openTapeSourcePopup()
{
    audiosource::Context ctx;
    ctx.engine = &processorRef.engine();
    ctx.window = 0;   // every Sound is a source for the tape
    ctx.numInputChannels = numInputChannels();
    ctx.dawAvailable = isPluginHost();
    if (onShowAudioSettings) ctx.openAudioSettings = [this] { onShowAudioSettings(); };

    auto built = audiosource::build (ctx, tapeChoice,
        [this] (const audiosource::SourceChoice& c) { tapeChoice = c; applyTapeSource(); },
        [this] (const audiosource::SourceChoice& c)
        {
            tapeChoice = c;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SoundEffectsAudioProcessorEditor> (this)]
            {
                if (safe != nullptr) safe->openTapeSourcePopup();
            });
        });
    auto* popup = built.popup.release();
    popup->showOver (canvas, { (canvas.getWidth() - popup->getWidth()) / 2, (canvas.getHeight() - popup->getHeight()) / 2 });
}

bool SoundEffectsAudioProcessorEditor::applyTapeSource()
{
    se::TapeSource s;
    s.kind = tapeChoice.kind;
    switch (tapeChoice.kind)
    {
        case se::AudioSourceKind::external:
        {
            const auto list = audiosource::externalSources (numInputChannels());
            if (! list.empty())
            {
                const auto& src = list[(size_t) std::clamp (tapeChoice.externalIndex, 0, (int) list.size() - 1)];
                s.firstChannel = src.firstChannel;
                s.stereo = src.stereo;
            }
            break;
        }
        case se::AudioSourceKind::canvas: s.tapWindow = tapeChoice.canvasWindow; break;
        case se::AudioSourceKind::daw:    s.stereo = true; break;
    }
    if (! processorRef.tapeDeck().setSource (s)) return false;   // not while recording
    auto& engine = processorRef.engine();
    engine.setTapeInputGain ((float) (tapeChoice.gainPercent / 100.0));
    engine.setTapeMonitor (tapeChoice.monitor && tapeChoice.kind != se::AudioSourceKind::canvas);
    return true;
}

juce::String SoundEffectsAudioProcessorEditor::tapeSourceName() const
{
    switch (tapeChoice.kind)
    {
        case se::AudioSourceKind::external:
        {
            const auto list = audiosource::externalSources (numInputChannels());
            return list.empty() ? juce::String ("No input")
                                : list[(size_t) std::clamp (tapeChoice.externalIndex, 0, (int) list.size() - 1)].name;
        }
        case se::AudioSourceKind::daw: return "DAW";
        case se::AudioSourceKind::canvas:
            if (tapeChoice.canvasWindow != 0)
                if (auto* w = processorRef.engine().windowById (tapeChoice.canvasWindow); w != nullptr && w->uiDoc)
                    return (w->slot >= 0 ? juce::String (w->slot + 1) + " - " : juce::String()) + juce::String (w->uiDoc->name);
            return "Main";
    }
    return {};
}

void SoundEffectsAudioProcessorEditor::timerCallback()
{
    canvas.refresh();
    mixer.refresh();
    effects.refresh();

    // The Sound the tape records from was closed: back to Main, once it may change.
    if (tapeChoice.kind == se::AudioSourceKind::canvas && tapeChoice.canvasWindow != 0
        && processorRef.engine().windowById (tapeChoice.canvasWindow) == nullptr)
    {
        const auto kept = tapeChoice;
        tapeChoice.canvasWindow = 0;
        if (! applyTapeSource()) tapeChoice = kept;
    }
    magnetophon.setSourceName (tapeSourceName());
    if (magnetophonPanel.isVisible() && ! magnetophonPanel.isCollapsed()) magnetophon.refresh();
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
    processorRef.engine().addWindow (std::make_unique<se::SoundDocument> (se::newSoundInit()));
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

namespace {

void showFileFailure (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

} // namespace

void SoundEffectsAudioProcessorEditor::exportSound()
{
    const auto id = canvas.selectedWindowId();
    auto* wc = canvas.windowComponentFor (id);
    se::WindowState captured;
    if (wc == nullptr || ! se::captureWindowState (processorRef.engine(), id, captured)) return;

    auto popup = filepopups::exportSound (captured);
    popup->onDismiss = [safe = juce::Component::SafePointer<SoundEffectsAudioProcessorEditor> (this),
                        sound = std::make_shared<se::WindowState> (std::move (captured))] (int result)
    {
        if (result != 1 || safe == nullptr) return;

        const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile (juce::String (se::safeFileName (sound->name)) + se::kEngineStateFileSuffix);
        safe->chooser = std::make_unique<juce::FileChooser> ("Export Sound", start,
                                                             "*" + juce::String (se::kEngineStateFileSuffix));
        safe->chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [sound] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File()) return;

            std::string err;
            if (! se::writeSoundExport (file.getFullPathName().toStdString(), *sound, err))
                showFileFailure ("Couldn't export the Sound", err);
        });
    };
    auto* raw = popup.release();
    raw->showOver (*wc, { (wc->getWidth() - raw->getWidth()) / 2, (wc->getHeight() - raw->getHeight()) / 2 });
}

void SoundEffectsAudioProcessorEditor::importSound()
{
    chooser = std::make_unique<juce::FileChooser> ("Import Sound",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*" + juce::String (se::kEngineStateFileSuffix));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SoundEffectsAudioProcessorEditor> (this)]
                          (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File() || safe == nullptr) return;

        auto state = std::make_shared<se::EngineState>();
        std::string err;
        if (! se::readSefx (file.getFullPathName().toStdString(), *state, err))
        {
            showFileFailure ("Couldn't import from " + file.getFileName(), err);
            return;
        }

        auto popup = filepopups::importSound (file.getFileName(), state->windows,
            [safe, state] (std::vector<size_t> picked)
            {
                if (safe == nullptr) return;
                se::EngineState chosen;
                for (size_t i : picked) chosen.windows.push_back (std::move (state->windows[i]));
                if (! safe->processorRef.importState (chosen))
                    showFileFailure ("Couldn't import every Sound", "The canvas has no room for them all.");
            });
        auto* raw = popup.release();
        raw->showOver (safe->canvas, { (safe->canvas.getWidth() - raw->getWidth()) / 2,
                                       (safe->canvas.getHeight() - raw->getHeight()) / 2 });
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
    magnetophonPanel.setVisible (panels);

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
        // The left column: the Mixer, the Magnetophon under it (274 px, or its title
        // bar when shaded). A shaded Mixer gives the canvas the column: both then
        // float over its top-left corner, the Magnetophon hanging under the Mixer's bar.
        const int magH = magnetophonPanel.isCollapsed() ? SidePanel::kCollapsedHeight
                                                       : MagnetophonComponent::kContentHeight + SidePanel::kCollapsedHeight;
        if (mixerPanel.isCollapsed())
        {
            column (mixerPanel, true);
            magnetophonPanel.setBounds (area.getX(), area.getY() + SidePanel::kCollapsedHeight, w, magH);
        }
        else
        {
            auto left = area.removeFromLeft (w);
            magnetophonPanel.setBounds (left.removeFromBottom (std::min (magH, left.getHeight() - SidePanel::kCollapsedHeight)));
            mixerPanel.setBounds (left);
        }
        column (effectsPanel, false);
    }

    canvas.setBounds (area);
    mixerPanel.toFront (false);     // over the canvas when shaded
    magnetophonPanel.toFront (false);
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
