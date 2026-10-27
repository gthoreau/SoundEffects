// StandaloneApp — the app's OWN macOS window, as opposed to the sound Windows.
//
// Two window vocabularies live in this program and they must not be confused. The
// Windows on the canvas are drawn by the app, Mac OS 8 down to the pixel. The thing
// those Windows float inside is a real macOS window: real traffic lights, a working
// zoom, real full screen. Since 0.4.0 its title bar is folded into the app's own top
// bar (MacWindowChrome): the native bar is hidden, the traffic lights sit at the
// left of our bar where the design puts them, and the application menus are drawn
// by the editor in that bar — the same menus the plugin shows. The macOS menu bar
// keeps only the application menu (About, Quit). [NATIVE]
//
// JUCE's stock standalone gives none of that. It hands you a DocumentWindow with a
// JUCE-DRAWN title bar, an "Options" button squeezed in beside the buttons, and a
// yellow banner across the top announcing that the audio input is muted. So we do
// not use it: JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP tells the wrapper to call our
// juce_CreateApplication() instead of defining its own, and this file supplies the
// application and the window.
//
// What we DO keep is juce::StandalonePluginHolder, which is the useful half of the
// wrapper — it owns the AudioDeviceManager, feeds the processor, remembers the
// device setup, and puts up the Audio/MIDI settings dialog. Only the window around
// it is ours.
//
// ## Why the window is ours and not a StandaloneFilterWindow subclass
//
// The feedback-loop banner is not a setting; it is a Component that
// StandaloneFilterWindow's private content class adds whenever the input is muted,
// reserving 30px at the top of the window for itself. It cannot be turned off from
// outside, and the input really is muted — that is the intended default here, with
// the greyed record button in each sound Window as its visible form. Every route to
// suppressing the banner while keeping the mute is a hack against a private class.
// Hosting the editor ourselves removes the banner by not having one, and takes the
// Options button and the content constrainer with it.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PluginEditor.h"
#include "ui/AppCommands.h"
#include "ui/AppMenuBar.h"
#include "ui/EditPopups.h"
#include "ui/MacWindowChrome.h"
#include "ui/Os8Gallery.h"
#include "engine/Import.h"
#include "engine/Project.h"

#include <memory>

namespace {

class MainWindow;

/** The app window: a plain Mac window with the plugin editor as its content, and
    the target every File command is routed to.

    It also holds the one piece of state that makes the canvas a DOCUMENT rather
    than just a running program: which file it came from. That is what the title
    bar shows, what Save writes to without asking, and what Revert goes back to. */
class MainWindow final : public juce::DocumentWindow,
                         public juce::ApplicationCommandTarget
{
public:
    MainWindow (const juce::String& title, juce::Colour background,
                juce::PropertySet* settingsToUse)
        : juce::DocumentWindow (title, background, juce::DocumentWindow::allButtons)
    {
        // ORDER MATTERS. setUsingNativeTitleBar() is what recreates the desktop
        // window, and the peer picks up its style flags — which buttons the system
        // title bar gets — at that moment. Ask for them afterwards and the flags
        // are set on a peer that has already been built.
        setResizable (true, false);   // no JUCE corner gripper; the OS edge-resizes
        setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, false);
        setUsingNativeTitleBar (true);

        pluginHolder = std::make_unique<juce::StandalonePluginHolder> (settingsToUse, false);
        attachEditor();

        // Fill the screen. userArea is precisely the rectangle macOS leaves between
        // the menu bar and the Dock, on whichever display is primary — so this
        // needs no arithmetic of its own and stays right when the Dock moves,
        // hides, or changes size. Deliberately unconditional: unlike the stock
        // window we do not restore a remembered position, because "open filling the
        // screen" is the behaviour that was asked for.
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            setBounds (display->userArea);

        // A floor, so the two side panels cannot be squeezed to nothing.
        setResizeLimits (640, 420, 32768, 32768);

        // The canvas came back from the settings file (StandalonePluginHolder's
        // constructor did that); this is the file it came FROM, so the title says
        // what you were working on rather than just what program this is.
        //
        // Only if it is still a document. See isStillADocument() — a remembered
        // path can outlive the file it named, or name a file the system is going
        // to delete, and neither is something to hand back as "what you were
        // working on". Dropping it makes this an unsaved canvas, which is exactly
        // what it is: the Windows on screen came from the autosaved state, not
        // from a file. ⌘S then means Save As, and asks where it should go.
        if (auto* props = pluginHolder->settings.get())
        {
            const juce::File remembered (props->getValue (kCanvasFileKey));

            if (isStillADocument (remembered))
                canvasFile = remembered;
            else
                props->removeValue (kCanvasFileKey);
        }

        updateTitle();
    }

