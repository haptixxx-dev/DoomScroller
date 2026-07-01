#pragma once

#include "engine/LevelLoader.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// DoomScroller text level format -> LevelData
// =============================================================================
//
// A tiny, line-oriented plain-text description of a level that the `level_convert`
// tool parses into a `LevelData` (see LevelLoader.h / LevelFormat.h) and writes
// out as a binary `.dslv`. The text form is human-authorable and diff-friendly;
// the binary form is what the runtime loads.
//
// GRAMMAR (one record per line; whitespace = any run of spaces/tabs)
// ------------------------------------------------------------------
//   # comment              -- a '#' anywhere starts a comment to end of line
//   <blank>                -- blank / whitespace-only lines are ignored
//
//   box   cx cy cz   hx hy hz   r g b
//       A static box (collision + render). 9 floats:
//         cx cy cz  = world-space center
//         hx hy hz  = half-extents along each axis
//         r  g  b   = vertex tint, 0..1
//       materialRef is always 0 (default material) from text.
//
//   spawn x y z   <kind>
//       A spawn point. 3 floats + a kind token:
//         x y z   = world position
//         <kind>  = "player" -> sets SpawnPointRecord.flags bit0 (player start)
//                   "enemy"  -> leaves flags 0
//
//   light x y z   r g b   radius intensity
//       A light. 8 floats:
//         x y z          = world position
//         r g b          = color, 0..1
//         radius         = falloff radius (world units)
//         intensity      = scalar brightness
//
// Tokens on a line are separated by any whitespace; column alignment in the
// examples above is purely cosmetic. The first token selects the record kind;
// an unrecognised kind, the wrong number of operands, or a non-numeric operand
// makes the parse fail (nullopt + an error message naming the offending line).
//
// PURITY
// ------
// This header is pure: it touches only `LevelData` / its POD records, the C++
// standard library, and throws nothing (std::strtof is used for float parsing,
// never exceptions). It links `engine_headers` only — no SDL3/Jolt/EnTT.
// =============================================================================

namespace ds {

namespace detail {

// Split `line` (already comment-stripped) into whitespace-delimited tokens.
inline std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> out;
    size_t i       = 0;
    const size_t n = line.size();
    while (i < n) {
        while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
            ++i;
        }
        if (i >= n) {
            break;
        }
        const size_t start = i;
        while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
            ++i;
        }
        out.push_back(line.substr(start, i - start));
    }
    return out;
}

// Parse one token as a float, no exceptions. Returns false if the token is not
// fully consumed as a number, or if it parses to a non-finite value ("inf"/"nan"
// are syntactically accepted but a level coordinate/extent is never legitimately
// infinite/NaN, so we reject those to honor the documented "non-numeric operand
// makes the parse fail" contract).
//
// NOTE: this deliberately uses std::strtof rather than the floating-point
// overload of std::from_chars. libc++ (Apple Clang / macOS) does not implement
// floating-point from_chars — it is declared `= delete`, so a from_chars(float)
// call fails to compile there ("call to deleted function"). strtof is available
// everywhere. It is locale-dependent, but the engine/tools run under the default
// "C" locale (no setlocale call), where '.' is the decimal separator, matching
// the text level format. We copy into a NUL-terminated buffer because strtof
// needs a C string and string_view tokens are not guaranteed NUL-terminated.
inline bool parseFloat(std::string_view tok, float& out) {
    if (tok.empty()) {
        return false;
    }
    std::string buf(tok);
    char* endPtr    = nullptr;
    errno           = 0;
    const float val = std::strtof(buf.c_str(), &endPtr);
    // Must consume the entire token (endPtr at the terminating NUL), not overflow
    // (ERANGE), and be finite. strtof returns 0 with endPtr==buf on a non-number.
    if (endPtr != buf.c_str() + buf.size() || errno == ERANGE || !std::isfinite(val)) {
        return false;
    }
    out = val;
    return true;
}

} // namespace detail

