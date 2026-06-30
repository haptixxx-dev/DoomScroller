#include "engine/ShaderWatcher.h"

namespace ds {

std::vector<std::string> detectChanges(std::vector<WatchEntry>& state, const std::vector<WatchEntry>& current) {
    std::vector<std::string> changed;
    for (const auto& cur : current) {
        // Find the matching tracked entry by name.
        WatchEntry* prev = nullptr;
        for (auto& s : state) {
            if (s.name == cur.name) {
                prev = &s;
                break;
            }
        }

        if (prev == nullptr) {
            // New shader: report and start tracking it.
            changed.push_back(cur.name);
            state.push_back(cur);
            continue;
        }

        if (cur.lastModified > prev->lastModified)
            changed.push_back(cur.name);
        // Update stored mtime regardless so a second identical poll is quiet.
        prev->lastModified = cur.lastModified;
    }
    return changed;
}

} // namespace ds

#ifdef DS_DEV

#include "engine/BuildConfig.h"
#include "engine/Paths.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_process.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace ds {

namespace {

// Stages compiled per shader, with their slangc entry-point and the compiled
// output extension. Mirrors cmake/CompileSlang.cmake's stage table for the
// vertex/fragment stages (the only ones the runtime pipelines reissue).
struct StageInfo {
    const char* stage; // slangc -stage
    const char* entry; // slangc -entry
    const char* ext;   // output file extension
};

const char* targetExt() {
    // DS_SHADER_TARGET is the CMake -target string (spirv/metal/dxil).
    std::string t = DS_SHADER_TARGET;
    if (t == "metal")
        return "msl";
    if (t == "dxil")
        return "dxil";
    return "spv"; // spirv (and any unknown) -> .spv
}

// Converts std::filesystem::file_time_type to an int64 tick count. The clock
// epoch is implementation-defined but stable within a process, which is all
// detectChanges needs (it only compares magnitudes).
int64_t mtimeTicks(const std::filesystem::path& p, std::error_code& ec) {
    auto t = std::filesystem::last_write_time(p, ec);
    if (ec)
        return 0;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

} // namespace

ShaderWatcher::ShaderWatcher() {
    // Seed the state with the current mtimes so the first poll() does not
    // immediately recompile everything.
    std::error_code ec;
    std::filesystem::path dir = paths::shaderSources();
    if (!std::filesystem::is_directory(dir, ec))
        return;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file() || entry.path().extension() != ".slang")
            continue;
        std::error_code mec;
        m_state.push_back(WatchEntry{entry.path().stem().string(), mtimeTicks(entry.path(), mec)});
    }
}

bool ShaderWatcher::recompile(const std::string& name) {
    const std::array<StageInfo, 2> kStages = {{
        {"vertex", "vertMain", targetExt()},
        {"fragment", "fragMain", targetExt()},
    }};

    std::filesystem::path src    = paths::shaderSources() / (name + ".slang");
    std::filesystem::path outDir = paths::shaders();

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    for (const auto& s : kStages) {
        std::filesystem::path out = outDir / (name + "." + s.stage + "." + s.ext);

        std::string srcStr = src.string();
        std::string outStr = out.string();
        // argv mirrors the build-time command in cmake/CompileSlang.cmake:
        //   slangc <src> -entry <entry> -stage <stage> -target <target> -o <out>
        const char* argv[] = {DS_SLANG_COMPILER, srcStr.c_str(),   "-entry", s.entry,        "-stage", s.stage,
                              "-target",         DS_SHADER_TARGET, "-o",     outStr.c_str(), nullptr};

        SDL_Process* proc = SDL_CreateProcess(argv, false);
        if (proc == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[hot-reload] failed to launch slangc for %s.%s: %s",
                         name.c_str(), s.stage, SDL_GetError());
            return false;
        }

        int exitCode = 0;
        bool waited  = SDL_WaitProcess(proc, true, &exitCode);
        SDL_DestroyProcess(proc);

        if (!waited || exitCode != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[hot-reload] slangc failed (%d) for %s.%s", exitCode,
                         name.c_str(), s.stage);
            return false;
        }
    }

    return true;
}

std::vector<std::string> ShaderWatcher::poll() {
    // 1. Snapshot the current mtimes of every .slang source.
    std::error_code ec;
    std::filesystem::path dir = paths::shaderSources();
    if (!std::filesystem::is_directory(dir, ec))
        return {};

    std::vector<WatchEntry> current;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file() || entry.path().extension() != ".slang")
            continue;
        std::error_code mec;
        current.push_back(WatchEntry{entry.path().stem().string(), mtimeTicks(entry.path(), mec)});
    }

    // 2. Pure diff against the stored state (also advances the state).
    std::vector<std::string> changed = detectChanges(m_state, current);
    if (changed.empty())
        return {};

    // 3. Recompile each changed shader; only report the ones that succeeded so
    //    the Engine never reloads a stale/half-written artifact.
    std::vector<std::string> recompiled;
    for (const auto& name : changed) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[hot-reload] shader %s changed, recompiling", name.c_str());
        if (recompile(name))
            recompiled.push_back(name);
    }
    return recompiled;
}

} // namespace ds

#endif // DS_DEV
