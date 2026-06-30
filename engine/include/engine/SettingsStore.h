#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace ds {

// Version stamped into the serialized settings blob. Bump when the on-disk
// layout changes; parseSettings rejects any blob whose version differs so old
// or future files never silently load with mismatched semantics.
inline constexpr uint32_t kSettingsVersion = 1;

// User-facing game settings. Defaults are the in-memory fallback: a missing
// key in a parsed blob keeps the corresponding default, so adding a new field
// is backward-compatible with older saved files (subject to the version stamp).
struct GameSettings {
    float masterVolume    = 1.f;
    float sfxVolume       = 1.f;
    float musicVolume     = 0.8f;
    float uiVolume        = 0.9f;
    float lookSensitivity = 0.1f;
    int windowWidth       = 1280;
    int windowHeight      = 720;
    bool fullscreen       = false;
    bool vsync            = true;
};

// Serialize settings to a deterministic line-based text blob. The first line is
// the magic + version header ("DSSETTINGS 1"); each subsequent line is a
// "key value" pair in a fixed order. Floats are written with %.9g (enough
// significant digits to exactly round-trip an IEEE-754 float) so a round-trip
// through parseSettings reproduces the same value.
inline std::string serializeSettings(const GameSettings& s) {
    auto floatLine = [](const char* key, float value) -> std::string {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(value));
        std::string line = key;
        line += ' ';
        line += buf;
        line += '\n';
        return line;
    };

    std::string out = "DSSETTINGS ";
    out += std::to_string(kSettingsVersion);
    out += '\n';

    out += floatLine("masterVolume", s.masterVolume);
    out += floatLine("sfxVolume", s.sfxVolume);
    out += floatLine("musicVolume", s.musicVolume);
    out += floatLine("uiVolume", s.uiVolume);
    out += floatLine("lookSensitivity", s.lookSensitivity);
    out += "windowWidth " + std::to_string(s.windowWidth) + '\n';
    out += "windowHeight " + std::to_string(s.windowHeight) + '\n';
    out += std::string("fullscreen ") + (s.fullscreen ? "1" : "0") + '\n';
    out += std::string("vsync ") + (s.vsync ? "1" : "0") + '\n';

    return out;
}

namespace detail {

// Parse a float without letting std::stof throw. Returns false on any malformed
// or out-of-range input, leaving `out` untouched.
inline bool parseFloat(const std::string& token, float& out) {
    try {
        std::size_t consumed = 0;
        float value          = std::stof(token, &consumed);
        if (consumed != token.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

// Parse an int without letting std::stoi throw. Returns false on any malformed
// or out-of-range input, leaving `out` untouched.
inline bool parseInt(const std::string& token, int& out) {
    try {
        std::size_t consumed = 0;
        int value            = std::stoi(token, &consumed);
        if (consumed != token.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

// Parse a bool as "0"/"1" or "true"/"false". Returns false on anything else,
// leaving `out` untouched.
inline bool parseBool(const std::string& token, bool& out) {
    if (token == "1" || token == "true") {
        out = true;
        return true;
    }
    if (token == "0" || token == "false") {
        out = false;
        return true;
    }
    return false;
}

} // namespace detail

// Parse a settings blob produced by serializeSettings. Returns nullopt if the
// magic header is missing or the version differs from kSettingsVersion.
// Otherwise it is tolerant: unknown keys are ignored, missing keys keep the
// struct default, and lines with a malformed value for a known key are skipped
// (that field keeps its default). Never throws.
inline std::optional<GameSettings> parseSettings(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::string line;

    // First non-empty line must be the magic + matching version.
    bool headerSeen = false;
    GameSettings result;

    while (std::getline(stream, line)) {
        // Strip a trailing carriage return (CRLF tolerance).
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::istringstream lineStream{line};
        std::string key;
        if (!(lineStream >> key)) {
            continue; // blank / whitespace-only line
        }

        if (!headerSeen) {
            if (key != "DSSETTINGS") {
                return std::nullopt; // missing magic
            }
            int version = 0;
            std::string versionToken;
            if (!(lineStream >> versionToken)) {
                return std::nullopt;
            }
            if (!detail::parseInt(versionToken, version) || version != static_cast<int>(kSettingsVersion)) {
                return std::nullopt; // version mismatch / unparseable
            }
            headerSeen = true;
            continue;
        }

        std::string value;
        if (!(lineStream >> value)) {
            continue; // key with no value: skip
        }

        if (key == "masterVolume") {
            detail::parseFloat(value, result.masterVolume);
        } else if (key == "sfxVolume") {
            detail::parseFloat(value, result.sfxVolume);
        } else if (key == "musicVolume") {
            detail::parseFloat(value, result.musicVolume);
        } else if (key == "uiVolume") {
            detail::parseFloat(value, result.uiVolume);
        } else if (key == "lookSensitivity") {
            detail::parseFloat(value, result.lookSensitivity);
        } else if (key == "windowWidth") {
            detail::parseInt(value, result.windowWidth);
        } else if (key == "windowHeight") {
            detail::parseInt(value, result.windowHeight);
        } else if (key == "fullscreen") {
            detail::parseBool(value, result.fullscreen);
        } else if (key == "vsync") {
            detail::parseBool(value, result.vsync);
        }
        // Unknown keys: ignored.
    }

    if (!headerSeen) {
        return std::nullopt; // empty / no magic at all
    }

    return result;
}

} // namespace ds
