#!/usr/bin/env python3
"""Doxygen INPUT_FILTER for assets/scripts/*.lua.

Doxygen has no native Lua parser -- it only recognizes // and /* */ style
comments, not Lua's --/--[[ ]]. This script rewrites this project's "---"
doc-comment blocks (immediately above a `function <name>(...)` definition)
into synthetic C++ declarations Doxygen's C++ parser CAN read, so the actual
ds.* Lua API surface defined in the .lua files (not just its C++ binding
side) shows up in the generated documentation.

Convention this filter expects (see assets/scripts/*.lua for examples):
    --- Brief description.
    -- More description.
    -- @param name what it is
    -- @return what comes back
    function ds.module.name(name)
        ...
    end

Only comment blocks whose FIRST line starts with "---" are treated as API
docs; plain "--" comments (ordinary in-code notes) are ignored, so internal
implementation commentary doesn't get mistaken for a documented function.

This output is fed to Doxygen only -- it is never written back to the real
.lua file (FILTER_PATTERNS only affects what Doxygen's parser sees), so it
is free to discard anything that isn't part of extracting a documented
function's signature + doc comment.
"""
import re
import sys

# Matches the START of a (possibly multi-line) function header:
# "function ds.module.name(" or "function onWaveStart(" etc.
FUNC_START_RE = re.compile(r"^function\s+([A-Za-z_][A-Za-z0-9_.]*)\s*\(")


def module_group(dotted_name: str) -> str:
    """"ds.parry.reflect_velocity" -> "ds_parry"; "onWaveStart" -> "ds_hooks"."""
    parts = dotted_name.split(".")
    if len(parts) >= 3:
        return "_".join(parts[:-1])
    if len(parts) == 2:
        return parts[0]
    return "ds_hooks"  # bare global hook functions (hooks.lua)


def param_names(raw_params: str):
    names = []
    for p in raw_params.split(","):
        p = p.strip()
        if p:
            names.append(p)
    return names


def emit_file_doc(out, doc_lines):
    if not doc_lines:
        return
    out.append("/**")
    for line in doc_lines:
        out.append(f" * {line}")
    out.append(" */")
    out.append("")


def emit_function(out, dotted_name, params, doc_lines):
    group = module_group(dotted_name)
    flat_name = dotted_name.replace(".", "_")
    documented_params = {
        re.match(r"@param\s+(\S+)", line.strip()).group(1)
        for line in doc_lines
        if re.match(r"@param\s+(\S+)", line.strip())
    }
    out.append(f"/// @ingroup {group}")
    out.append("/**")
    for line in doc_lines:
        out.append(f" * {line}")
    for p in params:
        if p not in documented_params:
            out.append(f" * @param {p}")
    out.append(" */")
    sig = ", ".join(f"LuaValue {p}" for p in params)
    out.append(f"LuaValue {flat_name}({sig});")
    out.append("")


def strip_comment_marker(line: str) -> str:
    # Strip a leading run of '-' (one or more) and at most one following space.
    return re.sub(r"^-+\s?", "", line)


def main():
    path = sys.argv[1]
    with open(path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    out = [
        "/// Synthetic stub type so Doxygen's C++ parser accepts the",
        "/// fabricated declarations below -- not a real engine type.",
        "typedef void* LuaValue;",
        "",
    ]

    pending_doc = []
    pending_is_api_doc = False
    file_doc_emitted = False
    i = 0
    n = len(lines)
    while i < n:
        raw = lines[i]
        stripped = raw.strip()

        if FUNC_START_RE.match(stripped):
            # Accumulate lines until the parens balance (multi-line headers,
            # e.g. enemy_ai.lua's ds.enemy_ai.tick).
            header = stripped
            while header.count("(") > header.count(")") and i + 1 < n:
                i += 1
                header += " " + lines[i].strip()
            m = re.match(r"^function\s+([A-Za-z_][A-Za-z0-9_.]*)\s*\(([\s\S]*?)\)", header)
            if m and pending_is_api_doc:
                emit_function(out, m.group(1), param_names(m.group(2)), pending_doc)
            elif not file_doc_emitted and pending_is_api_doc:
                # A "--- @file" block not directly above a function (the
                # common case: it's the first thing in the file) documents
                # the module as a whole.
                emit_file_doc(out, pending_doc)
                file_doc_emitted = True
            pending_doc = []
            pending_is_api_doc = False
            i += 1
            continue

        if stripped.startswith("--"):
            if not pending_doc:
                pending_is_api_doc = stripped.startswith("---")
            pending_doc.append(strip_comment_marker(stripped))
            i += 1
            continue

        # Blank line or real code breaks adjacency between a pending doc
        # block and whatever follows (this project's convention never puts a
        # blank line between a function's own doc block and the function
        # itself -- only the file-level "@file" header is followed by a
        # blank line before the first real declaration, so flush it here).
        if pending_is_api_doc and not file_doc_emitted and any(d.strip().startswith("@file") for d in pending_doc):
            emit_file_doc(out, [d for d in pending_doc if not d.strip().startswith("@file")])
            file_doc_emitted = True
        pending_doc = []
        pending_is_api_doc = False
        i += 1

    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
