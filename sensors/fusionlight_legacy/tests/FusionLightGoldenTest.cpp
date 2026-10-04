/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

// Replays sequences of sensor events and compares what the correction reports with recorded
// reference values. The references pin down the behaviour as a whole, so that the correction can
// be restructured without changing what it reports.
//
// After an intended change of behaviour run the test with FUSIONLIGHT_RECORD=1 and replace
// the entries of Goldens.h with the lines it prints between the ones of gtest:
//   FUSIONLIGHT_RECORD=1 ./fusionlight_legacy_test --gtest_filter='Scenarios/*' | grep -v '^\['
// The tables of ScreenSamplerTest (UniformCodes.inc, MixedCodes.inc) are recorded the same way.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
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

#include "Goldens.h"

struct Color {
    const char* name;
    int32_t red;
    int32_t green;
    int32_t blue;
};

constexpr Color kBlack{"black", 0, 0, 0};
constexpr Color kWhite{"white", 255, 255, 255};
constexpr Color kGrey192{"grey192", 192, 192, 192};
constexpr Color kGrey128{"grey128", 128, 128, 128};
constexpr Color kRed{"red", 255, 0, 0};
constexpr Color kGreen{"green", 0, 255, 0};
constexpr Color kBlue{"blue", 0, 0, 255};
// White with night light.
constexpr Color kWarm{"warm", 255, 191, 130};

constexpr Color kColors[] = {kBlack, kWhite, kGrey192, kGrey128, kRed, kGreen, kBlue, kWarm};
constexpr Color kPrimaries[] = {kWhite, kRed, kGreen, kBlue};

// The correction recalculates every 5 s at the latest.
constexpr int32_t kRecalculateMs = 5100;

Step At(int32_t level, const Color& color, float raw, int32_t dt_ms = 200) {
    Step step;
    step.dt_ms = dt_ms;
    step.raw = raw;
    step.level = level;
    step.red = color.red;
    step.green = color.green;
    step.blue = color.blue;
    return step;
}

void Repeat(std::vector<Step>& steps, const Step& step, int32_t count) {
    steps.insert(steps.end(), count, step);
}

// A float literal that reads back as the same value.
std::string Literal(float value) {
    if (std::isnan(value)) {
        return "NAN";
    }
    if (std::isinf(value)) {
        return value > 0.0f ? "INFINITY" : "-INFINITY";
    }
    char text[32];
    snprintf(text, sizeof(text), "%.9g", value);
    std::string result = text;
    if (result.find_first_of(".e") == std::string::npos) {
        result += ".0";
    }
    return result + "f";
}

void ExpectClose(float expected, float actual, const char* what) {
    if (std::isfinite(expected)) {
        EXPECT_NEAR(expected, actual, Tolerance(expected)) << what;
    } else if (std::isnan(expected)) {
        EXPECT_TRUE(std::isnan(actual)) << what << " is " << actual;
    } else {
        EXPECT_EQ(expected, actual) << what;
    }
}

struct Scenario {
    std::string name;
    RigOptions options;
    std::vector<Step> steps;
};

// Every color at several panel levels, in surroundings bright enough to leave out the panel load.
std::vector<Step> ColorsAtLevels(const std::vector<int32_t>& levels) {
    std::vector<Step> steps;
    for (const int32_t level : levels) {
        // The level the correction assumes only follows the panel at a limited speed.
        Repeat(steps, At(level, kBlack, 20000.0f, kRecalculateMs), 8);
        for (const auto& color : kColors) {
            Repeat(steps, At(level, color, 20000.0f, kRecalculateMs), 2);
        }
    }
    return steps;
}

// Dim surroundings, where the load of the whole screen is taken into account.
std::vector<Step> PanelLoad() {
    std::vector<Step> steps;
    for (const int32_t level : {838, 2047}) {
        Repeat(steps, At(level, kBlack, 5000.0f, kRecalculateMs), 8);
        for (const auto& color : {kWhite, kGrey192, kRed, kBlue}) {
            for (const float load : {0.02f, 0.25f, 0.5f, 1.0f, -1.0f}) {
                Step step = At(level, color, 5000.0f, kRecalculateMs);
                step.load = load;
                Repeat(steps, step, 2);
            }
        }
    }
    // The whole screen is not captured above twice the brightest light of the panel.
    for (const float raw : {7000.0f, 7700.0f, 7740.0f, 9000.0f, 7000.0f}) {
        Step step = At(2047, kWhite, raw, kRecalculateMs);
        step.load = 0.02f;
        Repeat(steps, step, 3);
    }
    return steps;
}

