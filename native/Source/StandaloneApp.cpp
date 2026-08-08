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

/**
 * What the stock Options button offered, in the menu bar where it belongs.
 *
 * Audio/MIDI Settings and Clear are still the wrapper's, so those are a relocation
 * rather than a reimplementation. Save and Open are ours: the wrapper's versions
 * put up dialogs titled "Save current state" / "Load a saved state", which are
 * hard-coded TRANS() literals inside StandalonePluginHolder with no way in from
 * outside, and they cannot tell a damaged file from an empty canvas. See
 * MainWindow::saveCanvas / openCanvas.
 */
class OptionsMenuBar final : public juce::MenuBarModel
{
public:
    explicit OptionsMenuBar (MainWindow& w) : window (w) {}

    juce::StringArray getMenuBarNames() override { return { "Options" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String&) override
    {
        juce::PopupMenu m;
        m.addItem (1, juce::String::fromUTF8 ("Audio/MIDI Settings\xE2\x80\xA6"));
        m.addSeparator();
        m.addItem (2, juce::String::fromUTF8 ("Save Canvas\xE2\x80\xA6"));
        m.addItem (3, juce::String::fromUTF8 ("Open Canvas\xE2\x80\xA6"));
        m.addSeparator();
        m.addItem (4, "Clear Canvas");
        return m;
    }

    // No shortcuts on any of these, deliberately: the editor already owns ⌘N and
    // ⌘O for New and Open, and a menu item would swallow the key before the
    // editor's keyPressed ever saw it.
    void menuItemSelected (int itemId, int) override;

private:
    MainWindow& window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsMenuBar)
};

/** The app window: a plain Mac window with the plugin editor as its content. */
class MainWindow final : public juce::DocumentWindow
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

        optionsMenu = std::make_unique<OptionsMenuBar> (*this);
        juce::MenuBarModel::setMacMainMenu (optionsMenu.get());
    }

    ~MainWindow() override
    {
        // Before optionsMenu dies: the menu bar holds a raw pointer to it.
        juce::MenuBarModel::setMacMainMenu (nullptr);

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

    /** Run an Options item. Clear throws the processor and its editor away and
        builds new ones, so the editor is re-attached — and re-hooked — around it.

        Save and Open are the processor's state, which for this program is the
        user's WORK — the whole canvas of Windows, tracks and audio (see
        engine/EngineState.h). They read as "save my work" and they mean it, so
        they are named for the work and not for the plumbing's word, "state". */
    void runOptionsItem (int itemId)
    {
        switch (itemId)
        {
            case 1: pluginHolder->showAudioSettingsDialog(); break;
            case 2: saveCanvas();                           break;
            case 3: openCanvas();                           break;
            case 4: resetToDefaultState();                  break;
            default: break;
        }
    }

    /** Write the canvas to a file the user picks. The suffix makes this a document
        dialog rather than a bare save: macOS appends .sefx to a name typed without
        one, because the chooser's filter becomes the panel's allowed file types. */
    void saveCanvas()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Save Canvas",
                                                           lastCanvasFile(),
                                                           canvasWildcard());

        const auto flags = juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File())
                return;   // cancelled

            rememberCanvasFile (file);

            juce::MemoryBlock data;
            pluginHolder->processor->getStateInformation (data);

            if (! file.replaceWithData (data.getData(), data.getSize()))
                showFailure ("Couldn't save the canvas",
                             "Nothing could be written to " + file.getFileName()
                                 + ". Check that the disk is not full and that the file is not locked.");
        });
    }

    /** Read a canvas the user picks. */
    void openCanvas()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open Canvas",
                                                           lastCanvasFile(),
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
            showFailure ("Not a SoundEffect canvas",
                         file.getFileName() + " is damaged, or was written by "
                         "a newer version of SoundEffect than this one.");
            return false;
        }

        pluginHolder->processor->setStateInformation (data.getData(), (int) data.getSize());
        rememberCanvasFile (file);
        return true;
    }

    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;

private:
    /** "*.sefx" — the chooser's filter, and on macOS the save panel's allowed file
        types, which is what makes a bare typed name grow the extension. */
    static juce::String canvasWildcard()
    {
        return "*" + juce::String (se::kEngineStateFileSuffix);
    }

    /** Where the chooser opens, and what it pre-fills. Same settings key the stock
        holder used, so a path remembered before this existed still comes back. */
    juce::File lastCanvasFile() const
    {
        if (auto* props = pluginHolder->settings.get())
        {
            const juce::File remembered (props->getValue ("lastStateFile"));

            if (remembered != juce::File())
                return remembered;
        }

        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Untitled Canvas" + juce::String (se::kEngineStateFileSuffix));
    }

    void rememberCanvasFile (const juce::File& file)
    {
        if (auto* props = pluginHolder->settings.get())
            props->setValue ("lastStateFile", file.getFullPathName());
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
        if (auto* ours = dynamic_cast<SoundEffectAudioProcessorEditor*> (editor.get()))
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
    std::unique_ptr<OptionsMenuBar> optionsMenu;

    // Both outlive the call that raises them: launchAsync returns immediately and
    // the panel calls back later, and an async alert is dismissed by the user, not
    // by us. Holding one of each means a second Save while a Save panel is up
    // replaces it rather than stacking, which is the behaviour the stock holder had.
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ScopedMessageBox messageBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

// Out of line: the menu bar is declared before the window it drives, so MainWindow
// is only an incomplete type up there.
void OptionsMenuBar::menuItemSelected (int itemId, int)
{
    window.runOptionsItem (itemId);
}

/**
 * The application. Modelled on juce::StandaloneFilterApp — the properties file, the
 * quit that saves plugin state — but it makes OUR window. The stock class is
 * compiled out entirely once JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP is set, so there
 * was never anything to subclass. If JUCE's wrapper changes, this is the file to
 * diff it against.
 */
class SoundEffectStandaloneApp final : public juce::JUCEApplication
{
public:
    SoundEffectStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";

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

JUCE_CREATE_APPLICATION_DEFINE (SoundEffectStandaloneApp)
