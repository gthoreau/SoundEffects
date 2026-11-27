// AppPrefs — the application-level choices the user makes once and expects to find
// again: Auto-apply, the time unit, which side panels are open, and (from step 6)
// the effect setups.
//
// Its own file, ~/Library/Application Support/SoundEffects/Preferences.settings,
// rather than the standalone host's settings: the standalone and the plugin both
// read it, so a setup saved in one is there in the other. Message thread only.

#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AppPrefs : public juce::DeletedAtShutdown
{
public:
    /** The shared instance, created on first use and saved and freed at shutdown. */
    static AppPrefs& get();

    // Keys, for listeners.
    static constexpr const char* autoApplyKey = "autoApply";
    static constexpr const char* timeUnitKey = "timeUnit";
    static constexpr const char* colorThemeShownKey = "colorThemeShown";

    /** 1-click effects apply straight away (the spec's default) or open a tray. */
    bool autoApply() const;
    void setAutoApply (bool);

    /** The selection readout's unit: 0 = s, 1 = ms (the default), 2 = samples. */
    int timeUnit() const;
    void setTimeUnit (int);

    /** Canvas ▸ Show/Hide Color Theme: the Sounds' colours (the default) or the
        sober macos8 grey and lavender. Read it through os8::themeColourFor. */
    bool colorThemeShown() const;
    void setColorThemeShown (bool);

    /** The two side panels: shown at all (⇥), and each one collapsed to its title. */
    bool sidePanelsShown() const;
    void setSidePanelsShown (bool);
    bool mixerCollapsed() const;
    void setMixerCollapsed (bool);
    bool effectsCollapsed() const;
    void setEffectsCollapsed (bool);
    /** The Magnetophon, under the Mixer: collapsed to its title bar. */
    bool magnetophonCollapsed() const;
    void setMagnetophonCollapsed (bool);

    /** An alert the user answered with "Don't show again" (0.6.0 step 5.2), one
        per question. There is no way back in the app. */
    bool alertHidden (const juce::String& key) const;
    void setAlertHidden (const juce::String& key);

    /** A whole XML tree under `key` — for structured preferences such as a module's
        named setups. Returns nullptr when nothing is stored. */
    std::unique_ptr<juce::XmlElement> getXml (const juce::String& key) const;
    void setXml (const juce::String& key, const juce::XmlElement&);

    /** Told the key of every preference that changes, so each open panel can
        follow a change made elsewhere (the menu, another window, the plugin). */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void appPrefChanged (const juce::String& key) = 0;
    };
    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Write to disk now (it also saves itself shortly after every change). */
    void save();

    /** Where the file lives — for tests and for the curious. */
    juce::File file() const;

    ~AppPrefs() override;

    JUCE_DECLARE_SINGLETON_INLINE (AppPrefs, false)

private:
    AppPrefs();

    bool getBool (const char* key, bool fallback) const;
    void setBool (const char* key, bool);
    void changed (const juce::String& key);

    std::unique_ptr<juce::PropertiesFile> props;
    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE (AppPrefs)
};
