/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include "FusionLight.h"
#include "ScreenSampler.h"
#include "TestRig.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace test {
namespace {

// An RGBA_8888 frame, with a stride wider than the frame like gralloc buffers have.
class Frame {
  public:
    Frame(int32_t width, int32_t height)
        : width_(width), height_(height), stride_(width + 8), pixels_(stride_ * height * 4, 0) {}

    void fill(int32_t left, int32_t top, int32_t right, int32_t bottom, int32_t red, int32_t green,
              int32_t blue) {
        for (int32_t y = top; y < bottom; ++y) {
            for (int32_t x = left; x < right; ++x) {
                uint8_t* pixel = &pixels_[(static_cast<size_t>(y) * stride_ + x) * 4];
                pixel[0] = red;
                pixel[1] = green;
                pixel[2] = blue;
                pixel[3] = 255;
            }
        }
    }

    void fill(int32_t red, int32_t green, int32_t blue) {
        fill(0, 0, width_, height_, red, green, blue);
    }

    ScreenColor sample(const ScreenRegion& region,
                       const ScreenLoadWeights* weights = nullptr) const {
        return SampleFrame(pixels_.data(), static_cast<size_t>(stride_) * 4, width_, height_,
                           region, weights);
    }

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }

  private:
    const int32_t width_;
    const int32_t height_;
    const int32_t stride_;
    std::vector<uint8_t> pixels_;
};

// Region above the light sensor of lunaa.
constexpr ScreenRegion kRegion{730, 0, 760, 20};

ScreenLoadWeights Weights() {
    return ParsePanelLoad(kPanelLoad)->weights;
}

// Code value sampled from a region that shows one code value everywhere. The panel hardly emits
// light for the darkest values, which makes them indistinguishable.
// clang-format off
constexpr std::array<int32_t, 256> kUniformCodes = {
#include "UniformCodes.inc"
};
// clang-format on

TEST(ScreenSamplerTest, UniformRegion) {
    Frame frame(800, 40);
    if (getenv("FUSIONLIGHT_RECORD") != nullptr) {
        for (int32_t code = 0; code < 256; ++code) {
            frame.fill(code, code, code);
            printf("%d,%s", frame.sample(kRegion).red, code % 16 == 15 ? "\n" : " ");
        }
        return;
    }
    for (int32_t code = 0; code < 256; ++code) {
        frame.fill(code, 255 - code, code / 2);
        const ScreenColor color = frame.sample(kRegion);
        EXPECT_EQ(kUniformCodes[code], color.red) << "code " << code;
        EXPECT_EQ(kUniformCodes[255 - code], color.green) << "code " << 255 - code;
        EXPECT_EQ(kUniformCodes[code / 2], color.blue) << "code " << code / 2;
        EXPECT_FALSE(color.load.has_value());
    }
}

TEST(ScreenSamplerTest, BrightCodesAreKept) {
    for (int32_t code = 32; code < 256; ++code) {
        EXPECT_EQ(code, kUniformCodes[code]);
    }
    EXPECT_EQ(0, kUniformCodes[0]);
}

TEST(ScreenSamplerTest, OnlyTheRegionCounts) {
    Frame frame(1080, 200);
    frame.fill(255, 255, 255);
    frame.fill(kRegion.left, kRegion.top, kRegion.right, kRegion.bottom, 0, 128, 255);
    const ScreenColor color = frame.sample(kRegion);
    EXPECT_EQ(0, color.red);
    EXPECT_EQ(128, color.green);
    EXPECT_EQ(255, color.blue);
}

TEST(ScreenSamplerTest, SamplesEveryThirdPixel) {
    Frame frame(800, 40);
    frame.fill(200, 200, 200);
    // Pixels between the sampled ones do not count.
    for (int32_t y = kRegion.top; y < kRegion.bottom; ++y) {
        for (int32_t x = kRegion.left; x < kRegion.right; ++x) {
            if ((x - kRegion.left) % 3 != 0 || (y - kRegion.top) % 3 != 0) {
                frame.fill(x, y, x + 1, y + 1, 0, 255, 17);
            }
        }
    }
    const ScreenColor color = frame.sample(kRegion);
    EXPECT_EQ(200, color.red);
    EXPECT_EQ(200, color.green);
    EXPECT_EQ(200, color.blue);
}

// The average is taken over the light of the panel, not over the code values.
TEST(ScreenSamplerTest, AveragesLight) {
    struct Case {
        int32_t first;
        int32_t second;
        int32_t expected;
    };
    // Left half of the region in the first value, right half in the second.
    const std::vector<Case> cases = {
#include "MixedCodes.inc"
    };
    Frame frame(800, 40);
    const int32_t middle = (kRegion.left + kRegion.right) / 2;
    if (getenv("FUSIONLIGHT_RECORD") != nullptr) {
        for (const int32_t first : {0, 16, 64, 128, 192}) {
            for (const int32_t second : {32, 100, 160, 224, 255}) {
                frame.fill(first, first, first);
                frame.fill(middle, 0, frame.width(), frame.height(), second, second, second);
                printf("{%d, %d, %d},\n", first, second, frame.sample(kRegion).red);
            }
        }
        return;
    }
    ASSERT_FALSE(cases.empty());
    for (const auto& test : cases) {
        frame.fill(test.first, test.first, test.first);
        frame.fill(middle, 0, frame.width(), frame.height(), test.second, test.second, test.second);
        const ScreenColor color = frame.sample(kRegion);
        EXPECT_EQ(test.expected, color.red) << test.first << " and " << test.second;
        EXPECT_EQ(test.expected, color.green);
        EXPECT_EQ(test.expected, color.blue);
        // Brighter than the average of the code values would be.
        EXPECT_GT(color.red, (test.first + test.second) / 2 - 1);
    }
}

