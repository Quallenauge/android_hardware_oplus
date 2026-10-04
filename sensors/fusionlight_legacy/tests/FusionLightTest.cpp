/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

// What the correction is meant to achieve, independent of how it gets there.

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

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

// The correction recalculates every 5 s at the latest.
constexpr int32_t kRecalculateMs = 5100;
// Bright enough to leave the load of the panel out.
constexpr float kBrightRaw = 20000.0f;

Step At(int32_t level, int32_t red, int32_t green, int32_t blue, float raw,
        int32_t dt_ms = kRecalculateMs) {
    Step step;
    step.dt_ms = dt_ms;
    step.raw = raw;
    step.level = level;
    step.red = red;
    step.green = green;
    step.blue = blue;
    return step;
}

// Repeats an event until the correction follows the level of the panel and returns the last
// report it calculated. In between it reports the lux it had before, without the light it
// compensated for.
Record Settle(Rig& rig, const Step& step) {
    Record result{};
    for (int32_t i = 0; i < 16; ++i) {
        const Record record = rig.step(step);
        if (record.reported && record.comp >= 0.0f) {
            result = record;
        }
    }
    EXPECT_TRUE(result.reported);
    return result;
}

struct Color {
    const char* name;
    int32_t red;
    int32_t green;
    int32_t blue;
};

constexpr Color kColors[] = {
        {"white", 255, 255, 255}, {"grey 192", 192, 192, 192}, {"grey 128", 128, 128, 128},
        {"red", 255, 0, 0},       {"green", 0, 255, 0},        {"blue", 0, 0, 255},
};

// Light of the panel the HAL subtracted on the phone, read from its log while the ALS tester
// showed the colors in bright surroundings.
TEST(FusionLightTest, CompensatesLikeOnTheDevice) {
    SKIP_WITHOUT_PROFILE();
    struct Case {
        int32_t level;
        float comp[6];
    };
    const Case cases[] = {
            {838, {1537, 796, 328, 449, 642, 518}},
            {1811, {3244, 1680, 693, 948, 1355, 1093}},
            {2047, {3645, 1888, 778, 1065, 1523, 1228}},
    };
    for (const auto& test : cases) {
        for (size_t i = 0; i < std::size(kColors); ++i) {
            Rig rig;
            const Color& color = kColors[i];
            const Record record =
                    Settle(rig, At(test.level, color.red, color.green, color.blue, kBrightRaw));
            EXPECT_NEAR(test.comp[i], record.comp, 1.0f) << color.name << " at " << test.level;
        }
    }
}

// The same in a dark room, where the tester lights a small patch above the sensor on a black
// screen and the correction accounts for the load of the panel.
TEST(FusionLightTest, CompensatesPatchLikeOnTheDevice) {
    SKIP_WITHOUT_PROFILE();
    const ScreenLoadWeights weights = ParsePanelLoad(kPanelLoad)->weights;
    // 270 x 140 pixels at the top edge of a 1080 x 2400 screen.
    const float patch = 270.0f * 140.0f / (1080.0f * 2400.0f);
    const float top_rows =
            weights.uniform + (1.0f - weights.uniform) * 2.0f * (1.0f - 70.0f / 2400.0f);
    struct Case {
        int32_t level;
        Color color;
        float raw;
        float comp;
    };
    const Case cases[] = {
            {838, kColors[0], 1667, 1648},  {1811, kColors[0], 3858, 3867},
            {2047, kColors[0], 4422, 4458}, {2047, kColors[1], 2201, 2082},
            {2047, kColors[3], 1106, 1100}, {2047, kColors[4], 1641, 1565},
            {2047, kColors[5], 1334, 1350},
    };
    for (const auto& test : cases) {
        Rig rig;
        Step step = At(test.level, test.color.red, test.color.green, test.color.blue, test.raw);
        step.load = patch * top_rows *
                    weights.of(ScreenColor{test.color.red, test.color.green, test.color.blue, {}});
        const Record record = Settle(rig, step);
        EXPECT_NEAR(test.comp, record.comp, test.comp * 0.005f)
                << test.color.name << " at " << test.level;
    }
}

