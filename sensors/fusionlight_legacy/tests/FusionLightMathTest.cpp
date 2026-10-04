/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "FusionLightMath.h"
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

constexpr ScreenColor kBlack{0, 0, 0, {}};
constexpr ScreenColor kWhite{255, 255, 255, {}};

TEST(BrightnessRemapTest, WithoutTableTheLevelIsTheBrightness) {
    for (const int32_t level : {-1, 0, 1, 838, 2047, 4095}) {
        EXPECT_EQ(level, ToBrightness({}, level));
    }
}

TEST(BrightnessRemapTest, InterpolatesTheTableBackwards) {
    const auto table = ParseBrightnessRemap(kBrightnessRemap);
    // Points of the table.
    EXPECT_EQ(4, ToBrightness(table, 4));
    EXPECT_EQ(61, ToBrightness(table, 5));
    EXPECT_EQ(797, ToBrightness(table, 256));
    EXPECT_EQ(2047, ToBrightness(table, 2047));
    EXPECT_EQ(2784, ToBrightness(table, 2784));
    // Between them, in whole numbers.
    EXPECT_EQ(189, ToBrightness(table, 15));
    EXPECT_EQ(837, ToBrightness(table, 287));
    EXPECT_EQ(1364, ToBrightness(table, 838));
    EXPECT_EQ(1937, ToBrightness(table, 1811));
    EXPECT_EQ(2400, ToBrightness(table, 2400));
    // Below the first and above the last point.
    EXPECT_EQ(0, ToBrightness(table, 0));
    EXPECT_EQ(1, ToBrightness(table, 1));
    EXPECT_EQ(3, ToBrightness(table, 3));
    EXPECT_EQ(3000, ToBrightness(table, 3000));
}

TEST(BrightnessRemapTest, NeverDecreases) {
    const auto table = ParseBrightnessRemap(kBrightnessRemap);
    int32_t last = 0;
    for (int32_t level = 0; level <= 3000; ++level) {
        const int32_t brightness = ToBrightness(table, level);
        EXPECT_GE(brightness, last) << "level " << level;
        last = brightness;
    }
}

TEST(PanelLoadFactorTest, UnknownLoadChangesNothing) {
    const PanelLoadArgs load = *ParsePanelLoad(kPanelLoad);
    EXPECT_FLOAT_EQ(1.0f, PanelLoadFactor(load, kWhite, 2047, 2047));
    ScreenColor color = kWhite;
    color.load = 0.5f;
    EXPECT_FLOAT_EQ(1.0f, PanelLoadFactor(load, color, 2047, 0));
}

// A screen that shows the color above the sensor everywhere is what the profile describes, so
// only the fixed scale applies.
TEST(PanelLoadFactorTest, UniformScreenOnlyScales) {
    const PanelLoadArgs load = *ParsePanelLoad(kPanelLoad);
    for (const auto& base : {kWhite, ScreenColor{255, 0, 0, {}}, ScreenColor{40, 200, 90, {}}}) {
        for (const int32_t level : {100, 838, 2047}) {
            ScreenColor color = base;
            color.load = load.weights.of(base);
            EXPECT_NEAR(load.scale, PanelLoadFactor(load, color, level, 2047), 1e-6f);
        }
    }
}

TEST(PanelLoadFactorTest, DarkerScreensEmitMore) {
    const PanelLoadArgs load = *ParsePanelLoad(kPanelLoad);
    float last = 0.0f;
    for (const float screen_load : {1.0f, 0.75f, 0.5f, 0.25f, 0.02f, 0.0f}) {
        ScreenColor color = kWhite;
        color.load = screen_load;
        const float factor = PanelLoadFactor(load, color, 2047, 2047);
        EXPECT_GT(factor, last) << "load " << screen_load;
        last = factor;
    }
    // An otherwise black screen at full brightness emits about a fifth more.
    EXPECT_NEAR(1.23f, last, 0.01f);

    // The effect shrinks with the brightness.
    ScreenColor patch = kWhite;
    patch.load = 0.0f;
    EXPECT_LT(PanelLoadFactor(load, patch, 838, 2047), PanelLoadFactor(load, patch, 2047, 2047));
    EXPECT_GT(PanelLoadFactor(load, patch, 838, 2047), 1.0f);
}

