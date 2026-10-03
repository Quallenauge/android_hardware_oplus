/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {

// Hardware sensor type of qti.sensor.wise_light, the RGBW ALS behind the panel.
constexpr int32_t kWiseLightSensorType = 33171103;

// Light one color channel of the panel leaks into the sensor: a cubic polynomial of the code
// value the screen shows, scaled from the panel the profile was made with to this one.
struct ChannelArgs {
    // Raw value of the sensor with the channel at full brightness, from the factory calibration.
    float max;
    // The same for the panel the profile was made with.
    float profile_max;
    // Coefficients for the cube, the square and the code value itself.
    std::array<float, 3> coefficients;
    float offset;
};

// The lux scale is divided into ranges. Each has the brightness level the screen typically has
// in such light and a tolerance window: how far the raw value may move away from the light
// that was compensated before the correction calculates again.
struct LuxRanges {
    // Window in raw values, derived from the one in lux.
    std::vector<float> window_upper;
    std::vector<float> window_lower;
    // Raw value of a white screen at the level of the range.
    std::vector<float> raw_at_level;
    // Window in lux.
    std::vector<float> window_upper_lux;
    std::vector<float> window_lower_lux;
    // Brightness level of the range.
    std::vector<int32_t> level;
    // Highest lux of the range.
    std::vector<int32_t> lux_limit;
};

// A function of the brightness level, of which a profile selects one of several forms by its
// type. The points split the level range into segments with parameters of their own.
struct FunctionArgs {
    int32_t type;
    int32_t segment_count;
    std::array<float, 3> points;
    std::array<float, 6> params;
};

// Parameters of the correction. A fusionlight profile and the factory calibration of the light
// sensor replace these defaults.
struct FusionLightArgs {
    ChannelArgs r{646.0f, 646.0f, {1.97e-05f, 0.00295f, 0.5063f}, -3.5831201f};
    ChannelArgs g{622.0f, 622.0f, {2.08e-05f, 0.00341f, 0.20182f}, -0.60028f};
    ChannelArgs b{367.0f, 367.0f, {9.1e-06f, 0.00339f, -0.01412f}, 0.91132f};
    // What the three channels together leak less than their sum, by the grey value of the color.
    ChannelArgs w{1352.0f, 283.0f, {2.68e-05f, -0.00337f, 0.21704f}, -2.4149499f};
    // Weights of red, green and blue in that grey value.
    std::array<float, 3> grey_weights{0.42f, 0.36f, 0.22f};

    // Raw value of a white screen per brightness level.
    float raw_per_level = 0.91f;
    // Lux per raw value. The first one applies to dim light, the others are selected by the
    // clear channel for some profiles.
    std::array<float, 4> lux_per_raw{0.54f, 0.282f, 0.14f, 0.09f};
    // Replaces the first factor for some profiles, if positive.
    float lux_per_raw_override = -1.0f;
    // Factory calibration of the sensor in thousandths.
    float calibration_factor = 900.0f;

    // Number of lux ranges in use, 9 or 15.
    int32_t lux_range_count = 9;
    LuxRanges lux_ranges_9{
            {7.41f, 22.22f, 55.56f, 92.59f, 1296.3f, 2962.96f, 5444.44f, 10925.93f, 148148.16f},
            {0.0f, 1.85f, 9.26f, 18.52f, 46.3f, 555.56f, 1851.85f, 3703.7f, 7407.41f},
            {9.1f, 43.68f, 95.55f, 206.57f, 326.69f, 395.85f, 510.51f, 631.54f, 930.93f},
            {9.0f, 17.0f, 30.0f, 50.0f, 700.0f, 1600.0f, 2940.0f, 5900.0f, 80000.0f},
            {0.0f, 5.0f, 10.0f, 18.0f, 30.0f, 300.0f, 1000.0f, 2000.0f, 4000.0f},
            {10, 48, 75, 141, 234, 367, 571, 712, 1023},
            {0, 8, 18, 30, 360, 1200, 2250, 4600, 10000},
    };
    LuxRanges lux_ranges_15{
            {6.0f, 13.0f, 30.0f, 50.0f, 700.0f, 1600.0f, 2940.0f, 5900.0f, 10000.0f, 20100.0f,
             30100.0f, 40100.0f, 60100.0f, 100100.0f, 500000.0f},
            {0.0f, 3.0f, 5.0f, 10.0f, 25.0f, 300.0f, 1000.0f, 2000.0f, 4000.0f, 9000.0f, 20000.0f,
             30000.0f, 40000.0f, 60000.0f, 100000.0f},
            {7.0f, 38.0f, 60.0f, 113.0f, 194.0f, 308.0f, 475.0f, 589.0f, 842.0f, 1023.0f, 1086.0f,
             1135.0f, 1255.0f, 1337.0f, 2047.0f},
            {6.0f, 13.0f, 30.0f, 50.0f, 700.0f, 1600.0f, 2940.0f, 5900.0f, 12000.0f, 20100.0f,
             30100.0f, 40100.0f, 60100.0f, 100100.0f, 500000.0f},
            {0.0f, 3.0f, 5.0f, 10.0f, 25.0f, 300.0f, 1000.0f, 2000.0f, 4000.0f, 9000.0f, 20000.0f,
             30000.0f, 40000.0f, 60000.0f, 100000.0f},
            {7, 38, 60, 113, 194, 308, 475, 589, 842, 1023, 1086, 1135, 1255, 1337, 2047},
            {0, 9, 16, 35, 360, 1200, 2250, 4600, 9999, 20000, 30000, 40000, 60000, 100000, 150000},
    };

    // For brighter light the factor between the raw value and lux depends on the clear channel
    // of the sensor: a measure derived from it selects one of several lines.
    // How the measure is derived from the clear channel.
    int32_t clear_measure = 0;
    // Number of lines, which also selects between forms of the function.
    int32_t lux_factor_segments = 5;
    // Measures up to which each line applies.
    std::array<float, 4> clear_thresholds{30.0f, 60.0f, 90.0f, 120.0f};
    // Slope and offset of each line.
    std::array<std::array<float, 2>, 5> lux_factor_lines{{
            {0.0f, 0.54f},
            {0.0f, 0.54f},
            {0.0f, 0.54f},
            {0.0f, 0.54f},
            {0.0f, 0.54f},
    }};
    // Raw value up to which the first factor is used regardless of the clear channel.
    int32_t lux_factor_min_raw = 1000;

    // Brightness behind the dim layer of the fingerprint sensor.
    FunctionArgs fingerprint_dim{0, 1, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    // Brightness with DC dimming.
    FunctionArgs dc_dim{0, 1, {0.0f, 0.0f, 259.0f}, {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    // Light of the panel depending on the brightness level.
    FunctionArgs brightness_curve{0, 0, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    // Code values of red, green and blue below which one form of the curve compensates nothing.
    std::array<float, 3> brightness_curve_color_limits{0.0f, 0.0f, 0.0f};

    // Report the scaled raw value without a capture in very bright or very dim light.
    int32_t report_bright_light = 0;
    int32_t report_low_light = 0;
    // Average the last six reported values.
    int32_t use_moving_average = 0;
    // Highest brightness level without the high brightness mode.
    int32_t max_level = 1023;
    // Brightness level below which DC dimming applies.
    float dc_dim_max_level = 260.0f;
    // Report another value in place of the highest brightness level.
    int32_t substitute_max_level = 0;
    int32_t max_level_substitute = 2047;
    // Lux below which 0 is reported, normally and for the low light report.
    float min_lux = 3.0f;
    float min_lux_low_light = 1.0f;
    // Take the brightness level from the sensor event instead of the panel.
    int32_t level_from_event = 0;
    // Use the override as the first lux factor and no calibration factor.
    int32_t fixed_lux_factors = 0;
    // Report the second smallest of the last six values, optionally only below a lux.
    int32_t use_median_filter = 0;
    int32_t median_filter_max_lux = -1;

    // Events closer to the previous one than this are dropped.
    int32_t min_event_gap_ms = 100;
    // The correction calculates anew after this time at the latest.
    int32_t recalculation_period_ms = 5000;

    const LuxRanges& ranges(int32_t count) const {
        return count == 15 ? lux_ranges_15 : lux_ranges_9;
    }
};

// The steps of LoadFusionLightArgs: reads a fusionlight profile, keeping the current values if
// it is missing, applies a factory calibration as reported by ISensorFeature, and derives the
// raw lux limits.
void LoadFusionLightProfile(const std::string& path, FusionLightArgs& result);
void ApplyCalibration(const std::string& response, FusionLightArgs& result);
void DeriveLimits(FusionLightArgs& result);

// Loads the fusionlight profile matching the light sensor, applies the factory calibration
// reported by ISensorFeature and derives the raw lux limits.
FusionLightArgs LoadFusionLightArgs(const std::string& sensor_name);

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
