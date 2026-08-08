// StandaloneApp — the app's OWN macOS window, as opposed to the sound Windows.
//
// Two window vocabularies live in this program and they must not be confused. The
// Windows on the canvas are faux-System 7 down to the pixel: flat grey face, 1px
// black frames, hand-painted close and zoom boxes. That is the product. But the
// thing those Windows float inside is a macOS application window, and a macOS
// window wears macOS chrome — traffic lights at the top left, a working zoom
// button, a real menu bar.
//
// JUCE's stock standalone gives it none of that. It hands you a DocumentWindow with
// a JUCE-DRAWN title bar, an "Options" button squeezed in beside the buttons, and a
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

#include <memory>

namespace {

class MainWindow;

/** Everything the File menu can do. Commands rather than plain menu items, so that
    one declaration in MainWindow::getCommandInfo carries the name, the key and
    whether the item is greyed — and macOS gets a real key equivalent out of it
    instead of decorative shortcut text. */
namespace CommandIDs
{
    enum
    {
        newSound = 0x5e01,   // Window-level: one more sound on the canvas
        openSound,
        closeSound,

        newCanvas,           // canvas-level: the document the Windows live on
        openCanvas,
        saveCanvas,
        saveCanvasAs,
        revertCanvas,

        audioSettings
    };
}

/**
 * The File menu, in the menu bar where it belongs.
 *
 * The original's File menu is the model (New / Open / Close / Save / Save As /
 * Revert / Preferences / Quit) and the two halves here are the two things this
 * program has that can be opened and saved. The TOP half is the original's, verb
 * for verb, aimed at a sound Window. The BOTTOM half is the canvas those Windows
 * live on, which the original had no equivalent of because it had no canvas.
 *
 * Quit is deliberately absent: System 7 had no application menu, so File was the
 * only place to put it, and macOS has put Quit in the app menu since 2001. It is
 * already there, with ⌘Q already working. [NATIVE]
 */
class FileMenuBar final : public juce::MenuBarModel
{
public:
    explicit FileMenuBar (juce::ApplicationCommandManager& cm) : commands (cm)
    {
        setApplicationCommandManagerToWatch (&commands);   // so greying stays live
    }

    juce::StringArray getMenuBarNames() override { return { "File" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String&) override
    {
        juce::PopupMenu m;
        m.addCommandItem (&commands, CommandIDs::newSound);
        m.addCommandItem (&commands, CommandIDs::openSound);
        m.addCommandItem (&commands, CommandIDs::closeSound);
        m.addSeparator();
        m.addCommandItem (&commands, CommandIDs::newCanvas);
        m.addCommandItem (&commands, CommandIDs::openCanvas);
        m.addCommandItem (&commands, CommandIDs::saveCanvas);
        m.addCommandItem (&commands, CommandIDs::saveCanvasAs);
        m.addCommandItem (&commands, CommandIDs::revertCanvas);
        m.addSeparator();

        juce::PopupMenu preferences;
        preferences.addCommandItem (&commands, CommandIDs::audioSettings);
        m.addSubMenu ("Preferences", preferences);

        return m;
    }

    /** Nothing, and it must stay nothing. A command item is dispatched by the
        command manager when it is picked — and JUCE then calls this as well, so
        doing the work here too would run every item twice. */
    void menuItemSelected (int, int) override {}

private:
    juce::ApplicationCommandManager& commands;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileMenuBar)
};

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
        if (auto* props = pluginHolder->settings.get())
            canvasFile = juce::File (props->getValue (kCanvasFileKey));

        updateTitle();

        commands.registerAllCommandsForTarget (this);
        commands.setFirstCommandTarget (this);
        addKeyListener (commands.getKeyMappings());