// If the sensor sees exactly the ambient light plus what the correction expects from the
// panel, the lux does not depend on what the screen shows.
TEST(FusionLightTest, LuxIsIndependentOfScreenContent) {
    SKIP_WITHOUT_PROFILE();
    for (const int32_t level : {67, 838, 2047}) {
        for (const float ambient : {8000.0f, 20000.0f}) {
            Rig black;
            const Record reference = Settle(black, At(level, 0, 0, 0, ambient));
            EXPECT_FLOAT_EQ(0.0f, reference.comp);
            EXPECT_GT(reference.lux, 0.0f);

            for (const auto& color : kColors) {
                Rig measure;
                const float comp =
                        Settle(measure, At(level, color.red, color.green, color.blue, kBrightRaw))
                                .comp;
                EXPECT_GT(comp, 0.0f);
                Rig rig;
                const Record record =
                        Settle(rig, At(level, color.red, color.green, color.blue, ambient + comp));
                EXPECT_NEAR(reference.lux, record.lux, reference.lux * 0.002f)
                        << color.name << " at " << level;
            }
        }
    }
}

TEST(FusionLightTest, BrighterContentCompensatesMore) {
    SKIP_WITHOUT_PROFILE();
    float last = -1.0f;
    for (const int32_t code : {0, 32, 64, 128, 192, 255}) {
        Rig rig;
        const float comp = Settle(rig, At(1561, code, code, code, kBrightRaw)).comp;
        EXPECT_GT(comp, last) << "grey " << code;
        last = comp;
    }
    last = -1.0f;
    for (const int32_t level : {8, 67, 287, 838, 1561, 2047}) {
        Rig rig;
        const float comp = Settle(rig, At(level, 255, 255, 255, kBrightRaw)).comp;
        EXPECT_GT(comp, last) << "level " << level;
        last = comp;
    }
}

TEST(FusionLightTest, LuxFollowsTheAmbientLight) {
    SKIP_WITHOUT_PROFILE();
    float last = -1.0f;
    for (const float raw : {4000.0f, 8000.0f, 20000.0f, 60000.0f, 150000.0f}) {
        Rig rig;
        const float lux = Settle(rig, At(838, 255, 255, 255, raw)).lux;
        EXPECT_GT(lux, last) << "raw " << raw;
        last = lux;
    }
}

// Without light of the panel the raw value only needs the factory calibration.
TEST(FusionLightTest, ScreenOffReportsTheCalibratedRawValue) {
    SKIP_WITHOUT_PROFILE();
    Rig rig;
    Settle(rig, At(838, 255, 255, 255, 4000.0f));
    const Record record = rig.step(At(0, 0, 0, 0, 2000.0f, 200));
    EXPECT_TRUE(record.reported);
    EXPECT_NEAR(0.11f * 2000.0f * 1.226f, record.lux, 0.01f);
    EXPECT_FALSE(record.capturing);
}

// The level of the panel is converted to the brightness the profile is based on.
TEST(FusionLightTest, ConvertsThePanelLevel) {
    SKIP_WITHOUT_PROFILE();
    struct Case {
        int32_t level;
        int32_t brightness;
    };
    for (const auto& test :
         {Case{1, 1}, Case{2, 2}, Case{4, 4}, Case{15, 189}, Case{287, 837}, Case{838, 1364},
          Case{1811, 1937}, Case{2047, 2047}, Case{2400, 2400}}) {
        Rig rig;
        EXPECT_FLOAT_EQ(test.brightness, Settle(rig, At(test.level, 0, 0, 0, kBrightRaw)).level)
                << "level " << test.level;
    }
    RigOptions options;
    options.remap = false;
    Rig rig(options);
    EXPECT_FLOAT_EQ(838.0f, Settle(rig, At(838, 0, 0, 0, kBrightRaw)).level);
}

// Capturing the whole screen is expensive and only pays off while the light of the panel is a
// large part of what the sensor sees.
TEST(FusionLightTest, CapturesTheWholeScreenOnlyInDimSurroundings) {
    SKIP_WITHOUT_PROFILE();
    // Twice the light of a white screen according to the factory calibration.
    const float limit = 2.0f * (1073.0f + 1562.0f + 1231.0f);
    Rig rig;
    EXPECT_TRUE(rig.step(At(838, 255, 255, 255, limit - 1.0f)).whole_screen);
    EXPECT_FALSE(rig.step(At(838, 255, 255, 255, limit)).whole_screen);
    EXPECT_FALSE(rig.step(At(838, 255, 255, 255, 50000.0f)).whole_screen);
    EXPECT_TRUE(rig.step(At(838, 255, 255, 255, 0.0f)).whole_screen);

    RigOptions options;
    options.load = false;
    Rig without(options);
    EXPECT_FALSE(without.step(At(838, 255, 255, 255, 100.0f)).whole_screen);
}

