// StandaloneApp — the app's OWN macOS window, as opposed to the sound Windows.
//
// Two window vocabularies live in this program and they must not be confused. The
// Windows on the canvas are faux-System 7 down to the pixel: flat grey face, 1px
// black frames, hand-painted close and zoom boxes. That is the product. But the
// thing those Windows float inside is a macOS application window, and a macOS
// window wears macOS chrome — traffic lights at the top left, a working zoom
// button, a real menu bar.
//
// JUCE's stock standalone app gives it neither. It hands you a DocumentWindow with
// a JUCE-DRAWN title bar (a dark strip, the app name centred, two hand-painted
// buttons at the right) and squeezes an "Options" button in beside them. Inside the
// app that idiom is deliberate; around the app it just looks like a plugin host
// that lost its host.
//
// So this file replaces the stock app — JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP tells
// the wrapper to call OUR juce_CreateApplication() instead of defining its own —
// with the same app wearing the system's clothes, opened at the full size of the
// screen. The size matters more than it sounds: every sound the user opens becomes
// a Window laid out on the canvas, and a canvas the size of a dialog is a canvas
// you spend the session dragging things around inside.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <memory>

namespace {

/**
 * The stock Options button's menu, promoted to the menu bar.
 *
 * Under a native title bar there is no JUCE title bar left to hang that button in,
 * and its four items are not expendable — Audio/MIDI Settings is the only way to
 * pick an output device. On a Mac they belong in the menu bar regardless, which is
 * where someone hunting for audio settings looks first.
 *
 * The item IDs are the wrapper's own (juce_StandaloneFilterWindow.h,
 * handleMenuResult), so this is a relocation, not a reimplementation.
 */
class OptionsMenuBar final : public juce::MenuBarModel
{
public:
    explicit OptionsMenuBar (juce::StandaloneFilterWindow& w) : window (w) {}

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
    void menuItemSelected (int itemId, int) override { window.handleMenuResult (itemId); }

private:
    juce::StandaloneFilterWindow& window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsMenuBar)
};

/** The stock standalone window, re-dressed as a Mac window. */
class MainWindow final : public juce::StandaloneFilterWindow
{
public:
    MainWindow (const juce::String& title, juce::Colour background,
                std::unique_ptr<juce::StandalonePluginHolder> holder)
        : juce::StandaloneFilterWindow (title, background, std::move (holder))
    {
        // ORDER MATTERS. setUsingNativeTitleBar() is what recreates the desktop
        // window, and the peer picks up its style flags — which buttons the system
        // title bar gets — at that moment. Ask for them afterwards and the flags
        // are set on a peer that has already been built.
        setResizable (true, false);   // no JUCE corner gripper; the OS edge-resizes
        setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, false);
        setUsingNativeTitleBar (true);

        // Fill the screen. userArea is precisely the rectangle macOS leaves between
        // the menu bar and the Dock, on whichever display is primary — so this
        // needs no arithmetic of its own and stays right when the Dock moves,
        // hides, or changes size.
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            setBounds (display->userArea);

        optionsMenu = std::make_unique<OptionsMenuBar> (*this);
        juce::MenuBarModel::setMacMainMenu (optionsMenu.get());
    }

    ~MainWindow() override
    {
        // Before optionsMenu dies: the menu bar holds a raw pointer to it.
        juce::MenuBarModel::setMacMainMenu (nullptr);
    }

    // The base class lays its Options button out against a JUCE title bar. Under a
    // native one getTitleBarHeight() is 0, so that call would size the button to a
    // negative height — harmless, but it is laying out a control that no longer has
    // anywhere to be. Its items are in the menu bar now, so skip it and let
    // DocumentWindow place the content.
    void resized() override { juce::DocumentWindow::resized(); }

private:
    std::unique_ptr<OptionsMenuBar> optionsMenu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

/**
 * A copy of juce::StandaloneFilterApp with one method changed.
 *
 * Everything but createWindow() is the stock behaviour — the properties file, the
 * quit handling that saves plugin state, the channel configuration. Subclassing
 * would be tidier, but the stock class is compiled out entirely the moment
 * JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP is set, so there is nothing left to
 * subclass. If JUCE's wrapper changes, this is the file to diff it against.
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
            std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false));

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