// The light around the phone changes while the screen stays the same.
std::vector<Step> AmbientSteps(int32_t level, const Color& color) {
    std::vector<Step> steps;
    Repeat(steps, At(level, color, 3000.0f, kRecalculateMs), 8);
    for (const float raw :
         {3000.0f,   3010.0f,  3050.0f,  3300.0f, 4000.0f, 8000.0f, 20000.0f, 60000.0f,
          150000.0f, 60000.0f, 20000.0f, 3000.0f, 1500.0f, 800.0f,  500.0f,   300.0f,
          100.0f,    50.0f,    20.0f,    5.0f,    0.0f,    3000.0f}) {
        Repeat(steps, At(level, color, raw), 4);
        steps.push_back(At(level, color, raw, kRecalculateMs));
        steps.push_back(At(level, color, raw));
    }
    return steps;
}

// The screen content changes while the sensor keeps seeing the same.
std::vector<Step> ContentChanges() {
    std::vector<Step> steps;
    Repeat(steps, At(1561, kBlack, 6000.0f, kRecalculateMs), 8);
    for (const auto& color : {kWhite, kBlack, kRed, kGrey128, kWarm, kBlack}) {
        Repeat(steps, At(1561, color, 6000.0f, 400), 16);
    }
    return steps;
}

std::vector<Step> ScreenOffAndOn() {
    std::vector<Step> steps;
    Repeat(steps, At(838, kWhite, 3000.0f, kRecalculateMs), 8);
    for (const float raw : {3000.0f, 2000.0f, 40.0f, 20.0f, 0.0f}) {
        Repeat(steps, At(0, kBlack, raw), 3);
    }
    Repeat(steps, At(838, kWhite, 3000.0f), 6);
    Repeat(steps, At(0, kBlack, 2000.0f, kRecalculateMs), 2);
    Repeat(steps, At(1561, kGrey192, 9000.0f), 10);
    Repeat(steps, At(1561, kGrey192, 9000.0f, kRecalculateMs), 3);
    return steps;
}

// The panel flashes below the finger, during which the last lux is held.
std::vector<Step> FingerprintFlash() {
    std::vector<Step> steps;
    Repeat(steps, At(287, kGrey128, 2500.0f, kRecalculateMs), 8);
    Step flash = At(287, kWhite, 60000.0f);
    flash.finger = true;
    flash.alpha = 234;
    Repeat(steps, flash, 6);
    // The touch state stays set after a successful unlock.
    Step unlocked = At(287, kGrey128, 2500.0f);
    unlocked.finger = true;
    Repeat(steps, unlocked, 8);
    Repeat(steps, At(287, kGrey128, 2500.0f), 4);
    Repeat(steps, flash, 3);
    Repeat(steps, At(287, kGrey128, 2600.0f), 8);
    steps.push_back(At(287, kGrey128, 2600.0f, kRecalculateMs));
    Repeat(steps, At(287, kGrey128, 2600.0f), 3);
    return steps;
}

// The dim layer of the fingerprint sensor darkens the screen at low brightness.
std::vector<Step> FingerprintDimLayer() {
    std::vector<Step> steps;
    for (const int32_t level : {8, 67, 287, 1561}) {
        Repeat(steps, At(level, kWhite, 20000.0f, kRecalculateMs), 8);
        for (const int32_t alpha : {0, 36, 100, 200, 234, 255, 0}) {
            Step step = At(level, kWhite, 20000.0f, kRecalculateMs);
            step.alpha = alpha;
            Repeat(steps, step, 3);
        }
    }
    return steps;
}

std::vector<Step> DcDimming() {
    std::vector<Step> steps;
    for (const int32_t level : {5, 19, 67, 114, 256}) {
        Repeat(steps, At(level, kWhite, 20000.0f, kRecalculateMs), 8);
        for (const int32_t dc_alpha : {0, 50, 150, 255, 0}) {
            Step step = At(level, kWhite, 20000.0f, kRecalculateMs);
            step.dc_alpha = dc_alpha;
            Repeat(steps, step, 3);
        }
    }
    return steps;
}

