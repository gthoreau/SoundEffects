#include "Registry.h"

#include "modules/Modules.h"

#include <algorithm>

namespace se {

const std::vector<EffectModule>& effectRegistry()
{
    static const std::vector<EffectModule> registry = []
    {
        std::vector<EffectModule> list;

        // Wave A — pointwise and simple.
        list.push_back (modules::makeAmplify());
        list.push_back (modules::makeInvert());
        list.push_back (modules::makeReverse());
        list.push_back (modules::makeSilence());
        list.push_back (modules::makeDrawLine());
        list.push_back (modules::makeFadeIn());
        list.push_back (modules::makeFadeOut());
        list.push_back (modules::makeNormalize());
        list.push_back (modules::makeSmooth());
        list.push_back (modules::makeDither());
        list.push_back (modules::makeNoise());
        list.push_back (modules::makeRobotize());
        list.push_back (modules::makeLowPassFIR());
        list.push_back (modules::makeHighPassFIR());
        list.push_back (modules::makeBandpassFIR());
        list.push_back (modules::makeBandstopFIR());
        list.push_back (modules::makeHighPassIIR());
        list.push_back (modules::makeLowPassIIR());
        list.push_back (modules::makeShelving());

        return list;
    }();

    return registry;
}

const EffectModule* findEffect (const std::string& name)
{
    for (const auto& m : effectRegistry())
        if (m.name == name) return &m;
    return nullptr;
}

const EffectModule* findEffectBySignature (const std::string& signature)
{
    for (const auto& m : effectRegistry())
        if (m.signature == signature) return &m;
    return nullptr;
}

EffectMenu buildEffectMenu()
{
    // The recovered submenu order. Modules with no group sit at top level.
    static const EffectGroup kGroupOrder[] = {
        EffectGroup::EchoesAndReverbs,
        EffectGroup::FirIirFilters,
        EffectGroup::Resampling,
        EffectGroup::Misc,
        EffectGroup::MikesModules,
        EffectGroup::Selection,
    };

    const auto byName = [] (const EffectModule* a, const EffectModule* b)
    {
        return a->name < b->name;
    };

    EffectMenu menu;

    for (const auto& m : effectRegistry())
        if (m.group == EffectGroup::None)
            menu.topLevel.push_back (&m);

    std::sort (menu.topLevel.begin(), menu.topLevel.end(), byName);

    for (EffectGroup g : kGroupOrder)
    {
        EffectMenu::Group group;
        group.id = g;
        group.name = groupName (g);

        for (const auto& m : effectRegistry())
            if (m.group == g) group.modules.push_back (&m);

        // An empty group is a group that has not been ported yet — don't draw a
        // heading over nothing.
        if (group.modules.empty()) continue;

        std::sort (group.modules.begin(), group.modules.end(), byName);
        menu.groups.push_back (std::move (group));
    }

    return menu;
}

} // namespace se
