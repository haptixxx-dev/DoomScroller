#include "engine/BloomKernel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("luminance of white is 1 and black is 0", "[bloom]") {
    REQUIRE(luminance(glm::vec3(1.0f)) == Catch::Approx(1.0f));
    REQUIRE(luminance(glm::vec3(0.0f)) == Catch::Approx(0.0f));
}

TEST_CASE("brightPass kills a dim color below the knee", "[bloom]") {
    // luminance ~0.2 with threshold 1.0, knee 0.5 -> well below threshold-knee=0.5.
    glm::vec3 dim = brightPass(glm::vec3(0.2f), 1.0f, 0.5f);
    REQUIRE(dim.x == Catch::Approx(0.0f));
    REQUIRE(dim.y == Catch::Approx(0.0f));
    REQUIRE(dim.z == Catch::Approx(0.0f));
}

TEST_CASE("brightPass passes a very bright color through", "[bloom]") {
    glm::vec3 bright = brightPass(glm::vec3(5.0f), 1.0f, 0.5f);
    REQUIRE(bright.x > 0.0f);
    REQUIRE(bright.y > 0.0f);
    REQUIRE(bright.z > 0.0f);
    // Hue preserved (grey stays grey).
    REQUIRE(bright.x == Catch::Approx(bright.y));
    REQUIRE(bright.y == Catch::Approx(bright.z));
}

TEST_CASE("luminance pins the exact Rec.709 per-channel coefficients", "[bloom]") {
    // Grey inputs can't distinguish the three coefficients (they sum to 1), so
    // probe each channel in isolation to pin R/G/B against corruption.
    REQUIRE(luminance(glm::vec3(1.f, 0.f, 0.f)) == Catch::Approx(0.2126f));
    REQUIRE(luminance(glm::vec3(0.f, 1.f, 0.f)) == Catch::Approx(0.7152f));
    REQUIRE(luminance(glm::vec3(0.f, 0.f, 1.f)) == Catch::Approx(0.0722f));
}

TEST_CASE("brightPass pins the over-threshold weight and soft-knee math", "[bloom]") {
    // Well above the band (b=5, threshold=1): contribution = b-threshold = 4,
    // weight = 4/5 = 0.8, so color 5 * 0.8 = 4.0 (hue preserved).
    REQUIRE(brightPass(glm::vec3(5.f), 1.f, 0.5f).x == Catch::Approx(4.0f).epsilon(1e-3f));

    // Soft-knee band case: b=luminance(vec3(1))=1.0 sits at threshold (1) inside
    // the band (threshold-knee, threshold+knee) = (0.5, 1.5). Per the header:
    //   soft = clamp(1 - 1 + 0.5, 0, 2*0.5) = 0.5
    //   soft = 0.5*0.5 / (4*0.5 + eps)      = 0.25/2.0 ~= 0.125
    //   contribution = max(0.125, 1 - 1)    = 0.125
    //   weight       = 0.125 / 1            = 0.125
    //   result.x     = 1 * 0.125            = 0.125
    REQUIRE(brightPass(glm::vec3(1.f), 1.f, 0.5f).x == Catch::Approx(0.125f).epsilon(1e-3f));
}

TEST_CASE("gaussianKernel(2,1.0) has 5 symmetric normalized taps", "[bloom]") {
    GaussianKernel k = gaussianKernel(2, 1.0f);
    REQUIRE(k.taps == 5);

    // Symmetric about the center (index radius == 2).
    REQUIRE(k.weights[0] == Catch::Approx(k.weights[4]));
    REQUIRE(k.weights[1] == Catch::Approx(k.weights[3]));

    // Normalized.
    float sum = 0.f;
    for (int i = 0; i < k.taps; ++i)
        sum += k.weights[i];
    REQUIRE(sum == Catch::Approx(1.0f));

    // Center tap is the largest.
    REQUIRE(k.weights[2] > k.weights[1]);
    REQUIRE(k.weights[2] > k.weights[0]);
}

TEST_CASE("gaussianKernel clamps radius so taps never exceed kMaxTaps", "[bloom]") {
    GaussianKernel k = gaussianKernel(100, 2.0f); // absurd radius
    REQUIRE(k.taps <= GaussianKernel::kMaxTaps);
    REQUIRE(k.taps == 2 * ((GaussianKernel::kMaxTaps - 1) / 2) + 1);

    GaussianKernel low = gaussianKernel(0, 1.0f); // below minimum
    REQUIRE(low.taps == 3);                       // clamped up to radius 1

    // Still normalized after clamping.
    float sum = 0.f;
    for (int i = 0; i < k.taps; ++i)
        sum += k.weights[i];
    REQUIRE(sum == Catch::Approx(1.0f));
}