std::vector<Step> BrightnessRamp() {
    std::vector<Step> steps;
    Repeat(steps, At(8, kWhite, 6000.0f, kRecalculateMs), 4);
    for (int32_t level = 8; level < 2047; level = level * 3 / 2 + 1) {
        steps.push_back(At(level, kWhite, 6000.0f, 150));
    }
    Repeat(steps, At(2047, kWhite, 6000.0f), 12);
    for (int32_t level = 2047; level > 8; level = level * 2 / 3) {
        steps.push_back(At(level, kWhite, 6000.0f, 150));
    }
    Repeat(steps, At(8, kWhite, 6000.0f), 12);
    // Short and long periods of the sensor limit the assumed change of the level differently.
    for (const float period : {0.0f, 20.0f, 160.0f, 400.0f, 1000.0f}) {
        Step low = At(100, kWhite, 6000.0f, kRecalculateMs);
        Step high = At(2047, kWhite, 6000.0f, kRecalculateMs);
        low.period = high.period = period;
        Repeat(steps, low, 3);
        Repeat(steps, high, 3);
    }
    return steps;
}

// Events arriving faster than the correction accepts them.
std::vector<Step> EventTiming() {
    std::vector<Step> steps;
    Repeat(steps, At(838, kGrey192, 4000.0f, kRecalculateMs), 8);
    for (const int32_t dt_ms : {10, 50, 99, 100, 101, 150, 200, 1000, 4900, 5000, 5001, 60000}) {
        Repeat(steps, At(838, kGrey192, 4000.0f, dt_ms), 3);
        Repeat(steps, At(838, kGrey192, 4400.0f, dt_ms), 3);
    }
    return steps;
}

// Light close to the limit below which 0 is reported.
std::vector<Step> LowLight() {
    std::vector<Step> steps;
    for (const auto& color : {kBlack, kGrey128, kWhite}) {
        for (const int32_t level : {19, 838}) {
            Repeat(steps, At(level, color, 0.0f, kRecalculateMs), 8);
            for (const float raw : {0.0f, 1.0f, 5.0f, 10.0f, 18.0f, 19.0f, 22.0f, 23.0f, 30.0f,
                                    50.0f, 100.0f, 200.0f, 400.0f, 1000.0f, 0.0f}) {
                Repeat(steps, At(level, color, raw, kRecalculateMs), 3);
            }
        }
    }
    return steps;
}

// The clear channel of the sensor selects the factor between the raw value and lux.
std::vector<Step> ClearChannel() {
    std::vector<Step> steps;
    Repeat(steps, At(838, kGrey128, 20000.0f, kRecalculateMs), 8);
    for (const float raw : {600.0f, 9000.0f, 20000.0f, 90000.0f}) {
        for (const float c : {0.0f,   1.0f,   10.0f,  34.0f,  35.0f,  36.0f,   60.0f,
                              97.0f,  98.0f,  99.0f,  120.0f, 149.0f, 150.0f,  151.0f,
                              170.0f, 179.0f, 180.0f, 181.0f, 250.0f, 1000.0f, 2147483648.0f}) {
            Step step = At(838, kGrey128, raw, kRecalculateMs);
            step.c = c;
            Repeat(steps, step, 2);
        }
    }
    return steps;
}

// Disabling and enabling the sensor, as the framework does when the screen turns off and on.
std::vector<Step> Reenable() {
    std::vector<Step> steps;
    Repeat(steps, At(287, kGrey128, 30.0f, kRecalculateMs), 8);
    Step disable = At(0, kBlack, 30.0f);
    disable.enable = 0;
    steps.push_back(disable);
    // Woken up by the fingerprint sensor in daylight.
    Step flash = At(8, kWhite, 60000.0f, 60000);
    flash.enable = 1;
    flash.finger = true;
    flash.alpha = 234;
    steps.push_back(flash);
    flash.enable = -1;
    flash.dt_ms = 200;
    Repeat(steps, flash, 4);
    Repeat(steps, At(287, kGrey128, 30000.0f), 8);
    steps.push_back(disable);
    Step enable = At(1561, kGrey128, 30000.0f, 60000);
    enable.enable = 1;
    steps.push_back(enable);
    Repeat(steps, At(1561, kGrey128, 30000.0f), 8);
    return steps;
}