// Parse a text level description into a LevelData. On success returns the filled
// LevelData (header counts set from the parsed vectors). On any malformed line
// returns std::nullopt and, if `error` is non-null, writes a human-readable
// message (including the 1-based line number) into it. No exceptions escape.
inline std::optional<LevelData> parseLevelText(std::string_view text, std::string* error = nullptr) {
    auto fail = [&](size_t lineNo, std::string_view why) -> std::optional<LevelData> {
        if (error != nullptr) {
            *error = "line " + std::to_string(lineNo) + ": " + std::string(why);
        }
        return std::nullopt;
    };

    LevelData data;
    size_t lineNo      = 0;
    size_t pos         = 0;
    const size_t total = text.size();

    while (pos <= total) {
        ++lineNo;
        // Extract the next physical line [pos, eol).
        size_t eol = text.find('\n', pos);
        std::string_view rawLine;
        if (eol == std::string_view::npos) {
            rawLine = text.substr(pos);
            pos     = total + 1; // terminate after this iteration
        } else {
            rawLine = text.substr(pos, eol - pos);
            pos     = eol + 1;
        }

        // Strip a trailing comment: everything from the first '#'.
        if (const size_t hash = rawLine.find('#'); hash != std::string_view::npos) {
            rawLine = rawLine.substr(0, hash);
        }

        std::vector<std::string_view> tok = detail::tokenize(rawLine);
        if (tok.empty()) {
            continue; // blank or comment-only line
        }

        const std::string_view kind = tok[0];
        if (kind == "box") {
            if (tok.size() != 10) {
                return fail(lineNo, "box expects 9 numbers (cx cy cz hx hy hz r g b)");
            }
            BoxRecord b{};
            float vals[9] = {};
            for (size_t k = 0; k < 9; ++k) {
                if (!detail::parseFloat(tok[k + 1], vals[k])) {
                    return fail(lineNo, "box has a non-numeric value");
                }
            }
            b.center[0]      = vals[0];
            b.center[1]      = vals[1];
            b.center[2]      = vals[2];
            b.halfExtents[0] = vals[3];
            b.halfExtents[1] = vals[4];
            b.halfExtents[2] = vals[5];
            b.color[0]       = vals[6];
            b.color[1]       = vals[7];
            b.color[2]       = vals[8];
            b.materialRef    = 0;
            data.boxes.push_back(b);
        } else if (kind == "spawn") {
            if (tok.size() != 5) {
                return fail(lineNo, "spawn expects 3 numbers + a kind (x y z player|enemy)");
            }
            SpawnPointRecord s{};
            float vals[3] = {};
            for (size_t k = 0; k < 3; ++k) {
                if (!detail::parseFloat(tok[k + 1], vals[k])) {
                    return fail(lineNo, "spawn has a non-numeric position");
                }
            }
            s.position[0]              = vals[0];
            s.position[1]              = vals[1];
            s.position[2]              = vals[2];
            const std::string_view who = tok[4];
            if (who == "player") {
                s.flags = 1u; // bit0 = player start
            } else if (who == "enemy") {
                s.flags = 0u;
            } else {
                return fail(lineNo, "spawn kind must be 'player' or 'enemy'");
            }
            data.spawns.push_back(s);
        } else if (kind == "light") {
            if (tok.size() != 9) {
                return fail(lineNo, "light expects 8 numbers (x y z r g b radius intensity)");
            }
            LightRecord l{};
            float vals[8] = {};
            for (size_t k = 0; k < 8; ++k) {
                if (!detail::parseFloat(tok[k + 1], vals[k])) {
                    return fail(lineNo, "light has a non-numeric value");
                }
            }
            l.position[0] = vals[0];
            l.position[1] = vals[1];
            l.position[2] = vals[2];
            l.color[0]    = vals[3];
            l.color[1]    = vals[4];
            l.color[2]    = vals[5];
            l.radius      = vals[6];
            l.intensity   = vals[7];
            data.lights.push_back(l);
        } else {
            return fail(lineNo, "unknown record kind (expected box|spawn|light)");
        }
    }

    data.header.boxCount   = static_cast<uint32_t>(data.boxes.size());
    data.header.spawnCount = static_cast<uint32_t>(data.spawns.size());
    data.header.lightCount = static_cast<uint32_t>(data.lights.size());
    return data;
}

} // namespace ds
