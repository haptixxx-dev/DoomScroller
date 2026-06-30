#include "engine/InstanceBatch.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace ds;

namespace {

// Distinct, non-null fake handle pointers. The grouping only compares pointer
// identity, so any stable distinct addresses work.
int meshA = 0;
int meshB = 0;
int idxA  = 0;
int idxB  = 0;
int texA  = 0;
int texB  = 0;

InstanceKey keyA() {
    return InstanceKey{&meshA, &idxA, &texA};
}
InstanceKey keyB() {
    return InstanceKey{&meshB, &idxB, &texB};
}

glm::mat4 modelAt(float x) {
    return glm::translate(glm::mat4(1.f), glm::vec3(x, 0.f, 0.f));
}

} // namespace

TEST_CASE("buildBatches groups draws sharing a key", "[instancing]") {
    std::array<InstanceDraw, 3> draws{
        InstanceDraw{keyA(), 36, rhi::IndexType::Uint16, modelAt(0.f)},
        InstanceDraw{keyB(), 6, rhi::IndexType::Uint32, modelAt(1.f)},
        InstanceDraw{keyA(), 36, rhi::IndexType::Uint16, modelAt(2.f)},
    };

    std::vector<DrawBatch> batches = buildBatches(draws);

    REQUIRE(batches.size() == 2);

    // First batch is keyA (first appearance), holding the two keyA draws in
    // input order.
    REQUIRE(batches[0].key == keyA());
    REQUIRE(batches[0].indexCount == 36);
    REQUIRE(batches[0].indexType == rhi::IndexType::Uint16);
    REQUIRE(batches[0].models.size() == 2);
    REQUIRE(batches[0].models[0][3][0] == Catch::Approx(0.f));
    REQUIRE(batches[0].models[1][3][0] == Catch::Approx(2.f));

    // Second batch is the lone keyB draw.
    REQUIRE(batches[1].key == keyB());
    REQUIRE(batches[1].indexCount == 6);
    REQUIRE(batches[1].indexType == rhi::IndexType::Uint32);
    REQUIRE(batches[1].models.size() == 1);
    REQUIRE(batches[1].models[0][3][0] == Catch::Approx(1.f));
}

TEST_CASE("buildBatches on empty input yields empty output", "[instancing]") {
    std::span<const InstanceDraw> empty{};
    std::vector<DrawBatch> batches = buildBatches(empty);
    REQUIRE(batches.empty());
}

TEST_CASE("buildBatches preserves model order within a batch", "[instancing]") {
    std::array<InstanceDraw, 4> draws{
        InstanceDraw{keyA(), 36, rhi::IndexType::Uint16, modelAt(10.f)},
        InstanceDraw{keyA(), 36, rhi::IndexType::Uint16, modelAt(20.f)},
        InstanceDraw{keyA(), 36, rhi::IndexType::Uint16, modelAt(30.f)},
        InstanceDraw{keyA(), 36, rhi::IndexType::Uint16, modelAt(40.f)},
    };

    std::vector<DrawBatch> batches = buildBatches(draws);

    REQUIRE(batches.size() == 1);
    REQUIRE(batches[0].models.size() == 4);
    REQUIRE(batches[0].models[0][3][0] == Catch::Approx(10.f));
    REQUIRE(batches[0].models[1][3][0] == Catch::Approx(20.f));
    REQUIRE(batches[0].models[2][3][0] == Catch::Approx(30.f));
    REQUIRE(batches[0].models[3][3][0] == Catch::Approx(40.f));
}

TEST_CASE("InstanceKey equality and hash distinguish handles", "[instancing]") {
    REQUIRE(keyA() == keyA());
    REQUIRE(keyA() != keyB());

    InstanceKeyHash hash;
    // Equal keys must hash equal; distinct keys are very likely (not required)
    // to differ — we only assert the contract that matters for correctness.
    REQUIRE(hash(keyA()) == hash(keyA()));

    // A key differing in only one field is not equal.
    InstanceKey mixed{&meshA, &idxA, &texB};
    REQUIRE(mixed != keyA());
}
