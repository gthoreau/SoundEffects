// Settings — a module's parameter block.
//
// Ported in spirit from the TS reference, where a module's `ModSettingsRec` is a
// plain object the host persists by signature without ever looking inside.
//
// This is a FLAT, string-keyed map with dotted paths for nested groups
// ("rate.initialValue" — Sample Hose has five such groups of five fields), and
// that is a deliberate choice over one struct per module. The whole point of the
// original's ModSetups.h ABI (§7.5) is that a module hands the host a schema and
// the HOST runs the dialog; 37 bespoke structs would mean 37 bespoke dialogs and
// throw that away. One map means one schema-driven panel renders every module.
//
// Values are typed on write and coerced on read: the IIR family's `a = 1` is
// stored as an integer but is conceptually a double, and the module must be able
// to ask for it as one without caring how it arrived.

#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace se {

struct Value
{
    enum class Kind : uint8_t { Int = 0, Real = 1, Str = 2 };

    Kind        kind = Kind::Int;
    int64_t     i = 0;
    double      d = 0.0;
    std::string s;

    Value() = default;
    Value (int64_t v) : kind (Kind::Int), i (v) {}
    Value (int v)     : kind (Kind::Int), i (v) {}
    Value (bool v)    : kind (Kind::Int), i (v ? 1 : 0) {}
    Value (double v)  : kind (Kind::Real), d (v) {}
    Value (std::string v) : kind (Kind::Str), s (std::move (v)) {}

    int64_t asInt()    const noexcept { return kind == Kind::Real ? static_cast<int64_t> (d) : i; }
    double  asDouble() const noexcept { return kind == Kind::Real ? d : static_cast<double> (i); }
    bool    asBool()   const noexcept { return asInt() != 0; }
    const std::string& asString() const noexcept { return s; }
};

using Settings = std::map<std::string, Value>;

/** Load-or-default reads, mirroring the original's GetModSettings: a key that was
    never set falls back rather than failing, because "fields start empty on first
    run and thereafter remember last-used values" (§8). */
inline int64_t getInt (const Settings& s, const std::string& key, int64_t fallback = 0)
{
    const auto it = s.find (key);
    return it == s.end() ? fallback : it->second.asInt();
}

inline double getDouble (const Settings& s, const std::string& key, double fallback = 0.0)
{
    const auto it = s.find (key);
    return it == s.end() ? fallback : it->second.asDouble();
}

inline bool getBool (const Settings& s, const std::string& key, bool fallback = false)
{
    const auto it = s.find (key);
    return it == s.end() ? fallback : it->second.asBool();
}

} // namespace se