TEST(LeakageTest, FollowsTheFactoryCalibration) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    const PanelLeakage black = LeakageAtFullBrightness(args, kBlack);
    EXPECT_FLOAT_EQ(0.0f, black.red);
    EXPECT_FLOAT_EQ(0.0f, black.green);
    EXPECT_FLOAT_EQ(0.0f, black.blue);
    EXPECT_NEAR(0.0f, black.total(), 0.5f);

    // Full channels reach about what the sensor measured in the factory.
    const PanelLeakage white = LeakageAtFullBrightness(args, kWhite);
    EXPECT_NEAR(args.r.max, white.red, args.r.max * 0.03f);
    EXPECT_NEAR(args.g.max, white.green, args.g.max * 0.03f);
    EXPECT_NEAR(args.b.max, white.blue, args.b.max * 0.03f);
    EXPECT_NEAR(args.w.max, white.total(), args.w.max * 0.03f);
    // Together the channels leak less than their sum.
    EXPECT_GT(white.overlap, 0.0f);

    // The channels do not depend on each other.
    const PanelLeakage red = LeakageAtFullBrightness(args, ScreenColor{255, 0, 0, {}});
    EXPECT_FLOAT_EQ(white.red, red.red);
    EXPECT_FLOAT_EQ(0.0f, red.green);
    EXPECT_FLOAT_EQ(0.0f, red.blue);
}

TEST(LeakageTest, GrowsWithTheCodeValue) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    float last = -1.0f;
    for (int32_t code = 0; code <= 255; code += 15) {
        const float total =
                LeakageAtFullBrightness(args, ScreenColor{code, code, code, {}}).total();
        EXPECT_GT(total, last) << "grey " << code;
        last = total;
    }
}

// Light of a white screen the HAL subtracted on the phone, read from its log.
TEST(LeakageTest, FollowsTheBrightnessLikeOnTheDevice) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    const PanelLeakage white = LeakageAtFullBrightness(args, kWhite);
    EXPECT_NEAR(3645.0f, LeakageAtBrightness(args, white, 2047, 2047, kWhite, 0), 1.0f);
    EXPECT_NEAR(3244.0f, LeakageAtBrightness(args, white, 1937, 2047, kWhite, 0), 1.0f);
    EXPECT_NEAR(1537.0f, LeakageAtBrightness(args, white, 1364, 2047, kWhite, 0), 1.0f);
}

TEST(LeakageTest, GrowsWithTheBrightness) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    const PanelLeakage white = LeakageAtFullBrightness(args, kWhite);
    // Below the first point of the curve the leakage stays what it is there.
    const float lowest = LeakageAtBrightness(args, white, 4, 2047, kWhite, 0);
    EXPECT_GT(lowest, 0.0f);
    EXPECT_FLOAT_EQ(lowest, LeakageAtBrightness(args, white, 1, 2047, kWhite, 0));
    // At low brightness the curve is nearly flat and not strictly rising, above its second point
    // the leakage grows with every step.
    for (int32_t level = 4; level < 260; level += 16) {
        const float leakage = LeakageAtBrightness(args, white, level, 2047, kWhite, 0);
        EXPECT_GT(leakage, lowest * 0.9f) << "level " << level;
        EXPECT_LT(leakage, lowest * 2.5f) << "level " << level;
    }
    float last = LeakageAtBrightness(args, white, 260, 2047, kWhite, 0);
    for (int32_t level = 300; level <= 2047; level += 50) {
        const float leakage = LeakageAtBrightness(args, white, level, 2047, kWhite, 0);
        EXPECT_GT(leakage, last) << "level " << level;
        last = leakage;
    }
    EXPECT_GT(last, lowest * 50.0f);
}

// Without a brightness curve the leakage is proportional to the level.
TEST(LeakageTest, IsLinearWithoutCurve) {
    FusionLightArgs args;
    DeriveLimits(args);
    ASSERT_EQ(0, args.brightness_curve.type);
    const PanelLeakage white = LeakageAtFullBrightness(args, kWhite);
    EXPECT_FLOAT_EQ(white.total(), LeakageAtBrightness(args, white, 1023, 1023, kWhite, 0));
    EXPECT_FLOAT_EQ(white.total() * 300 / 1023.0f,
                    LeakageAtBrightness(args, white, 300, 1023, kWhite, 0));
    EXPECT_FLOAT_EQ(0.0f, LeakageAtBrightness(args, white, 0, 1023, kWhite, 0));
}