TEST(ScreenSamplerTest, LoadOfUniformScreens) {
    const ScreenLoadWeights weights = Weights();
    Frame frame(1080, 2400);
    frame.fill(0, 0, 0);
    EXPECT_NEAR(0.0f, *frame.sample(kRegion, &weights).load, 1e-6f);

    frame.fill(255, 255, 255);
    EXPECT_NEAR(weights.red + weights.green + weights.blue, *frame.sample(kRegion, &weights).load,
                1e-3f);
    frame.fill(255, 0, 0);
    EXPECT_NEAR(weights.red, *frame.sample(kRegion, &weights).load, 1e-3f);
    frame.fill(0, 255, 0);
    EXPECT_NEAR(weights.green, *frame.sample(kRegion, &weights).load, 1e-3f);
    frame.fill(0, 0, 255);
    EXPECT_NEAR(weights.blue, *frame.sample(kRegion, &weights).load, 1e-3f);

    // The load follows the light, a medium grey emits about a fifth of white.
    frame.fill(128, 128, 128);
    const float grey = std::pow(128.0f / 255.0f, 2.2f);
    EXPECT_NEAR((weights.red + weights.green + weights.blue) * grey,
                *frame.sample(kRegion, &weights).load, 1e-3f);
}

TEST(ScreenSamplerTest, LoadMatchesColorLoad) {
    const ScreenLoadWeights weights = Weights();
    Frame frame(1080, 2400);
    for (const auto& color : {ScreenColor{255, 255, 255, {}}, ScreenColor{255, 191, 130, {}},
                              ScreenColor{40, 200, 90, {}}, ScreenColor{0, 0, 0, {}}}) {
        frame.fill(color.red, color.green, color.blue);
        EXPECT_NEAR(weights.of(color), *frame.sample(kRegion, &weights).load, 1e-3f);
    }
}

// Rows close to the top edge count more than rows at the bottom.
TEST(ScreenSamplerTest, LoadDependsOnPosition) {
    const ScreenLoadWeights weights = Weights();
    const float white = weights.red + weights.green + weights.blue;
    const float uniform = weights.uniform;
    Frame frame(1080, 2400);
    for (const float share : {0.1f, 0.25f, 0.5f, 0.75f}) {
        const auto rows = static_cast<int32_t>(share * frame.height());
        frame.fill(0, 0, 0);
        frame.fill(0, 0, frame.width(), rows, 255, 255, 255);
        const float top = *frame.sample(kRegion, &weights).load;
        frame.fill(0, 0, 0);
        frame.fill(0, frame.height() - rows, frame.width(), frame.height(), 255, 255, 255);
        const float bottom = *frame.sample(kRegion, &weights).load;

        EXPECT_GT(top, bottom);
        EXPECT_NEAR(white * (uniform * share +
                             (1.0f - uniform) * (1.0f - (1.0f - share) * (1.0f - share))),
                    top, 0.01f)
                << "top " << share;
        EXPECT_NEAR(white * (uniform * share + (1.0f - uniform) * share * share), bottom, 0.01f)
                << "bottom " << share;
        // Both parts together are the whole screen.
        frame.fill(0, 0, 0);
        frame.fill(0, 0, frame.width(), frame.height() - rows, 255, 255, 255);
        EXPECT_NEAR(white, bottom + *frame.sample(kRegion, &weights).load, 0.01f);
    }
}

TEST(ScreenSamplerTest, LoadIsIndependentOfResolution) {
    const ScreenLoadWeights weights = Weights();
    Frame large(1080, 2400);
    Frame small(720, 1600);
    for (Frame* frame : {&large, &small}) {
        frame->fill(0, 0, 0);
        frame->fill(0, 0, frame->width() / 2, frame->height() / 4, 255, 128, 0);
    }
    EXPECT_NEAR(*large.sample(kRegion, &weights).load, *small.sample(kRegion, &weights).load,
                5e-3f);
}

TEST(ScreenSamplerTest, ColorLoad) {
    const ScreenLoadWeights weights = Weights();
    EXPECT_FLOAT_EQ(0.0f, weights.of(ScreenColor{0, 0, 0, {}}));
    EXPECT_FLOAT_EQ(weights.red + weights.green + weights.blue,
                    weights.of(ScreenColor{255, 255, 255, {}}));
    EXPECT_FLOAT_EQ(weights.blue, weights.of(ScreenColor{0, 0, 255, {}}));
    EXPECT_NEAR(weights.green * std::pow(100.0f / 255.0f, 2.2f),
                weights.of(ScreenColor{0, 100, 0, {}}), 1e-6f);
    // Values a capture cannot have are limited.
    EXPECT_FLOAT_EQ(weights.red, weights.of(ScreenColor{300, -5, 0, {}}));
}

}  // anonymous namespace
}  // namespace test
}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