// A bright area above the sensor emits more light the darker the rest of the screen is.
TEST(FusionLightTest, DarkerScreensCompensateMore) {
    SKIP_WITHOUT_PROFILE();
    float last = 1e9f;
    for (const float load : {0.02f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        Rig rig;
        Step step = At(2047, 255, 255, 255, 5000.0f);
        step.load = load;
        const float comp = Settle(rig, step).comp;
        EXPECT_LT(comp, last) << "load " << load;
        last = comp;
    }
    // The load does not matter in bright surroundings.
    Rig dark;
    Rig lit;
    Step step = At(2047, 255, 255, 255, kBrightRaw);
    step.load = 0.02f;
    const float patch = Settle(dark, step).comp;
    step.load = 1.0f;
    EXPECT_FLOAT_EQ(patch, Settle(lit, step).comp);
}

TEST(FusionLightTest, CapturesOnlyWhileRecalculating) {
    SKIP_WITHOUT_PROFILE();
    Rig rig;
    const Step steady = At(838, 128, 128, 128, 9000.0f, 200);
    Settle(rig, steady);
    for (int32_t i = 0; i < 5; ++i) {
        rig.step(steady);
    }
    const int32_t samples = rig.sampler().samples;
    for (int32_t i = 0; i < 5; ++i) {
        EXPECT_FALSE(rig.step(steady).capturing);
    }
    EXPECT_EQ(samples, rig.sampler().samples);

    // A clear change of the light asks for a capture, which the next event uses.
    Step changed = steady;
    changed.raw = 30000.0f;
    EXPECT_TRUE(rig.step(changed).capturing);
    const Record record = rig.step(changed);
    EXPECT_FALSE(record.capturing);
    EXPECT_GT(rig.sampler().samples, samples);
    EXPECT_TRUE(record.reported);
}

// While the panel flashes below a finger the sensor mostly sees that flash.
TEST(FusionLightTest, HoldsTheLuxDuringAFingerprintFlash) {
    SKIP_WITHOUT_PROFILE();
    Rig rig;
    const Step steady = At(287, 128, 128, 128, 2500.0f, 200);
    const float lux = Settle(rig, steady).lux;

    Step flash = At(287, 255, 255, 255, 90000.0f, 200);
    flash.finger = true;
    flash.alpha = 234;
    for (int32_t i = 0; i < 6; ++i) {
        const Record record = rig.step(flash);
        if (record.reported) {
            EXPECT_FLOAT_EQ(lux, record.lux) << "event " << i;
        }
    }
}

// The lux held back is from before the sensor was disabled, which may be hours ago.
TEST(FusionLightTest, DoesNotReportTheLuxOfAnEarlierActivation) {
    SKIP_WITHOUT_PROFILE();
    Rig rig;
    const float dark = Settle(rig, At(287, 128, 128, 128, 30.0f, 200)).lux;
    EXPECT_LT(dark, 10.0f);

    Step flash = At(8, 255, 255, 255, 90000.0f, 200);
    flash.finger = true;
    flash.alpha = 234;
    flash.enable = 0;
    rig.step(flash);
    flash.enable = 1;
    flash.dt_ms = 3'600'000;
    EXPECT_FALSE(rig.step(flash).reported);
    flash.enable = -1;
    flash.dt_ms = 200;
    for (int32_t i = 0; i < 4; ++i) {
        EXPECT_FALSE(rig.step(flash).reported) << "event " << i;
    }

    bool reported = false;
    for (int32_t i = 0; i < 12 && !reported; ++i) {
        const Record record = rig.step(At(287, 128, 128, 128, 90000.0f, 200));
        if (record.reported) {
            reported = true;
            EXPECT_GT(record.lux, 1000.0f);
        }
    }
    EXPECT_TRUE(reported);
}

TEST(FusionLightTest, DisabledSensorPassesEventsThrough) {
    SKIP_WITHOUT_PROFILE();
    Rig rig;
    Step step = At(838, 255, 255, 255, 4000.0f, 200);
    step.enable = 0;
    const Record record = rig.step(step);
    EXPECT_TRUE(record.reported);
    EXPECT_FLOAT_EQ(4000.0f, record.lux);
    EXPECT_FALSE(rig.sampler().started);
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