        fileMenu = std::make_unique<FileMenuBar> (commands);
        juce::MenuBarModel::setMacMainMenu (fileMenu.get());
    }

    ~MainWindow() override
    {
        // Before fileMenu dies: the menu bar holds a raw pointer to it.
        juce::MenuBarModel::setMacMainMenu (nullptr);
        removeKeyListener (commands.getKeyMappings());
        commands.setFirstCommandTarget (nullptr);

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

    void getAllCommands (juce::Array<juce::CommandID>& c) override
    {
        c.addArray ({ CommandIDs::newSound,   CommandIDs::openSound,    CommandIDs::closeSound,
                      CommandIDs::newCanvas,  CommandIDs::openCanvas,   CommandIDs::saveCanvas,
                      CommandIDs::saveCanvasAs, CommandIDs::revertCanvas,
                      CommandIDs::audioSettings });
    }

    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& r) override
    {
        using Mods = juce::ModifierKeys;
        const auto cmd = Mods::commandModifier;

        switch (id)
        {
            case CommandIDs::newSound:
                r.setInfo ("New", "Add an empty sound Window to the canvas", "File", 0);
                r.addDefaultKeypress ('N', cmd);
                break;

            case CommandIDs::openSound:
                r.setInfo (ellipsis ("Open"), "Import an audio file into a new Window", "File", 0);
                r.addDefaultKeypress ('O', cmd);
                break;

            case CommandIDs::closeSound:
                r.setInfo ("Close", "Close the focused Window", "File", 0);
                r.addDefaultKeypress ('W', cmd);
                r.setActive (ourEditor() != nullptr && ourEditor()->hasFocusedWindow());
                break;

            case CommandIDs::newCanvas:
                r.setInfo ("New Canvas", "Start again with an empty canvas", "File", 0);
                break;

            case CommandIDs::openCanvas:
                r.setInfo (ellipsis ("Open Canvas"), "Open a saved canvas", "File", 0);
                break;

            case CommandIDs::saveCanvas:
                // Never greyed, unlike the original's Save. There is nothing to be
                // gained by refusing ⌘S: with no file yet it means Save As, which
                // is what every program written since has done. [NATIVE]
                r.setInfo ("Save Canvas", "Save the canvas", "File", 0);
                r.addDefaultKeypress ('S', cmd);
                break;

            case CommandIDs::saveCanvasAs:
                r.setInfo (ellipsis ("Save Canvas As"), "Save the canvas to a new file", "File", 0);
                r.addDefaultKeypress ('S', cmd | Mods::shiftModifier);
                break;

            case CommandIDs::revertCanvas:
                r.setInfo ("Revert Canvas", "Go back to the last saved version", "File", 0);
                r.setActive (canvasFile.existsAsFile());
                break;

            case CommandIDs::audioSettings:
                r.setInfo (ellipsis ("Audio/MIDI Settings"), "Choose the audio device", "File", 0);
                r.addDefaultKeypress (',', cmd);
                break;

            default: break;
        }
    }

    bool perform (const InvocationInfo& info) override
    {
        switch (info.commandID)
        {
            case CommandIDs::newSound:      if (auto* e = ourEditor()) e->newWindow();          return true;
            case CommandIDs::openSound:     if (auto* e = ourEditor()) e->openFile();           return true;
            case CommandIDs::closeSound:    if (auto* e = ourEditor()) e->closeFocusedWindow(); return true;

            case CommandIDs::newCanvas:     newCanvas();                            return true;
            case CommandIDs::openCanvas:    openCanvas();                           return true;
            case CommandIDs::saveCanvas:    saveCanvas();                           return true;
            case CommandIDs::saveCanvasAs:  saveCanvasAs();                         return true;
            case CommandIDs::revertCanvas:  revertCanvas();                         return true;

            case CommandIDs::audioSettings: pluginHolder->showAudioSettingsDialog(); return true;
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
        if (! canvasFile.existsAsFile())
        {
            saveCanvasAs();
            return;
        }

        writeCanvasTo (canvasFile);
    }

    /** Write the canvas to a file the user picks. The suffix makes this a document
        dialog rather than a bare save: macOS appends .sefx to a name typed without
        one, because the chooser's filter becomes the panel's allowed file types. */
    void saveCanvasAs()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Save Canvas",
                                                           canvasFileOrDefault(),
                                                           canvasWildcard());

        const auto flags = juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file != juce::File())
                writeCanvasTo (file);
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
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data))
        {
            showFailure ("Couldn't open the canvas",
                         file.getFileName() + " could not be read.");
            return false;
        }

        se::EngineState probe;

        if (! se::parseEngineState (data.getData(), data.getSize(), probe))
        {
            showFailure ("Not a SoundEffects canvas",
                         file.getFileName() + " is damaged, or was written by "
                         "a newer version of SoundEffects than this one.");
            return false;
        }

        pluginHolder->processor->setStateInformation (data.getData(), (int) data.getSize());
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

    void updateTitle() { setName (canvasName()); }

    void setCanvasFile (const juce::File& file)
    {
        canvasFile = file;
        updateTitle();

        if (auto* props = pluginHolder->settings.get())
            props->setValue (kCanvasFileKey, file.getFullPathName());

        commands.commandStatusChanged();   // Revert may have just become available
    }

    void writeCanvasTo (const juce::File& file)
    {
        juce::MemoryBlock data;
        pluginHolder->processor->getStateInformation (data);

        if (file.replaceWithData (data.getData(), data.getSize()))
            setCanvasFile (file);
        else
            showFailure ("Couldn't save the canvas",
                         "Nothing could be written to " + file.getFileName()
                             + ". Check that the disk is not full and that the file is not locked.");
    }

    /** "*.sefx" — the chooser's filter, and on macOS the save panel's allowed file
        types, which is what makes a bare typed name grow the extension. */
    static juce::String canvasWildcard()
    {
        return "*" + juce::String (se::kEngineStateFileSuffix);
    }

    /** Where a chooser opens, and what Save As pre-fills: the current file, or a
        name in Documents matching what the title bar has been saying. */
    juce::File canvasFileOrDefault() const
    {
        if (canvasFile != juce::File())
            return canvasFile;

        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile (juce::String (kUntitledCanvas) + se::kEngineStateFileSuffix);
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
    }

    void detachEditor()
    {
        clearContentComponent();

        if (editor != nullptr)
        {
            if (auto* processor = pluginHolder->processor.get())
                processor->editorBeingDeleted (editor.get());

            editor = nullptr;
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;

    // Declared BEFORE the menu bar, which holds a reference to it, and destroyed
    // after — the menu is watching it for enable/disable changes.
    juce::ApplicationCommandManager commands;
    std::unique_ptr<FileMenuBar> fileMenu;

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

        mainWindow = std::make_unique<MainWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            appProperties.getUserSettings());

        mainWindow->setVisible (true);

        // A path in argv — launching the binary from a shell. A Finder double-click
        // does NOT arrive this way even when it starts the app: LaunchServices sends
        // an open-document event, which the run loop delivers to
        // anotherInstanceStarted after initialise has returned. Both roads are
        // covered; neither can arrive before the window exists.
        openFirstCanvasIn (commandLine);
    }

    void shutdown() override
    {
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
};

} // namespace

JUCE_CREATE_APPLICATION_DEFINE (SoundEffectsStandaloneApp)