    /** Fold the title bar into the app's top bar. Needs the native peer, so it runs
        once the window is on screen. The traffic lights go where the design has
        them: 14 px circles centred at x = 18, 41, 64, 16.5 px down the 32 px bar. */
    void adoptTopBarChrome()
    {
        os8::chrome::adoptAppTopBar (*this, 16.5f, { 18.0f, 41.0f, 64.0f });

        // The window was sized while it still had a title bar of its own, which is
        // now gone: fill the screen again, so the top bar sits where the title bar
        // was rather than one title bar lower.
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            setBounds (display->userArea);
    }

    ~MainWindow() override
    {
        pluginHolder->stopPlaying();
        detachEditor();
        pluginHolder = nullptr;
    }

    /** Red traffic light. One window, so closing it is quitting. */
    void closeButtonPressed() override
    {
        pluginHolder->savePluginState();
        juce::JUCEApplicationBase::quit();
    }

    // ============================ commands ==================================

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

    /** The commands only the standalone can answer: the canvas as a document, and
        the audio device. The editor owns the command manager and the menus; it
        hands these on to us (setHostCommandTarget). Names and keys come from the
        one shortcut table, AppCommands. */
    void getAllCommands (juce::Array<juce::CommandID>& c) override
    {
        c.addArray ({ cmd::newCanvas, cmd::openCanvas, cmd::saveCanvas, cmd::saveCanvasAs,
                      cmd::revertCanvas, cmd::audioSettings });
    }

    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& r) override
    {
        cmd::fill (r, id);

        // Save Canvas is never greyed, unlike the original's Save: with no file yet
        // it means Save As, which is what every program written since has done.
        // [NATIVE]
        if (id == cmd::revertCanvas)
            r.setActive (canvasFile.existsAsFile());
    }

    bool perform (const InvocationInfo& info) override
    {
        switch (info.commandID)
        {
            case cmd::newCanvas:     newCanvas();                            return true;
            case cmd::openCanvas:    openCanvas();                           return true;
            case cmd::saveCanvas:    saveCanvas();                           return true;
            case cmd::saveCanvasAs:  saveCanvasAs();                         return true;
            case cmd::revertCanvas:  revertCanvas();                         return true;
            case cmd::audioSettings: pluginHolder->showAudioSettingsDialog(); return true;
            default: break;
        }

        return false;
    }

    // ========================= the canvas document ===========================

    /** ⌘S. Straight to the file it came from, with no dialog — that is the whole
        point of a document having a file. Falls through to Save As the first time,
        when there is not one yet. */
    void saveCanvas()
    {
        // Only a project this version wrote is rewritten in place. A 0.3 / 0.4-dev
        // single file, or an exported Sound, goes through Save As: that makes it a
        // project folder next to it and leaves the original untouched. [NATIVE]
        if (! canvasFile.existsAsFile() || ! se::isProjectFile (canvasFile.getFullPathName().toStdString()))
        {
            saveCanvasAs();
            return;
        }

        writeProject (canvasFile.getParentDirectory(), canvasFile.getFileNameWithoutExtension());
    }

    /** Write the canvas to a file the user picks. The suffix makes this a document
        dialog rather than a bare save: macOS appends .sefx to a name typed without
        one, because the chooser's filter becomes the panel's allowed file types. */
    void saveCanvasAs()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Save Canvas",
                                                           canvasFileOrDefault(),
                                                           canvasWildcard());

        // No overwrite warning from the panel: it would speak about <Name>.sefx
        // while what is replaced is the folder <Name>/. saveAsProject asks itself.
        const auto flags = juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file != juce::File())
                saveAsProject (file);
        });
    }

    /** Read a canvas the user picks. */
    void openCanvas()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open Canvas",
                                                           canvasFileOrDefault(),
                                                           canvasWildcard());

        const auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file != juce::File())
                loadCanvasFile (file);
        });
    }

    /** Empty canvas, no file, back to "Untitled Canvas".

        Asks first, and so does Revert. This program has no undo — that is a
        deliberate inheritance, alert text and all — so a menu item that throws away
        an afternoon's work has no business being one click from the top of the
        File menu without a word. The close box on a single Window still asks
        nothing, because one Window is cheap to remake. */
    void newCanvas()
    {
        askThenDo ("Start a new canvas?",
                   "The Windows on the canvas will be thrown away. This cannot be undone.",
                   "New Canvas",
                   [this]
                   {
                       resetToDefaultState();
                       setCanvasFile (juce::File());
                   });
    }

    /** Back to the last saved version, throwing away everything since. */
    void revertCanvas()
    {
        if (! canvasFile.existsAsFile())
            return;

        const auto file = canvasFile;

        askThenDo ("Revert to the last saved canvas?",
                   "Everything changed since " + file.getFileName()
                       + " was last saved will be thrown away. This cannot be undone.",
                   "Revert",
                   [this, file] { loadCanvasFile (file); });
    }

    /** Put a .sefx on screen, from wherever it came from — the Open item, a file
        double-clicked in the Finder, or a path on the command line.

        The parse is done HERE, before the processor sees the bytes, purely so that
        a bad file can be reported. setStateInformation keeps the canvas the user
        already has when a blob does not parse, which is right for a host loading a
        session but silent — and on a fresh launch "keep what you have" means an
        empty canvas, which is indistinguishable from a file that opened fine and
        happened to be empty. Someone who just double-clicked a file deserves to be
        told it is damaged. */
    bool loadCanvasFile (const juce::File& file)
    {
        se::EngineState state;
        std::string err;

        if (! se::readSefx (file.getFullPathName().toStdString(), state, err))
        {
            showFailure ("Couldn't open the canvas", err);
            return false;
        }

        processor().loadState (std::move (state));
        setCanvasFile (file);
        return true;
    }

    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;

