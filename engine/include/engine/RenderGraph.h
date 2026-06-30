#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rhi/IRHICommandList.h"
#include "rhi/RHITypes.h"

namespace ds {

// ---------------------------------------------------------------------------
// RenderGraph — a lightweight declarative frame-pass scheduler (PLAN task 23).
//
// A frame is described as an ordered list of RenderPass entries. Each pass owns
// its color/depth attachments and a `record` callback that emits the draw calls
// for that pass. execute() walks the list and, for every pass, issues exactly:
//
//     beginRenderPass(desc); record(cmd); endRenderPass();
//
// This centralises the begin/record/end boundary so individual systems no
// longer hand-roll it, while keeping the emitted RHI call sequence identical to
// the imperative code it replaces.
//
// Dependency-light by design: only pulls in the rhi interface headers plus a
// few std containers. No SDL3 / Jolt / GPU-device dependency, so it compiles
// against the `engine_headers` target and can be unit-tested with a mock
// command list (no GPU required).
//
// NOTE (future extension point): a pass declares its outputs via its
// attachments and (conceptually) its inputs via the textures its `record`
// lambda samples. Today every render target is allocated up-front by the
// engine and referenced directly. A future revision can add transient
// render-target allocation / aliasing here — the graph would own a pool and
// hand passes their attachments — without changing the addPass/execute API.
// That allocator is intentionally NOT built yet; the value of this task is the
// declarative pass list plus the centralised begin/record/end.
// ---------------------------------------------------------------------------

struct RenderPass {
    // Human-readable name (debug / profiling / tests). Owned copy so callers
    // can pass string literals or temporaries freely.
    std::string name;

    // Output attachments for this pass. An invalid ColorAttachment texture
    // handle binds the swapchain backbuffer (matches RHI semantics). An empty
    // colorAttachments list + a set depthAttachment is a depth-only pass.
    std::vector<rhi::ColorAttachment> colorAttachments;
    std::optional<rhi::DepthAttachment> depthAttachment;

    // Records the draw calls for this pass. Invoked between beginRenderPass and
    // endRenderPass. Must NOT itself open/close a render pass.
    std::function<void(rhi::IRHICommandList&)> record;
};

class RenderGraph {
  public:
    // Declares a pass. Passes execute in the order they are added.
    void addPass(RenderPass pass) { m_passes.push_back(std::move(pass)); }

    // Walks the declared passes in order. For each: builds a RenderPassDesc
    // from the owned attachments, beginRenderPass(desc), record(cmd),
    // endRenderPass(). The attachment storage lives in the RenderPass for the
    // duration of the call so the RenderPassDesc spans/pointer stay valid.
    void execute(rhi::IRHICommandList& cmd) {
        for (auto& pass : m_passes) {
            rhi::RenderPassDesc desc{};
            if (!pass.colorAttachments.empty())
                desc.colorAttachments = {pass.colorAttachments.data(), pass.colorAttachments.size()};
            desc.depthAttachment = pass.depthAttachment ? &*pass.depthAttachment : nullptr;

            cmd.beginRenderPass(desc);
            if (pass.record)
                pass.record(cmd);
            cmd.endRenderPass();
        }
    }

    // Resets the pass list for the next frame.
    void clear() { m_passes.clear(); }

    // Declared pass count (mostly for tests / introspection).
    size_t passCount() const { return m_passes.size(); }

  private:
    std::vector<RenderPass> m_passes;
};

} // namespace ds