TEST(LuxPerRawTest, DefaultIsTheFirstFactor) {
    const FusionLightArgs args;
    for (const float clear : {0.0f, 50.0f, 500.0f}) {
        EXPECT_FLOAT_EQ(args.lux_per_raw[0], LuxPerRaw(args, 20000.0f, clear, 20.0f));
    }
}

// The clear channel selects one of five lines.
TEST(LuxPerRawTest, FollowsTheLinesOfTheProfile) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    ASSERT_EQ(5, args.lux_factor_segments);
    const auto line = [&](size_t index, float clear) {
        return args.lux_factor_lines[index][0] * clear + args.lux_factor_lines[index][1];
    };
    const auto& thresholds = args.clear_thresholds;
    EXPECT_FLOAT_EQ(line(0, 0.0f), LuxPerRaw(args, 20000.0f, 0.0f, 20.0f));
    EXPECT_FLOAT_EQ(line(0, thresholds[0]), LuxPerRaw(args, 20000.0f, thresholds[0], 20.0f));
    EXPECT_FLOAT_EQ(line(1, thresholds[0] + 1.0f),
                    LuxPerRaw(args, 20000.0f, thresholds[0] + 1.0f, 20.0f));
    EXPECT_FLOAT_EQ(line(2, thresholds[2]), LuxPerRaw(args, 20000.0f, thresholds[2], 20.0f));
    EXPECT_FLOAT_EQ(line(3, thresholds[3]), LuxPerRaw(args, 20000.0f, thresholds[3], 20.0f));
    EXPECT_FLOAT_EQ(line(4, thresholds[3] + 1.0f),
                    LuxPerRaw(args, 20000.0f, thresholds[3] + 1.0f, 20.0f));
    EXPECT_FLOAT_EQ(line(4, 5000.0f), LuxPerRaw(args, 20000.0f, 5000.0f, 20.0f));
    // The raw value does not matter for this profile.
    EXPECT_FLOAT_EQ(LuxPerRaw(args, 2000.0f, 60.0f, 20.0f),
                    LuxPerRaw(args, 90000.0f, 60.0f, 20.0f));
}

TEST(DimLayerTest, NoLayerKeepsTheLevel) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    EXPECT_FLOAT_EQ(500.0f, LevelBehindDimLayer(args, 500.0f, 2047, 0));
    EXPECT_FLOAT_EQ(0.0f, LevelBehindDimLayer(args, 0.0f, 2047, 200));
}

TEST(DimLayerTest, FollowsTheProfile) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    // From the log of the HAL on the phone.
    EXPECT_NEAR(2121.23f, LevelBehindDimLayer(args, 2106.0f, 2047, 36), 0.01f);
    // The alpha itself does not matter for this profile, only that the layer is there.
    EXPECT_FLOAT_EQ(LevelBehindDimLayer(args, 2106.0f, 2047, 36),
                    LevelBehindDimLayer(args, 2106.0f, 2047, 234));
    // At low brightness the panel behind the layer is brighter than its level says.
    EXPECT_GT(LevelBehindDimLayer(args, 50.0f, 2047, 234), 50.0f);
    EXPECT_GT(LevelBehindDimLayer(args, 100.0f, 2047, 234),
              LevelBehindDimLayer(args, 50.0f, 2047, 234));
}

TEST(DcDimmingTest, OnlyAppliesAtLowBrightness) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    EXPECT_FLOAT_EQ(100.0f, LevelWithDcDimming(args, 100.0f, 0));
    EXPECT_FLOAT_EQ(0.0f, LevelWithDcDimming(args, 0.0f, 150));
    EXPECT_FLOAT_EQ(args.dc_dim_max_level, LevelWithDcDimming(args, args.dc_dim_max_level, 150));
    EXPECT_FLOAT_EQ(1000.0f, LevelWithDcDimming(args, 1000.0f, 150));

    // Below the limit the panel is brighter than its level says, more so the darker it is.
    const float low = LevelWithDcDimming(args, 100.0f, 150);
    const float high = LevelWithDcDimming(args, 350.0f, 150);
    EXPECT_NEAR(144.1f, low, 0.5f);
    EXPECT_NEAR(379.0f, high, 0.5f);
    EXPECT_GT(low / 100.0f, high / 350.0f);
    // The alpha itself does not matter for this profile.
    EXPECT_FLOAT_EQ(low, LevelWithDcDimming(args, 100.0f, 255));
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