std::vector<Step> NoCapture() {
    std::vector<Step> steps;
    Step step = At(838, kWhite, 6000.0f, kRecalculateMs);
    step.no_capture = true;
    Repeat(steps, step, 6);
    Repeat(steps, At(838, kWhite, 6000.0f, kRecalculateMs), 4);
    Repeat(steps, step, 4);
    return steps;
}

Scenario WithArgs(const std::string& name, const std::function<void(FusionLightArgs&)>& change,
                  std::vector<Step> steps) {
    Scenario scenario{name, {}, std::move(steps)};
    scenario.options.args = FusionLightArgs();
    if (HasProfile()) {
        FusionLightArgs args = ProfileArgs();
        change(args);
        scenario.options.args = args;
    }
    return scenario;
}

std::vector<Step> Mixed() {
    std::vector<Step> steps = ColorsAtLevels({19, 287, 1561});
    const auto ambient = AmbientSteps(838, kGrey192);
    steps.insert(steps.end(), ambient.begin(), ambient.end());
    return steps;
}

std::vector<Scenario> Scenarios() {
    std::vector<Scenario> scenarios;
    RigOptions plain;
    plain.remap = false;
    plain.load = false;
    RigOptions no_load;
    no_load.load = false;

    scenarios.push_back(
            {"ColorsAtLevels", {}, ColorsAtLevels({4, 8, 19, 67, 287, 838, 1561, 2047, 2400})});
    scenarios.push_back({"ColorsAtLevelsWithoutRemap", plain,
                         ColorsAtLevels({4, 100, 259, 260, 261, 838, 1364, 2047, 2400})});
    scenarios.push_back({"PanelLoad", {}, PanelLoad()});
    scenarios.push_back({"PanelLoadDisabled", no_load, PanelLoad()});
    scenarios.push_back({"AmbientStepsDark", {}, AmbientSteps(838, kBlack)});
    scenarios.push_back({"AmbientStepsWhite", {}, AmbientSteps(1561, kWhite)});
    scenarios.push_back({"ContentChanges", {}, ContentChanges()});
    scenarios.push_back({"ScreenOffAndOn", {}, ScreenOffAndOn()});
    scenarios.push_back({"FingerprintFlash", {}, FingerprintFlash()});
    scenarios.push_back({"FingerprintDimLayer", {}, FingerprintDimLayer()});
    scenarios.push_back({"DcDimming", {}, DcDimming()});
    scenarios.push_back({"BrightnessRamp", {}, BrightnessRamp()});
    scenarios.push_back({"EventTiming", {}, EventTiming()});
    scenarios.push_back({"LowLight", {}, LowLight()});
    scenarios.push_back({"ClearChannel", {}, ClearChannel()});
    scenarios.push_back({"Reenable", {}, Reenable()});
    scenarios.push_back({"NoCapture", {}, NoCapture()});

    // Variants of the profile, for the parts of the correction the stk32600 profile does not use.
    for (int32_t type = 0; type <= 12; ++type) {
        scenarios.push_back(WithArgs(
                "PwmFunction" + std::to_string(type),
                [type](FusionLightArgs& args) { args.brightness_curve.type = type; },
                ColorsAtLevels({8, 150, 287, 1561, 2047})));
        scenarios.push_back(WithArgs(
                "AlphaFunction" + std::to_string(type),
                [type](FusionLightArgs& args) { args.fingerprint_dim.type = type; },
                FingerprintDimLayer()));
        scenarios.push_back(WithArgs(
                "DcFunction" + std::to_string(type),
                [type](FusionLightArgs& args) { args.dc_dim.type = type; }, DcDimming()));
    }
    for (int32_t count = 0; count <= 6; ++count) {
        scenarios.push_back(WithArgs(
                "SeparateLux" + std::to_string(count),
                [count](FusionLightArgs& args) { args.lux_factor_segments = count; },
                ClearChannel()));
    }
    for (int32_t type = 0; type <= 2; ++type) {
        scenarios.push_back(WithArgs(
                "RetType" + std::to_string(type),
                [type](FusionLightArgs& args) { args.clear_measure = type; }, ClearChannel()));
    }
    scenarios.push_back(WithArgs(
            "HighLightReport", [](FusionLightArgs& args) { args.report_bright_light = 1; },
            Mixed()));
    scenarios.push_back(WithArgs(
            "LowLightReport", [](FusionLightArgs& args) { args.report_low_light = 1; }, Mixed()));
    scenarios.push_back(WithArgs(
            "LowLightReportLowLight", [](FusionLightArgs& args) { args.report_low_light = 1; },
            LowLight()));
    scenarios.push_back(WithArgs(
            "SmoothQueue", [](FusionLightArgs& args) { args.use_moving_average = 1; }, Mixed()));
    scenarios.push_back(WithArgs(
            "MedianFilter", [](FusionLightArgs& args) { args.use_median_filter = 1; }, Mixed()));
    scenarios.push_back(WithArgs(
            "MedianFilterBelowLevel",
            [](FusionLightArgs& args) {
                args.use_median_filter = 1;
                args.median_filter_max_lux = 500;
            },
            Mixed()));
    scenarios.push_back(WithArgs(
            "ApolloLight", [](FusionLightArgs& args) { args.substitute_max_level = 1; }, Mixed()));
    scenarios.push_back(WithArgs(
            "SpecialRouCoe", [](FusionLightArgs& args) { args.lux_per_raw_override = 0.2f; },
            Mixed()));
    scenarios.push_back(WithArgs(
            "NineLevels", [](FusionLightArgs& args) { args.lux_range_count = 9; }, Mixed()));
    scenarios.push_back(WithArgs(
            "PwmLimits",
            [](FusionLightArgs& args) {
                args.brightness_curve_color_limits = {40.0f, 60.0f, 80.0f};
            },
            ColorsAtLevels({8, 287, 2047})));
    // The defaults built into the correction, without any profile.
    Scenario defaults{"DefaultArgs", plain, Mixed()};
    defaults.options.args = [] {
        FusionLightArgs args;
        DeriveLimits(args);
        return args;
    }();
    scenarios.push_back(defaults);
    return scenarios;
}

