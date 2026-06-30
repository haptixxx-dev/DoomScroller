#include "engine/RenderGraph.h"
#include "engine/rhi/IRHICommandList.h"
#include "engine/rhi/RHITypes.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

// Minimal no-op IRHICommandList that records the order of beginRenderPass /
// endRenderPass calls (and lets record lambdas push their own markers). No GPU
// required: every override is a stub, and only the calls we assert on append to
// `log`. Lives in the test TU and links engine_headers only — the rhi
// interface headers are free of SDL3/Jolt.
class MockCommandList : public ds::rhi::IRHICommandList {
  public:
    std::vector<std::string> log;

    // The two calls under test.
    void beginRenderPass(const ds::rhi::RenderPassDesc& /*desc*/) override { log.emplace_back("begin"); }
    void endRenderPass() override { log.emplace_back("end"); }

    // Everything else is an inert stub.
    void setPipeline(ds::rhi::RHIPipeline) override {}
    void setViewport(float, float, float, float, float, float) override {}
    void setScissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void setVertexBuffer(uint32_t, ds::rhi::RHIBuffer, uint64_t) override {}
    void setIndexBuffer(ds::rhi::RHIBuffer, ds::rhi::IndexType, uint64_t) override {}
    void pushVertexConstants(const void*, uint32_t, uint32_t) override {}
    void pushFragmentConstants(const void*, uint32_t, uint32_t) override {}
    void bindVertexTexture(uint32_t, ds::rhi::RHITexture, ds::rhi::RHISampler) override {}
    void bindFragmentTexture(uint32_t, ds::rhi::RHITexture, ds::rhi::RHISampler) override {}
    void bindVertexUniform(uint32_t, ds::rhi::RHIBuffer, uint64_t, uint64_t) override {}
    void bindFragmentUniform(uint32_t, ds::rhi::RHIBuffer, uint64_t, uint64_t) override {}
    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void drawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
    void uploadBuffer(ds::rhi::RHIBuffer, const void*, uint64_t, uint64_t) override {}
    void uploadTexture(ds::rhi::RHITexture, const void*, uint64_t, uint32_t, uint32_t) override {}
    void copyBuffer(ds::rhi::RHIBuffer, uint64_t, ds::rhi::RHIBuffer, uint64_t, uint64_t) override {}
};

} // namespace

TEST_CASE("RenderGraph executes passes in begin/record/end order", "[rendergraph]") {
    ds::RenderGraph graph;
    MockCommandList cmd;

    auto makePass = [&](const char* name) {
        ds::RenderPass pass;
        pass.name   = name;
        pass.record = [&cmd, name](ds::rhi::IRHICommandList&) { cmd.log.emplace_back(std::string("record:") + name); };
        return pass;
    };

    graph.addPass(makePass("a"));
    graph.addPass(makePass("b"));
    graph.addPass(makePass("c"));

    REQUIRE(graph.passCount() == 3);

    graph.execute(cmd);

    const std::vector<std::string> expected{
        "begin", "record:a", "end", "begin", "record:b", "end", "begin", "record:c", "end",
    };
    REQUIRE(cmd.log == expected);
}

TEST_CASE("RenderGraph::clear resets the pass list", "[rendergraph]") {
    ds::RenderGraph graph;
    MockCommandList cmd;

    ds::RenderPass pass;
    pass.name = "only";
    graph.addPass(std::move(pass));
    REQUIRE(graph.passCount() == 1);

    graph.clear();
    REQUIRE(graph.passCount() == 0);

    // Executing an empty graph emits nothing.
    graph.execute(cmd);
    REQUIRE(cmd.log.empty());
}

TEST_CASE("RenderGraph passes with no record lambda still bracket begin/end", "[rendergraph]") {
    ds::RenderGraph graph;
    MockCommandList cmd;

    ds::RenderPass pass;
    pass.name = "empty"; // no record callback set
    graph.addPass(std::move(pass));

    graph.execute(cmd);

    const std::vector<std::string> expected{"begin", "end"};
    REQUIRE(cmd.log == expected);
}