private:
    /** The canvas's name, which is its file's — or the name a canvas that has never
        been saved goes by. Not stored in the blob: a document is called what its
        file is called, so renaming it in the Finder renames it here, and there is
        no second copy of the name to disagree with the first. */
    static constexpr const char* kUntitledCanvas = "Untitled Canvas";

    /** Where the current file is remembered between launches, so the title bar
        still says what you were working on. */
    static constexpr const char* kCanvasFileKey = "canvasFile";

    juce::String canvasName() const
    {
        return canvasFile == juce::File() ? juce::String (kUntitledCanvas)
                                          : canvasFile.getFileNameWithoutExtension();
    }

    /** The window's name (for the Dock and Mission Control — the title bar itself
        is hidden) and the name at the left of the app's top bar. */
    void updateTitle()
    {
        setName (canvasName());
        if (auto* e = ourEditor()) e->setCanvasName (canvasName());
    }

    void setCanvasFile (const juce::File& file)
    {
        canvasFile = file;
        updateTitle();

        if (auto* props = pluginHolder->settings.get())
            props->setValue (kCanvasFileKey, file.getFullPathName());

        if (auto* e = ourEditor())
            e->commands().commandStatusChanged();   // Revert may have just become available
    }

    SoundEffectsAudioProcessor& processor() const
    {
        return static_cast<SoundEffectsAudioProcessor&> (*pluginHolder->processor);
    }

    /** Save As: the panel hands back where and under what name; the canvas becomes
        the folder `<Name>/` with `<Name>.sefx` and `Assets/` inside. Picked from
        inside a folder already called <Name> it is written there, not nested. */
    void saveAsProject (const juce::File& chosen)
    {
        const auto name = chosen.getFileNameWithoutExtension();
        const auto parent = chosen.getParentDirectory();
        const auto dir = parent.getFileName() == name ? parent : parent.getChildFile (name);
        const auto target = dir.getChildFile (name + se::kEngineStateFileSuffix);

        if (! target.existsAsFile())
        {
            writeProject (dir, name);
            return;
        }

        askThenDo ("Replace the project \"" + name + "\"?",
                   "The folder " + dir.getFileName() + " already holds a canvas with that name. "
                   "Saving replaces it. This cannot be undone.",
                   "Replace",
                   [this, dir, name] { writeProject (dir, name); });
    }

    void writeProject (const juce::File& dir, const juce::String& name)
    {
        std::string err;

        if (se::writeProject (dir.getFullPathName().toStdString(), name.toStdString(),
                              processor().captureState(), err))
            setCanvasFile (dir.getChildFile (name + se::kEngineStateFileSuffix));
        else
            showFailure ("Couldn't save the canvas", err);
    }

    /** "*.sefx" — the chooser's filter, and on macOS the save panel's allowed file
        types, which is what makes a bare typed name grow the extension. */
    static juce::String canvasWildcard()
    {
        return "*" + juce::String (se::kEngineStateFileSuffix);
    }

    /** Where a chooser opens, and what Save As pre-fills: the current file, or a
        name in Documents matching what the title bar has been saying.

        existsAsFile, not "is set" — the file may have gone away while we were
        running, and a save panel opened on a directory that is no longer there
        lands somewhere the user did not choose. Falling back to Documents keeps
        the panel somewhere real, and canvasName() keeps the NAME the title bar has
        been showing, so Save As offers to write the document you think you are
        editing rather than renaming it to Untitled behind your back. */
    juce::File canvasFileOrDefault() const
    {
        if (canvasFile.existsAsFile())
            return canvasFile;

        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile (canvasName() + se::kEngineStateFileSuffix);
    }

    /** Directories the system empties behind your back. Not a guess at which files
        matter — just the places macOS itself documents as disposable: /tmp and
        /var/tmp, which are cleared on boot, and /var/folders, which holds the
        per-user and per-process temp directories and is reaped a few days after
        each stops being used.

        The whole of /var/folders, not merely our own $TMPDIR. The path being judged
        was written by whatever process put it there — most often an earlier run of
        something else entirely — and its temp directory is exactly as disposable as
        ours while never being the one $TMPDIR names. $TMPDIR is still added on top,
        for the case where it has been pointed somewhere unusual.

        Deliberately NOT juce::File::tempDirectory: on macOS JUCE means
        ~/Library/Caches/SoundEffects by that, which is a different idea. */
    static bool isSomewhereTheSystemWillEmpty (const juce::File& file)
    {
        juce::Array<juce::File> volatiles { juce::File ("/tmp"),          juce::File ("/private/tmp"),
                                            juce::File ("/var/tmp"),      juce::File ("/private/var/tmp"),
                                            juce::File ("/var/folders"),  juce::File ("/private/var/folders") };

        const auto tmpdir = juce::SystemStats::getEnvironmentVariable ("TMPDIR", {});

        if (tmpdir.isNotEmpty() && juce::File::isAbsolutePath (tmpdir))
            volatiles.add (juce::File (tmpdir));

        for (const auto& dir : volatiles)
            if (file.isAChildOf (dir))
                return true;

        return false;
    }

    /** Is a remembered path still worth calling this canvas's document?

        Two ways it is not, and they are different failures. The file may simply be
        GONE — deleted, or on a volume that is not mounted — in which case the title
        bar would be naming something that does not exist and Revert would have
        nothing to go back to. Or the file may be right where it was, but in a
        temp directory: ⌘S would then quietly write the whole canvas, tens of
        megabytes of audio, to a path the system is going to throw away, and the
        title bar would say it was saved.

        The temp rule is restore-time ONLY. Saving to /tmp during a session is the
        user's business and is honoured without comment — Save As asked, and they
        answered. What has no defensible reading is a temp path surviving a quit
        and being presented on the next launch as the document you were working on:
        a temp directory is per-boot by definition, so a file kept there across
        launches was scratch, never a document. */
    static bool isStillADocument (const juce::File& file)
    {
        return file.existsAsFile() && ! isSomewhereTheSystemWillEmpty (file);
    }

    /** Menu text with a real ellipsis, which is what macOS menus use. */
    static juce::String ellipsis (const juce::String& s)
    {
        return s + juce::String::fromUTF8 ("\xE2\x80\xA6");
    }

    /** Confirm, then act — on OK only. Async, like every other dialog here. */
    template <typename Fn>
    void askThenDo (const juce::String& title, const juce::String& message,
                    const juce::String& okText, Fn&& action)
    {
        messageBox = juce::AlertWindow::showScopedAsync (
            juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
                                                          title, message, okText, "Cancel"),
            [onOk = std::forward<Fn> (action)] (int result) { if (result == 1) onOk(); });
    }

    SoundEffectsAudioProcessorEditor* ourEditor() const
    {
        return dynamic_cast<SoundEffectsAudioProcessorEditor*> (editor.get());
    }

    void showFailure (const juce::String& title, const juce::String& message)
    {
        messageBox = juce::AlertWindow::showScopedAsync (
            juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                    title, message),
            nullptr);
    }

    void resetToDefaultState()
    {
        pluginHolder->stopPlaying();
        detachEditor();
        pluginHolder->deletePlugin();

        if (auto* props = pluginHolder->settings.get())
            props->removeValue ("filterState");

        pluginHolder->createPlugin();
        attachEditor();
        pluginHolder->startPlaying();
    }

    void attachEditor()
    {
        auto* processor = pluginHolder->processor.get();
        if (processor == nullptr) return;

        editor.reset (processor->hasEditor() ? processor->createEditorIfNeeded()
                                             : new juce::GenericAudioProcessorEditor (*processor));
        if (editor == nullptr) return;

        // Hand the editor the things only the standalone app can answer, because
        // only the standalone owns an audio device. In a VST3/AU both stay null and
        // the record button is simply grey and inert — which is correct there.
        if (auto* ours = dynamic_cast<SoundEffectsAudioProcessorEditor*> (editor.get()))
        {
            ours->onShowAudioSettings = [this] { pluginHolder->showAudioSettingsDialog(); };

            // Polled by the editor's 30 Hz timer rather than pushed by a listener:
            // the editor is already refreshing everything else on that tick, and a
            // listener would have to be torn down and re-armed every time the editor
            // is rebuilt. Muted input is the default (StandalonePluginHolder's own),
            // so the record button starts grey and lights up when the user clears
            // "Mute audio input" in the settings dialog.
            ours->isAudioInputActive = [this]
            {
                return ! (bool) pluginHolder->getMuteInputValue().getValue();
            };
        }

        setContentNonOwned (editor.get(), false);

        // The editor owns the menus and the command manager; we answer the commands
        // only a standalone can (see getAllCommands), and supply the canvas's name.
        if (auto* ours = dynamic_cast<SoundEffectsAudioProcessorEditor*> (editor.get()))
        {
            ours->setHostCommandTarget (this);
            ours->setCanvasName (canvasName());
        }
    }

    void detachEditor()
    {
        if (auto* ours = dynamic_cast<SoundEffectsAudioProcessorEditor*> (editor.get()))
            ours->setHostCommandTarget (nullptr);
        clearContentComponent();

        if (editor != nullptr)
        {
            if (auto* processor = pluginHolder->processor.get())
                processor->editorBeingDeleted (editor.get());

            editor = nullptr;
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;


    /** The file this canvas came from, empty until it has been saved once. */
    juce::File canvasFile;

    // Both outlive the call that raises them: launchAsync returns immediately and
    // the panel calls back later, and an async alert is dismissed by the user, not
    // by us. Holding one of each means a second Save while a Save panel is up
    // replaces it rather than stacking, which is the behaviour the stock holder had.
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ScopedMessageBox messageBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

/**
 * The application. Modelled on juce::StandaloneFilterApp — the properties file, the
 * quit that saves plugin state — but it makes OUR window. The stock class is
 * compiled out entirely once JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP is set, so there
 * was never anything to subclass. If JUCE's wrapper changes, this is the file to
 * diff it against.
 */
class SoundEffectsStandaloneApp final : public juce::JUCEApplication
{
public:
    SoundEffectsStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";

        migrateLegacySettings (options);

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    /** A .sefx was opened while we are already running — double-clicked in the
        Finder, dropped on the Dock icon, or passed to `open`. macOS routes all of
        those to the running app rather than starting a second one, and JUCE's
        AppDelegate turns them into this call with the paths space-joined and
        quoted if they contain spaces. */
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openFirstCanvasIn (commandLine);
    }

    void initialise (const juce::String& commandLine) override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            jassertfalse;   // no displays: there is no window to make
            return;
        }

        // The design-system workbench (see Os8Gallery.h). The snapshot form writes
        // <name>.png (1x), <name>@2x.png and a 4x close-up of the UI elements,
        // <name>-ui@4x.png, then quits without touching the canvas or the audio device.
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (const int i = args.indexOf ("--gallery-snapshot"); i >= 0 && i + 1 < args.size())
        {
            const juce::File png (args[i + 1].unquoted());
            const auto sibling = [&png] (const char* suffix)
            {
                return png.getSiblingFile (png.getFileNameWithoutExtension() + suffix);
            };
            const bool ok = Os8Gallery::writeSnapshot (png, 1.0f)
                         && Os8Gallery::writeSnapshot (sibling ("@2x.png"), 2.0f)
                         && Os8Gallery::writeSnapshot (sibling ("-ui@4x.png"), 4.0f, Os8Gallery::uiElementsArea())
                         && Os8Gallery::writeSnapshot (sibling ("-fields@4x.png"), 4.0f,
                                                       Os8Gallery::fieldsArea().withWidth (720))
                         && Os8Gallery::writeSnapshot (sibling ("-sliders@4x.png"), 4.0f,
                                                       Os8Gallery::slidersArea().withWidth (720))
                         && Os8Gallery::writeSnapshot (sibling ("-transport@4x.png"), 4.0f,
                                                       Os8Gallery::transportArea().withWidth (720));
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        // The mixer strip in every state of Mixer-channel 509:9257 and the five strips
        // of the tinted example 636:11566, plus the panel's top, as 1x PNGs in a
        // directory — the pixel diff against Figma's exports. No canvas, no audio.
        if (const int i = args.indexOf ("--mixer-snapshot"); i >= 0 && i + 1 < args.size())
        {
            setApplicationReturnValue (writeMixerSnapshot (juce::File (args[i + 1].unquoted())) ? 0 : 1);
            quit();
            return;
        }

        // The Effects panel's header, its six group frames and every effect's tray,
        // each as a 1x PNG — the pixel diff against Figma's exports.
        if (const int i = args.indexOf ("--effects-snapshot"); i >= 0 && i + 1 < args.size())
        {
            setApplicationReturnValue (writeEffectsSnapshot (juce::File (args[i + 1].unquoted())) ? 0 : 1);
            quit();
            return;
        }

        // The Edit and Selection menus' popups (step 7), each as a 1x PNG with its
        // frame and shadow — the pixel diff against "Popups-settings" 543:7841.
        if (const int i = args.indexOf ("--edit-snapshot"); i >= 0 && i + 1 < args.size())
        {
            setApplicationReturnValue (writeEditSnapshot (juce::File (args[i + 1].unquoted())) ? 0 : 1);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            appProperties.getUserSettings());

        mainWindow->setVisible (true);
        mainWindow->adoptTopBarChrome();

        // After the main window, so the workbench opens in front of it.
        // The whole editor, off-screen: <name>.png, plus each application menu drawn
        // as it would drop down, <name>-menu-<Title>.png. A check of the top bar and
        // the menus that needs no screen (and no user at it).
        if (const int i = args.indexOf ("--app-snapshot"); i >= 0 && i + 1 < args.size())
        {
            setApplicationReturnValue (writeAppSnapshot (juce::File (args[i + 1].unquoted())) ? 0 : 1);
            quit();
            return;
        }

        if (args.contains ("--gallery"))
        {
            struct GalleryWindow : juce::DocumentWindow
            {
                GalleryWindow() : juce::DocumentWindow ("Os8 Gallery", juce::Colours::white, closeButton) {}
                void closeButtonPressed() override { setVisible (false); }
            };
            galleryWindow = std::make_unique<GalleryWindow>();
            galleryWindow->setUsingNativeTitleBar (true);
            galleryWindow->setContentOwned (new Os8Gallery(), true);
            galleryWindow->setResizable (true, false);
            galleryWindow->centreWithSize (galleryWindow->getWidth(), galleryWindow->getHeight());
            galleryWindow->setVisible (true);
        }

        // A path in argv — launching the binary from a shell. A Finder double-click
        // does NOT arrive this way even when it starts the app: LaunchServices sends
        // an open-document event, which the run loop delivers to
        // anotherInstanceStarted after initialise has returned. Both roads are
        // covered; neither can arrive before the window exists.
        openFirstCanvasIn (commandLine);
    }

    void shutdown() override
    {
        galleryWindow = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        // A modal dialog gets to answer for itself first; the quit is re-posted once
        // it has gone. Stock behaviour, kept verbatim.
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    static bool writePng (const juce::Image& image, const juce::File& png)
    {
        png.deleteFile();
        juce::FileOutputStream out (png);
        juce::PNGImageFormat format;
        return out.openedOk() && format.writeImageToStream (image, out);
    }

    /** See --effects-snapshot. */
    static bool writeEffectsSnapshot (const juce::File& dir)
    {
        dir.createDirectory();
        se::Engine engine;
        engine.prepare (44100.0, 512);

        // Figma's header example: "[sound-name]", slot 99, 22.1 kHz, 8 bits, 2 tracks, 4.52 s.
        se::DocumentInit init;
        init.name = "[sound-name]";
        init.rateHz = 22100.0;
        init.numTracks = 2;
        init.bps = 8;
        init.sizeBytes = (size_t) (4.52 * 22100.0);
        const uint64_t id = engine.addWindow (std::make_unique<se::SoundDocument> (init));
        engine.windowById (id)->slot = 99;

        EffectsPanelComponent panel (engine);
        panel.setSize (SidePanel::kWidth - 2, 3000);
        panel.setTargetWindow (id);
        panel.refresh();

        bool ok = true;
        const auto shoot = [&] (juce::Rectangle<int> r, const juce::String& name)
        {
            ok = writePng (panel.createComponentSnapshot (r, true, 1.0f), dir.getChildFile (name + ".png")) && ok;
        };

        shoot ({ 0, 0, panel.getWidth(), 48 }, "header");
        for (int g = 0; g < panel.numGroups(); ++g)
            shoot (panel.groupFrame (g), "group-" + juce::String (g + 1));

        for (const auto& m : se::effectRegistry())
        {
            const auto r = panel.openEffectNamed (juce::String (m.name));
            if (! r.isEmpty()) shoot (r.withWidth (panel.getWidth()), "tray-" + juce::String (m.name).replaceCharacter (' ', '-'));
        }

        // The Setup popups (6.3), over the Flanger tray they belong to, shadow included.
        {
            const auto tray = panel.openEffectNamed ("Flanger");
            const struct { bool del; const char* text; const char* file; } popups[] = {
                { false, "", "popup-save-empty" }, { false, "My flange", "popup-save" },
                { false, "Soft Flange", "popup-save-replace" }, { true, "Plain Flange - Slowest", "popup-delete" },
            };
            for (const auto& p : popups)
            {
                const auto r = panel.snapshotSetupPopup (p.del, juce::String::fromUTF8 (p.text));
                shoot (r, p.file);
                shoot (tray.getUnion (r).expanded (0, 2).withX (0).withWidth (panel.getWidth()), juce::String (p.file) + "-in-tray");
                panel.clearSnapshotPopups();
            }
        }

        // The two canvas palettes, frame only (their shadow is 2 px below the frame).
        ViewPalette view;
        view.setSize (ViewPalette::kMaxWidth, ViewPalette::kHeight);
        view.setKeyboardShown (true);
        view.setZoom (1.0f);
        ok = writePng (view.createComponentSnapshot ({ 0, 0, ViewPalette::kMaxWidth, ViewPalette::kHeight }, true, 1.0f),
                       dir.getChildFile ("palette-view.png")) && ok;

        KeyboardPalette keys (engine);
        keys.setTargetWindow (id);
        keys.setSize (KeyboardPalette::kWidth, KeyboardPalette::kHeight);
        ok = writePng (keys.createComponentSnapshot ({ 0, 0, KeyboardPalette::kWidth, KeyboardPalette::kHeight }, true, 1.0f),
                       dir.getChildFile ("palette-keyboard.png")) && ok;
        return ok;
    }

    /** See --edit-snapshot. The documents are set up to give each popup the state its
        Figma variant shows: a two-track sound with its second track selected (Leave
        Silence's second radio, Shift Sound's "channel number 2"). */
    static bool writeEditSnapshot (const juce::File& dir)
    {
        dir.createDirectory();
        editpopups::resetMemory();

        se::DocumentInit init;
        init.numTracks = 2;
        init.sizeBytes = 22254;
        se::SoundDocument doc (init);
        doc.firstSelTrack = doc.lastSelTrack = 2;

        bool ok = true;
        const auto shoot = [&] (editpopups::Built b, const char* name)
        {
            b.field->setSnapshotState (os8::FieldState::highlighted);
            auto& p = *b.popup;
            ok = writePng (p.createComponentSnapshot (p.getLocalBounds(), true, 1.0f),
                           dir.getChildFile (juce::String (name) + ".png")) && ok;
        };
        shoot (editpopups::leaveSilence (doc, {}), "popup-leave-silence");
        shoot (editpopups::shiftSound (doc, {}), "popup-shift-sound");
        shoot (editpopups::selectExactly (doc, {}), "popup-select-exactly");   // a caret: "the insertion point."
        doc.selEnd = 100;                                                       // a selection: "the selection" radio
        shoot (editpopups::selectPeak (doc, {}), "popup-select-peak");
        return ok;
    }

    /** See --mixer-snapshot. */
    static bool writeMixerSnapshot (const juce::File& dir)
    {
        dir.createDirectory();
        se::Engine engine;
        engine.prepare (44100.0, 512);

        // The design's frozen levels: VU bars 63 and 68 px long, the fader thumb at
        // x 74, the balance centred.
        const auto ampForBar = [] (int px)
        {
            const float pos = ((float) px - 5.5f) / 132.0f;
            return se::Engine::dbToGain (MixerStrip::posToDb (pos));
        };
        const auto addSound = [&] (const char* name, int tint, int slot, bool muted)
        {
            se::DocumentInit init;
            init.name = name;
            init.numTracks = 1;
            init.bps = 8;
            init.sizeBytes = 16;
            const uint64_t id = engine.addWindow (std::make_unique<se::SoundDocument> (init));
            auto* w = engine.windowById (id);
            w->tint = tint;
            w->slot = slot;
            w->muted = muted;
            w->gain = se::Engine::dbToGain (MixerStrip::posToDb (74.0f / 132.0f));
            w->meterLeft = ampForBar (63);
            w->meterRight = ampForBar (68);
            return id;
        };
        const auto shoot = [] (juce::Component& c, const juce::File& png)
        {
            return writePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), png);
        };

        bool ok = true;
        struct Variant { const char* file; bool hover, active, muted; };
        const Variant variants[] = {
            { "default-off", false, false, false }, { "hover-off", true, false, false },
            { "active-off",  false, true,  false }, { "default-on", false, false, true },
            { "hover-on",    true,  false, true  }, { "active-on",  false, true,  true },
        };
        for (const auto& v : variants)
        {
            MixerStrip strip (engine, addSound ("Untitled Sound", -1, 98, v.muted));
            strip.setSelected (v.active);
            strip.setForcedHover (v.hover);
            strip.setSize (MixerStrip::kWidth, v.active ? MixerStrip::kActiveHeight : MixerStrip::kHeight);
            strip.refreshState (false);
            ok = shoot (strip, dir.getChildFile (juce::String ("strip-") + v.file + ".png")) && ok;
        }

        for (auto id : std::vector<uint64_t> (engine.windowIds())) engine.removeWindow (id);
        engine.collectRetired();

        // 636:11566: Drums (Seashell), Vocals 01 (Glacier), Buzzing Sounds (Soft
        // butter, selected), Frantic Percs (Soft lilac, muted), Screaming racoons.
        const uint64_t ex[] = {
            addSound ("Drums", 4, 0, false), addSound ("Vocals 01", 10, 1, false),
            addSound ("Buzzing Sounds", 2, 2, false), addSound ("Frantic Percs", 8, 3, true),
            addSound ("Screaming racoons", 0, 4, false),
        };
        for (int k = 0; k < 5; ++k)
        {
            MixerStrip strip (engine, ex[k]);
            strip.setSelected (k == 2);
            strip.setSize (MixerStrip::kWidth, k == 2 ? MixerStrip::kActiveHeight : MixerStrip::kHeight);
            strip.refreshState (false);
            ok = shoot (strip, dir.getChildFile ("example-" + juce::String (k + 1) + ".png")) && ok;
        }

        MixerComponent mixer (engine);
        SidePanel panel ("Mixer", mixer);
        panel.setTitleRule (true);
        panel.setSize (SidePanel::kWidth, 320);
        mixer.refresh();
        mixer.setSelectedWindow (ex[2]);
        mixer.refresh();
        ok = shoot (panel, dir.getChildFile ("panel.png")) && ok;
        return ok;
    }

    /** See --app-snapshot. Menus are drawn through the same LookAndFeel calls a
        real popup makes, laid out the way JUCE lays them (rows from x = 0 in a
        window the border wider on each side). */
    static bool writeAppSnapshot (const juce::File& png)
    {
        std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorIfNeeded());
        auto* ours = dynamic_cast<SoundEffectsAudioProcessorEditor*> (editor.get());
        if (ours == nullptr) return false;

        ours->setCanvasName ("Untitled Canvas");

        // One real sound Window, as Figma's Sound-window shows one: two tracks at
        // 22.1 kHz / 8 bits, 4.52 s long, with a selection in the first track.
        if (auto* sfx = dynamic_cast<SoundEffectsAudioProcessor*> (processor.get()))
        {
            const int frames = (int) (22050.0 * 4.52);
            std::vector<std::vector<float>> channels (2, std::vector<float> ((size_t) frames));
            juce::Random rng (7);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < frames; ++i)
                {
                    const float env = 0.2f + 0.8f * std::abs (std::sin ((float) i / 2600.0f + (float) c));
                    channels[(size_t) c][(size_t) i] = env * (rng.nextFloat() * 2.0f - 1.0f) * 0.9f;
                }
            auto doc = se::makeDocumentFromFloat (channels, 22050.0, 8, "Window name");
            const auto fs = (size_t) doc->frameSize();
            doc->firstSelTrack = doc->lastSelTrack = 1;
            se::setSelection (*doc, (size_t) (frames * 0.33) * fs, (size_t) (frames * 0.5) * fs);
            sfx->engine().addWindow (std::move (doc));

            // Drain the engine's queue so the Window exists, then let the editor
            // build its component.
            processor->prepareToPlay (44100.0, 512);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            processor->processBlock (buffer, midi);
            ours->refreshNow();
        }
        ours->setSize (1440, 900);
        ours->refreshNow();
        bool ok = writePng (ours->createComponentSnapshot (ours->getLocalBounds(), true, 1.0f), png);

        const auto names = ours->getMenuBarNames();
        for (int m = 0; m < names.size(); ++m)
        {
            auto menu = ours->getMenuForIndex (m, names[m]);
            auto& lf = Os8AppMenuLookAndFeel::instance();
            const juce::PopupMenu::Options options;
            const int border = lf.getPopupMenuBorderSizeWithOptions (options);

            int width = 0, height = 0;
            std::vector<std::pair<juce::PopupMenu::Item, int>> rows;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                int w = 0, h = 0;
                auto measured = item.text;
                if (item.commandManager != nullptr)
                    if (auto* info = item.commandManager->getCommandForID (item.itemID))
                        if (! info->defaultKeypresses.isEmpty())
                            measured << "   " << info->defaultKeypresses.getReference (0).getTextDescriptionWithIcons();
                lf.getIdealPopupMenuItemSizeWithOptions (measured, item.isSeparator, 16, w, h, options);
                width = juce::jmax (width, w);
                rows.emplace_back (item, h);
                height += h;
            }

            juce::Image image (juce::Image::ARGB, width + 2 * border, height + 2 * border, true);
            juce::Graphics g (image);
            g.fillAll (juce::Colours::white);
            lf.drawPopupMenuBackgroundWithOptions (g, image.getWidth(), image.getHeight(), options);
            int y = border;
            for (auto& [item, h] : rows)
            {
                lf.drawPopupMenuItemWithOptions (g, { 0, y, width, h }, false, item, options);
                y += h;
            }
            ok = ok && writePng (image, png.getSiblingFile (png.getFileNameWithoutExtension() + "-menu-" + names[m] + ".png"));
        }

        processor->editorBeingDeleted (editor.get());
        editor = nullptr;
        return ok;
    }

    /** The app was called "SoundEffect", singular, until the name was corrected, and
        the properties file is named after the app — so the rename on its own would
        have left the autosaved canvas, the audio device choice and the window size
        behind in a file nothing reads any more. Copy the old one across the first
        time we start under the new name. The original is left where it is: it is a
        kilobyte, and it is the way back if this build is ever downgraded. */
    static void migrateLegacySettings (const juce::PropertiesFile::Options& options)
    {
        const juce::File current = options.getDefaultFile();

        if (current.existsAsFile())
            return;

        juce::PropertiesFile::Options legacy = options;
        legacy.applicationName = "SoundEffect";

        const juce::File previous = legacy.getDefaultFile();

        if (previous.existsAsFile() && previous != current)
            previous.copyFileTo (current);
    }

    /** Open the first canvas named in a command line, and bring the app forward so
        the result is visible. One only: there is a single window and a single
        canvas in it, so opening several would just be the last one winning. */
    void openFirstCanvasIn (const juce::String& commandLine)
    {
        if (mainWindow == nullptr || commandLine.isEmpty())
            return;

        // fromTokens with quoting, to match how the paths were joined.
        for (const auto& token : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File file (token.unquoted());

            if (file.existsAsFile() && mainWindow->loadCanvasFile (file))
            {
                juce::Process::makeForegroundProcess();
                mainWindow->toFront (true);
                return;
            }
        }
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::DocumentWindow> galleryWindow;
};

} // namespace

JUCE_CREATE_APPLICATION_DEFINE (SoundEffectsStandaloneApp)
