#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ds {

// =============================================================================
// Rebindable input map (pure action <-> code resolution + text serialize)
// =============================================================================
//
// A small, engine-independent layer that maps abstract gameplay Actions to
// opaque hardware codes. The header is deliberately SDL-free: codes are stored
// as plain uint32_t so unit tests can link against engine_headers only. The
// Engine is responsible for translating raw SDL scancodes / mouse buttons into
// these uint32 codes (and back) when it consults the map.
//
// The default codes below are arbitrary nonzero placeholders chosen so each
// action is distinct; they are NOT real SDL scancodes. Treat them as stable
// internal identifiers that the Engine's binding layer maps onto.
// =============================================================================

// Abstract gameplay actions. `Count` is a sentinel for array sizing and is not
// itself a bindable action.
enum class Action : uint8_t {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Jump,
    Dash,
    Slide,
    Fire,
    AltFire,
    Parry,
    Weapon1,
    Weapon2,
    Weapon3,
    Count,
};

// Number of bindable actions (excludes the `Count` sentinel).
inline constexpr int kActionCount = static_cast<int>(Action::Count);

// Reserved code meaning "no binding". A code of 0 never matches an action.
inline constexpr uint32_t kUnbound = 0;

// Serialized-format version. Bumped whenever the text layout changes.
inline constexpr uint32_t kInputVersion = 1;

// Stable string id for an action, used by the text serializer (e.g.
// "move_forward"). Returns "unknown" for an out-of-range / `Count` value.
inline const char* actionName(Action action) {
    switch (action) {
    case Action::MoveForward:
        return "move_forward";
    case Action::MoveBack:
        return "move_back";
    case Action::MoveLeft:
        return "move_left";
    case Action::MoveRight:
        return "move_right";
    case Action::Jump:
        return "jump";
    case Action::Dash:
        return "dash";
    case Action::Slide:
        return "slide";
    case Action::Fire:
        return "fire";
    case Action::AltFire:
        return "alt_fire";
    case Action::Parry:
        return "parry";
    case Action::Weapon1:
        return "weapon1";
    case Action::Weapon2:
        return "weapon2";
    case Action::Weapon3:
        return "weapon3";
    case Action::Count:
        break;
    }
    return "unknown";
}

// Inverse of actionName: resolves a stable string id to its Action, or nullopt
// for an unrecognised name.
inline std::optional<Action> actionFromName(std::string_view name) {
    for (int i = 0; i < kActionCount; ++i) {
        auto action = static_cast<Action>(i);
        if (name == actionName(action))
            return action;
    }
    return std::nullopt;
}

// Dense binding table: one hardware code per action, indexed by the action's
// underlying integer value. A code of `kUnbound` (0) means the action has no
// binding.
struct InputMap {
    uint32_t codes[kActionCount] = {};
};

// Default placeholder codes, one distinct nonzero value per action. These are
// internal identifiers (NOT real SDL scancodes); the Engine maps real input
// onto them. Defined contiguously starting at 1 so every action is bound and
// distinct in a fresh map.
inline constexpr std::array<uint32_t, kActionCount> kDefaultCodes = {
    101u, // MoveForward
    102u, // MoveBack
    103u, // MoveLeft
    104u, // MoveRight
    105u, // Jump
    106u, // Dash
    107u, // Slide
    108u, // Fire
    109u, // AltFire
    110u, // Parry
    111u, // Weapon1
    112u, // Weapon2
    113u, // Weapon3
};

// Construct a map with every action bound to its default placeholder code.
inline InputMap defaultInputMap() {
    InputMap map{};
    for (int i = 0; i < kActionCount; ++i)
        map.codes[i] = kDefaultCodes[static_cast<std::size_t>(i)];
    return map;
}

// Bind `action` to `code` (use `kUnbound` to clear). Out-of-range actions are
// ignored.
inline void setBinding(InputMap& map, Action action, uint32_t code) {
    auto idx = static_cast<int>(action);
    if (idx < 0 || idx >= kActionCount)
        return;
    map.codes[idx] = code;
}

// Return the code currently bound to `action`, or `kUnbound` if unbound /
// out-of-range.
inline uint32_t bindingFor(const InputMap& map, Action action) {
    auto idx = static_cast<int>(action);
    if (idx < 0 || idx >= kActionCount)
        return kUnbound;
    return map.codes[idx];
}

// Reverse lookup: the first action bound to `code`, or nullopt. `kUnbound` (0)
// never matches even if some slot happens to be unbound.
inline std::optional<Action> actionForCode(const InputMap& map, uint32_t code) {
    if (code == kUnbound)
        return std::nullopt;
    for (int i = 0; i < kActionCount; ++i) {
        if (map.codes[i] == code)
            return static_cast<Action>(i);
    }
    return std::nullopt;
}

// Serialize a map to text. Format: a "DSINPUT <version>" header line followed
// by one "action_name code" line per bindable action. Lines are '\n' separated
// and the output ends with a trailing newline.
inline std::string serializeInputMap(const InputMap& map) {
    std::string out = "DSINPUT ";
    out += std::to_string(kInputVersion);
    out += '\n';
    for (int i = 0; i < kActionCount; ++i) {
        out += actionName(static_cast<Action>(i));
        out += ' ';
        out += std::to_string(map.codes[i]);
        out += '\n';
    }
    return out;
}

// Parse text produced by serializeInputMap. The header version must equal
// kInputVersion or nullopt is returned. Lines for unknown action names are
// ignored; actions absent from the text keep their default code. Malformed
// code values cause the whole parse to fail (nullopt).
inline std::optional<InputMap> parseInputMap(std::string_view text) {
    InputMap map   = defaultInputMap();
    bool sawHeader = false;

    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl        = text.find('\n', pos);
        std::size_t end       = (nl == std::string_view::npos) ? text.size() : nl;
        std::string_view line = text.substr(pos, end - pos);
        pos                   = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;

        // Trim a trailing '\r' so CRLF text parses too.
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty())
            continue;

        if (!sawHeader) {
            // First non-empty line must be the versioned header.
            constexpr std::string_view kHeaderTag = "DSINPUT ";
            if (line.substr(0, kHeaderTag.size()) != kHeaderTag)
                return std::nullopt;
            std::string_view verText = line.substr(kHeaderTag.size());
            uint32_t version         = 0;
            auto* begin              = verText.data();
            auto* finish             = verText.data() + verText.size();
            auto [ptr, ec]           = std::from_chars(begin, finish, version);
            if (ec != std::errc{} || ptr != finish)
                return std::nullopt;
            if (version != kInputVersion)
                return std::nullopt;
            sawHeader = true;
            continue;
        }

        // Body line: "action_name code".
        std::size_t sp = line.find(' ');
        if (sp == std::string_view::npos)
            return std::nullopt;
        std::string_view nameText = line.substr(0, sp);
        std::string_view codeText = line.substr(sp + 1);

        uint32_t code  = 0;
        auto* begin    = codeText.data();
        auto* finish   = codeText.data() + codeText.size();
        auto [ptr, ec] = std::from_chars(begin, finish, code);
        if (ec != std::errc{} || ptr != finish)
            return std::nullopt;

        if (auto action = actionFromName(nameText))
            setBinding(map, *action, code);
        // Unknown action names are silently ignored.
    }

    if (!sawHeader)
        return std::nullopt;
    return map;
}

} // namespace ds
