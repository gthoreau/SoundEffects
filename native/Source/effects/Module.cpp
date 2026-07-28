#include "Module.h"

namespace se {

const char* groupName (EffectGroup g) noexcept
{
    switch (g)
    {
        case EffectGroup::EchoesAndReverbs: return "Echoes & Reverbs";
        case EffectGroup::FirIirFilters:    return "FIR/IIR Filters";
        case EffectGroup::Resampling:       return "Resampling";
        case EffectGroup::Misc:             return "Misc";
        case EffectGroup::MikesModules:     return "Mike's Modules";
        case EffectGroup::Selection:        return "Selection";
        case EffectGroup::None:             break;
    }
    return "";
}

Settings getModSettings (const EffectModule& module, const Host& host)
{
    if (const Settings* stored = host.loadEffectSettings (module.signature))
        return *stored;
    return module.defaults ? module.defaults() : Settings{};
}

ModResult runEffect (const EffectModule& module, SoundDocument& doc, Host& host,
                     const Settings& prefs)
{
    // Defaults first, then whatever the caller supplied on top — a module must
    // never see a half-populated settings block, since the original's fields
    // "start empty on first run and thereafter remember last-used values".
    Settings merged = module.defaults ? module.defaults() : Settings{};
    for (const auto& [key, value] : prefs)
        merged[key] = value;

    if (module.validate)
        merged = module.validate (std::move (merged));

    return module.effect (doc, host, merged);
}

} // namespace se
