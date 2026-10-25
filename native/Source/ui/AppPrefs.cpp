#include "AppPrefs.h"

namespace {
constexpr const char* kAutoApply       = AppPrefs::autoApplyKey;
constexpr const char* kTimeUnit        = AppPrefs::timeUnitKey;
constexpr const char* kSidePanels      = "sidePanelsShown";
constexpr const char* kMixerCollapsed  = "mixerCollapsed";
constexpr const char* kEffectsCollapsed = "effectsCollapsed";

juce::PropertiesFile::Options options()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Preferences";
    o.folderName          = "SoundEffects";
    o.filenameSuffix      = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;   // readable, diffable
    o.millisecondsBeforeSaving = 500;                           // coalesce slider-like bursts
    return o;
}
} // namespace

AppPrefs& AppPrefs::get()
{
    return *getInstance();
}

AppPrefs::AppPrefs()
    : props (std::make_unique<juce::PropertiesFile> (options()))
{
}

AppPrefs::~AppPrefs()
{
    save();
    clearSingletonInstance();
}

juce::File AppPrefs::file() const   { return props->getFile(); }
void AppPrefs::save()               { props->saveIfNeeded(); }

void AppPrefs::changed (const juce::String& key)
{
    listeners.call ([&key] (Listener& l) { l.appPrefChanged (key); });
}

bool AppPrefs::getBool (const char* key, bool fallback) const
{
    return props->getBoolValue (key, fallback);
}

void AppPrefs::setBool (const char* key, bool value)
{
    if (props->containsKey (key) && getBool (key, ! value) == value) return;
    props->setValue (key, value);
    changed (key);
}

bool AppPrefs::autoApply() const          { return getBool (kAutoApply, true); }
void AppPrefs::setAutoApply (bool v)      { setBool (kAutoApply, v); }

int AppPrefs::timeUnit() const            { return juce::jlimit (0, 2, props->getIntValue (kTimeUnit, 1)); }
void AppPrefs::setTimeUnit (int unit)
{
    unit = juce::jlimit (0, 2, unit);
    if (props->containsKey (kTimeUnit) && timeUnit() == unit) return;
    props->setValue (kTimeUnit, unit);
    changed (kTimeUnit);
}

bool AppPrefs::sidePanelsShown() const    { return getBool (kSidePanels, true); }
void AppPrefs::setSidePanelsShown (bool v){ setBool (kSidePanels, v); }
bool AppPrefs::mixerCollapsed() const     { return getBool (kMixerCollapsed, false); }
void AppPrefs::setMixerCollapsed (bool v) { setBool (kMixerCollapsed, v); }
bool AppPrefs::effectsCollapsed() const   { return getBool (kEffectsCollapsed, false); }
void AppPrefs::setEffectsCollapsed (bool v) { setBool (kEffectsCollapsed, v); }

std::unique_ptr<juce::XmlElement> AppPrefs::getXml (const juce::String& key) const
{
    return props->getXmlValue (key);
}

void AppPrefs::setXml (const juce::String& key, const juce::XmlElement& xml)
{
    props->setValue (key, &xml);
    changed (key);
}
