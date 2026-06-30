#pragma once

#include "engine/SaveData.h"
#include "engine/SettingsStore.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// DoomScroller user-storage file I/O
// =============================================================================
//
// Thin filesystem glue around the pure SettingsStore / SaveData serialize+parse
// cores. It owns no global state and pulls in no SDL / Jolt / EnTT: just
// <filesystem> + <fstream> + the two pure headers. That keeps it linkable into
// engine_math (and thus the headless test binary) without dragging in the
// engine's heavy runtime dependencies.
//
// Layout under a user directory (typically paths::userDir()):
//   <dir>/settings.cfg   text   serializeSettings / parseSettings
//   <dir>/save.dat       binary serializeSave     / parseSave
//
// Every reader is total: a missing or unreadable file yields std::nullopt and a
// parse failure (bad magic, version, CRC, truncation) likewise yields nullopt,
// never an exception. Writers create the parent directory tree on demand and
// return false on any I/O failure.
// =============================================================================

namespace ds {

// Canonical file names, joined onto the caller-supplied user directory.
inline constexpr std::string_view kSettingsFileName = "settings.cfg";
inline constexpr std::string_view kSaveFileName     = "save.dat";

// Write `bytes` to `path` as raw binary, creating any missing parent
// directories first. Returns false if the directories cannot be created or the
// file cannot be opened/written.
inline bool writeFile(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(out);
}

// Read the entire file at `path` as raw bytes. Returns std::nullopt if the file
// does not exist or cannot be opened/read.
inline std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& path) {
    // Only read regular files: opening a directory/device can succeed on some
    // platforms while tellg() returns a bogus huge size, which would make the
    // size-based allocation below throw — breaking the documented no-throw
    // totality guarantee. Guarding here keeps the contract platform-independent.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }

    const std::streamoff size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!in) {
            return std::nullopt;
        }
    }
    return bytes;
}

// Write `text` to `path` as a text file, creating any missing parent
// directories first. Returns false on any I/O failure.
inline bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    if (!text.empty()) {
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    return static_cast<bool>(out);
}

// Read the entire file at `path` as text. Returns std::nullopt if the file does
// not exist or cannot be opened/read.
inline std::optional<std::string> readTextFile(const std::filesystem::path& path) {
    // See readFile: reject non-regular paths so the size-based allocation cannot
    // throw on a directory/device, honoring the no-throw totality contract.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }

    const std::streamoff size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);

    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        in.read(text.data(), static_cast<std::streamsize>(size));
        if (!in) {
            return std::nullopt;
        }
    }
    return text;
}

// Serialize `settings` and write them to <dir>/settings.cfg. Returns false on
// any I/O failure.
inline bool saveSettings(const std::filesystem::path& dir, const GameSettings& settings) {
    return writeTextFile(dir / kSettingsFileName, serializeSettings(settings));
}

// Read and parse <dir>/settings.cfg. Returns std::nullopt if the file is
// missing/unreadable or the blob fails to parse.
inline std::optional<GameSettings> loadSettings(const std::filesystem::path& dir) {
    const std::optional<std::string> text = readTextFile(dir / kSettingsFileName);
    if (!text) {
        return std::nullopt;
    }
    return parseSettings(*text);
}

// Serialize `save` and write it to <dir>/save.dat. Returns false on any I/O
// failure.
inline bool saveGame(const std::filesystem::path& dir, const SaveData& save) {
    return writeFile(dir / kSaveFileName, serializeSave(save));
}

// Read and parse <dir>/save.dat. Returns std::nullopt if the file is
// missing/unreadable or the blob fails its magic/version/CRC checks.
inline std::optional<SaveData> loadGame(const std::filesystem::path& dir) {
    const std::optional<std::vector<uint8_t>> bytes = readFile(dir / kSaveFileName);
    if (!bytes) {
        return std::nullopt;
    }
    return parseSave(*bytes);
}

} // namespace ds