class FusionLightGoldenTest : public testing::TestWithParam<Scenario> {};

TEST_P(FusionLightGoldenTest, MatchesReference) {
    SKIP_WITHOUT_PROFILE();
    const Scenario& scenario = GetParam();
    Rig rig(scenario.options);
    std::vector<Record> records;
    records.reserve(scenario.steps.size());
    for (const auto& step : scenario.steps) {
        records.push_back(rig.step(step));
    }

    if (getenv("FUSIONLIGHT_RECORD") != nullptr) {
        printf("{\"%s\", {\n", scenario.name.c_str());
        for (const auto& record : records) {
            printf("{%d, %s, %s, %s, %d, %d},\n", record.reported, Literal(record.lux).c_str(),
                   Literal(record.comp).c_str(), Literal(record.level).c_str(), record.capturing,
                   record.whole_screen);
        }
        printf("}},\n");
        return;
    }

    const auto golden = kGoldens.find(scenario.name);
    ASSERT_NE(golden, kGoldens.end()) << "No reference values recorded";
    ASSERT_EQ(golden->second.size(), records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& expected = golden->second[i];
        const Record& actual = records[i];
        const Step& step = scenario.steps[i];
        SCOPED_TRACE(testing::Message()
                     << "step " << i << ": dt=" << step.dt_ms << "ms raw=" << step.raw
                     << " c=" << step.c << " level=" << step.level << " RGB(" << step.red << ", "
                     << step.green << ", " << step.blue << ") load=" << step.load << " alpha="
                     << step.alpha << " dc_alpha=" << step.dc_alpha << " finger=" << step.finger);
        EXPECT_EQ(expected.reported, actual.reported);
        ExpectClose(expected.lux, actual.lux, "lux");
        ExpectClose(expected.comp, actual.comp, "compensated light");
        ExpectClose(expected.level, actual.level, "brightness level");
        EXPECT_EQ(expected.capturing, actual.capturing);
        EXPECT_EQ(expected.whole_screen, actual.whole_screen);
    }
}

INSTANTIATE_TEST_SUITE_P(Scenarios, FusionLightGoldenTest, testing::ValuesIn(Scenarios()),
                         [](const testing::TestParamInfo<Scenario>& info) {
                             return info.param.name;
                         });

}  // anonymous namespace
}  // namespace test
}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
