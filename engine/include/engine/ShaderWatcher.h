#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ds {

// One watched shader source and the last modification time we observed for it.
// `name` is the shader base-name (e.g. "mesh"), matching the .slang stem and
// the name passed to ShaderLoader::load.
struct WatchEntry {
    std::string name;
    int64_t lastModified = 0;
};

// --- Pure, filesystem-free poll core (unit-tested). ------------------------
//
// Given the previously-seen state and a fresh snapshot of (name -> mtime),
// returns the base-names whose mtime increased (a real change) or that are new
// to the watch set, and updates `state` to match `current` so the next call
// only reports subsequent changes.
//
// Semantics:
//   - An entry in `current` not present in `state` is reported (newly added)
//     and inserted into `state`.
//   - An entry present in both is reported iff its mtime strictly increased;
//     `state` is updated to the new mtime either way.
//   - A purely-equal snapshot reports nothing.
//   - Entries dropped from `current` are left untouched in `state` (we never
//     "unwatch"; the watcher rescans a fixed source set each poll).
//
// This contains no I/O so it can be exercised headless in tests.
std::vector<std::string> detectChanges(std::vector<WatchEntry>& state, const std::vector<WatchEntry>& current);

#ifdef DS_DEV

// --- DS_DEV-only side-effecting watcher. -----------------------------------
//
// Scans paths::shaderSources()/*.slang, runs detectChanges, and for each
// changed shader invokes slangc (mirroring cmake/CompileSlang.cmake flags) to
// rewrite the compiled bytecode in paths::shaders(). Reports the changed
// base-names so the Engine can reissue the affected pipelines.
//
// Recompilation is best-effort: a missing slangc or a compile failure is
// logged and the shader is omitted from the returned list (so the Engine does
// not reload a stale/half-written artifact).
class ShaderWatcher {
  public:
    ShaderWatcher();

    // Rescans the source directory and recompiles any changed shaders.
    // Returns the base-names that were successfully recompiled this poll
    // (empty when nothing changed or all recompiles failed).
    std::vector<std::string> poll();

  private:
    // Recompiles <name>.slang to <out>/<name>.<stage>.<ext> for the vertex and
    // fragment stages. Returns true if every stage compiled.
    bool recompile(const std::string& name);

    std::vector<WatchEntry> m_state;
};

#else

// Shipping: the watcher compiles out to an empty no-op. poll() always reports
// "nothing changed" and the call site optimises away.
class ShaderWatcher {
  public:
    ShaderWatcher() = default;
    std::vector<std::string> poll() { return {}; }
};

#endif // DS_DEV

} // namespace ds
