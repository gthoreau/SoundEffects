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
 * The four items are the wrapper's own, so this is a relocation rather than a
 * reimplementation — the difference being that each now calls straight into
 * StandalonePluginHolder instead of going through a window that no longer exists.
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
        m.addItem (2, juce::String::fromUTF8 ("Save Current State\xE2\x80\xA6"));
        m.addItem (3, juce::String::fromUTF8 ("Load a Saved State\xE2\x80\xA6"));
        m.addSeparator();
        m.addItem (4, "Reset to Default State");
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

    /** Run an Options item. Reset throws the processor and its editor away and
        builds new ones, so the editor is re-attached — and re-hooked — around it. */
    void runOptionsItem (int itemId)
    {
        switch (itemId)
        {
            case 1: pluginHolder->showAudioSettingsDialog(); break;
            case 2: pluginHolder->askUserToSaveState();      break;
            case 3: pluginHolder->askUserToLoadState();      break;
            case 4: resetToDefaultState();                   break;
            default: break;
        }
    }

    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;

private:
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
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
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
    juce::ApplicationProperties appProperties;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace

JUCE_CREATE_APPLICATION_DEFINE (SoundEffectStandaloneApp)
