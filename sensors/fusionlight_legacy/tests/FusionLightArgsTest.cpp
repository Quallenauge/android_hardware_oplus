/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include <android-base/file.h>
#include <gtest/gtest.h>

#include "FusionLight.h"
#include "FusionLightArgs.h"
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

// A profile with made up values, in the structure of the real ones.
constexpr auto kProfile = R"(<?xml version='1.0' encoding='utf-8'?>
<Attributes level="1">
    <Project level="2" index="1">
        <ProjectCode type="string">12345</ProjectCode>
        <LevelMax type="int">15</LevelMax>
    </Project>
    <Args level="2" index="2">
        <R><a>101</a><b>102</b><c>0.103</c><d>0.104</d><e>0.105</e><f>0.106</f></R>
        <G><a>201</a><b>202</b><c>0.203</c><d>0.204</d><e>0.205</e><f>0.206</f></G>
        <B><a>301</a><b>302</b><c>0.303</c><d>0.304</d><e>0.305</e><f>0.306</f></B>
        <W><a>401</a><b>402</b><c>0.403</c><d>0.404</d><e>0.405</e><f>0.406</f></W>
        <Gray><a>0.5</a><b>0.3</b><c>0.2</c></Gray>
        <Cal><a>0.6</a><b>0.21</b><c>0.22</c><d>0.23</d><e>0.24</e><f>950</f></Cal>
        <RawArrays>
            <One><v>1</v><v>2</v><v>3</v><v>4</v><v>5</v><v>6</v><v>7</v><v>8</v><v>9</v><v>10</v><v>11</v><v>12</v><v>13</v><v>14</v><v>15</v></One>
            <Two><v>21</v><v>22</v></Two>
            <Three><v>31</v></Three>
            <Four><v>41</v><v>42</v><v>43</v></Four>
            <Five><v>51</v></Five>
        </RawArrays>
        <BrightLevel>
            <Values><v>61</v><v>62</v><v>63</v></Values>
            <Limits><v>71</v><v>72</v></Limits>
        </BrightLevel>
        <PWMFunction>
            <PWMFunctionType>7</PWMFunctionType>
            <PWMParagraphCount>2</PWMParagraphCount>
            <PWMSeperatePoint1>11</PWMSeperatePoint1>
            <PWMSeperatePoint2>12</PWMSeperatePoint2>
            <PWMSeperatePoint3>13</PWMSeperatePoint3>
            <PWMRlimit>14</PWMRlimit>
            <PWMGlimit>15</PWMGlimit>
            <PWMBlimit>16</PWMBlimit>
            <PWMParameter1>1.1</PWMParameter1>
            <PWMParameter2>1.2</PWMParameter2>
            <PWMParameter3>1.3</PWMParameter3>
            <PWMParameter4>1.4</PWMParameter4>
            <PWMParameter5>1.5</PWMParameter5>
            <PWMParameter6>1.6</PWMParameter6>
        </PWMFunction>
        <SpecialCustom>
            <a>1</a><b>0</b><c>7</c><d>8</d><e>1</e>
            <MaxLevel>2047</MaxLevel>
            <DCLevelMax>333</DCLevelMax>
            <LowLightAccuracy>2.5</LowLightAccuracy>
            <LowLightAccuracy1>0.5</LowLightAccuracy1>
            <MedianFilterQueue>1</MedianFilterQueue>
            <MedianFilterEnableLevel>77</MedianFilterEnableLevel>
        </SpecialCustom>
    </Args>
</Attributes>
)";

FusionLightArgs Parse(const std::string& content) {
    TemporaryFile file;
    EXPECT_TRUE(android::base::WriteStringToFile(content, file.path));
    FusionLightArgs args;
    LoadFusionLightProfile(file.path, args);
    return args;
}

TEST(FusionLightArgsTest, MissingProfileKeepsDefaults) {
    FusionLightArgs args;
    LoadFusionLightProfile("/does/not/exist.xml", args);
    const FusionLightArgs defaults;
    EXPECT_EQ(defaults.lux_range_count, args.lux_range_count);
    EXPECT_EQ(defaults.r.max, args.r.max);
    EXPECT_EQ(defaults.brightness_curve.type, args.brightness_curve.type);
    EXPECT_EQ(defaults.max_level, args.max_level);
}

TEST(FusionLightArgsTest, ProfileWithoutProjectKeepsDefaults) {
    const FusionLightArgs args = Parse("<Attributes><Args><R><a>5</a></R></Args></Attributes>");
    EXPECT_EQ(FusionLightArgs().r.max, args.r.max);
}

// Most values are read by their position, not by their name.
TEST(FusionLightArgsTest, ParsesProfile) {
    const FusionLightArgs args = Parse(kProfile);
    EXPECT_EQ(15, args.lux_range_count);

    EXPECT_FLOAT_EQ(101.0f, args.r.max);
    EXPECT_FLOAT_EQ(102.0f, args.r.profile_max);
    EXPECT_FLOAT_EQ(0.103f, args.r.coefficients[0]);
    EXPECT_FLOAT_EQ(0.104f, args.r.coefficients[1]);
    EXPECT_FLOAT_EQ(0.105f, args.r.coefficients[2]);
    EXPECT_FLOAT_EQ(0.106f, args.r.offset);
    EXPECT_FLOAT_EQ(201.0f, args.g.max);
    EXPECT_FLOAT_EQ(0.306f, args.b.offset);
    EXPECT_FLOAT_EQ(402.0f, args.w.profile_max);

    EXPECT_FLOAT_EQ(0.5f, args.grey_weights[0]);
    EXPECT_FLOAT_EQ(0.3f, args.grey_weights[1]);
    EXPECT_FLOAT_EQ(0.2f, args.grey_weights[2]);
    EXPECT_FLOAT_EQ(0.6f, args.raw_per_level);
    EXPECT_FLOAT_EQ(0.21f, args.lux_per_raw[0]);
    EXPECT_FLOAT_EQ(0.24f, args.lux_per_raw[3]);
    EXPECT_FLOAT_EQ(950.0f, args.calibration_factor);

    // The tables of the selected length are filled, as far as the profile has values.
    const FusionLightArgs defaults;
    const LuxRanges& tables = args.lux_ranges_15;
    EXPECT_FLOAT_EQ(1.0f, tables.window_upper[0]);
    EXPECT_FLOAT_EQ(15.0f, tables.window_upper[14]);
    EXPECT_FLOAT_EQ(22.0f, tables.window_lower[1]);
    EXPECT_FLOAT_EQ(defaults.lux_ranges_15.window_lower[2], tables.window_lower[2]);
    EXPECT_FLOAT_EQ(31.0f, tables.raw_at_level[0]);
    EXPECT_FLOAT_EQ(43.0f, tables.window_upper_lux[2]);
    EXPECT_FLOAT_EQ(51.0f, tables.window_lower_lux[0]);
    EXPECT_EQ(63, tables.level[2]);
    EXPECT_EQ(72, tables.lux_limit[1]);
    EXPECT_EQ(defaults.lux_ranges_9.window_upper, args.lux_ranges_9.window_upper);

    EXPECT_EQ(7, args.brightness_curve.type);
    EXPECT_EQ(2, args.brightness_curve.segment_count);
    EXPECT_FLOAT_EQ(11.0f, args.brightness_curve.points[0]);
    EXPECT_FLOAT_EQ(13.0f, args.brightness_curve.points[2]);
    EXPECT_FLOAT_EQ(14.0f, args.brightness_curve_color_limits[0]);
    EXPECT_FLOAT_EQ(16.0f, args.brightness_curve_color_limits[2]);
    EXPECT_FLOAT_EQ(1.1f, args.brightness_curve.params[0]);
    EXPECT_FLOAT_EQ(1.6f, args.brightness_curve.params[5]);
    // Groups the profile leaves out keep their defaults.
    EXPECT_EQ(defaults.fingerprint_dim.type, args.fingerprint_dim.type);
    EXPECT_EQ(defaults.lux_factor_segments, args.lux_factor_segments);

    EXPECT_EQ(1, args.report_bright_light);
    EXPECT_EQ(0, args.report_low_light);
    EXPECT_EQ(1, args.use_moving_average);
    EXPECT_EQ(2047, args.max_level);
    EXPECT_FLOAT_EQ(333.0f, args.dc_dim_max_level);
    EXPECT_FLOAT_EQ(2.5f, args.min_lux);
    EXPECT_FLOAT_EQ(0.5f, args.min_lux_low_light);
    EXPECT_EQ(1, args.use_median_filter);
    EXPECT_EQ(77, args.median_filter_max_lux);
    EXPECT_EQ(defaults.substitute_max_level, args.substitute_max_level);
}

TEST(FusionLightArgsTest, RejectsInvalidLevels) {
    std::string profile = kProfile;
    profile.replace(profile.find(">15<"), 4, ">12<");
    profile.replace(profile.find(">2047<"), 6, ">2000<");
    const FusionLightArgs args = Parse(profile);
    EXPECT_EQ(9, args.lux_range_count);
    EXPECT_EQ(FusionLightArgs().max_level, args.max_level);
    // The values now go to the tables of the other length.
    EXPECT_FLOAT_EQ(1.0f, args.lux_ranges_9.window_upper[0]);
}

TEST(FusionLightArgsTest, AppliesCalibration) {
    FusionLightArgs args;
    ApplyCalibration(kCalibration, args);
    EXPECT_FLOAT_EQ(1073.0f, args.r.max);
    EXPECT_FLOAT_EQ(1562.0f, args.g.max);
    EXPECT_FLOAT_EQ(1231.0f, args.b.max);
    EXPECT_FLOAT_EQ(3611.0f, args.w.max);
    EXPECT_FLOAT_EQ(0.11f, args.lux_per_raw[0]);
    EXPECT_FLOAT_EQ(1226.0f, args.calibration_factor);
    // The calibrated maxima of the profile stay.
    EXPECT_FLOAT_EQ(FusionLightArgs().r.profile_max, args.r.profile_max);
}

TEST(FusionLightArgsTest, PartialCalibration) {
    FusionLightArgs args;
    ApplyCalibration(R"({"G_MAX":"800","cali_para":"1000"})", args);
    const FusionLightArgs defaults;
    EXPECT_FLOAT_EQ(defaults.r.max, args.r.max);
    EXPECT_FLOAT_EQ(800.0f, args.g.max);
    EXPECT_FLOAT_EQ(defaults.lux_per_raw[0], args.lux_per_raw[0]);
    EXPECT_FLOAT_EQ(1000.0f, args.calibration_factor);
}

TEST(FusionLightArgsTest, IgnoresInvalidCalibration) {
    const FusionLightArgs defaults;
    for (const char* calibration : {"", "{", "nonsense"}) {
        FusionLightArgs args;
        ApplyCalibration(calibration, args);
        EXPECT_FLOAT_EQ(defaults.r.max, args.r.max) << calibration;
        EXPECT_FLOAT_EQ(defaults.calibration_factor, args.calibration_factor) << calibration;
    }
}

TEST(FusionLightArgsTest, CalibrationWithOwnFactors) {
    FusionLightArgs args;
    args.fixed_lux_factors = 1;
    args.lux_per_raw_override = 0.3f;
    ApplyCalibration(kCalibration, args);
    EXPECT_FLOAT_EQ(1073.0f, args.r.max);
    EXPECT_FLOAT_EQ(0.3f, args.lux_per_raw[0]);
    EXPECT_FLOAT_EQ(1000.0f, args.calibration_factor);
}

TEST(FusionLightArgsTest, DerivesLimits) {
    for (const int32_t length : {9, 15}) {
        FusionLightArgs args;
        args.lux_range_count = length;
        ApplyCalibration(kCalibration, args);
        const FusionLightArgs before = args;
        DeriveLimits(args);

        EXPECT_FLOAT_EQ(3611.0f / 1023.0f, args.raw_per_level);
        const LuxRanges& tables = args.ranges(length);
        const LuxRanges& base = before.ranges(length);
        ASSERT_EQ(static_cast<size_t>(length), tables.window_upper.size());
        for (int32_t i = 0; i < length; ++i) {
            EXPECT_FLOAT_EQ(base.window_upper_lux[i] * 1000.0f / (1226.0f * 0.11f),
                            tables.window_upper[i]);
            EXPECT_FLOAT_EQ(base.window_lower_lux[i] * 1000.0f / (1226.0f * 0.11f),
                            tables.window_lower[i]);
            EXPECT_FLOAT_EQ(3611.0f / 1023.0f * base.level[i], tables.raw_at_level[i]);
        }
        // The tables of the other length are left alone.
        const int32_t other = length == 9 ? 15 : 9;
        EXPECT_EQ(before.ranges(other).window_upper, args.ranges(other).window_upper);
    }
}

TEST(FusionLightArgsTest, InstalledProfile) {
    SKIP_WITHOUT_PROFILE();
    const FusionLightArgs args = ProfileArgs();
    EXPECT_EQ(15, args.lux_range_count);
    EXPECT_EQ(2047, args.max_level);
    EXPECT_EQ(7, args.brightness_curve.type);
    EXPECT_EQ(9, args.fingerprint_dim.type);
    EXPECT_EQ(5, args.dc_dim.type);
    EXPECT_EQ(5, args.lux_factor_segments);
    EXPECT_FLOAT_EQ(1073.0f, args.r.max);
    EXPECT_FLOAT_EQ(3611.0f, args.w.max);
    EXPECT_FLOAT_EQ(1226.0f, args.calibration_factor);
    EXPECT_FLOAT_EQ(0.11f, args.lux_per_raw[0]);
}

TEST(BrightnessRemapTest, Parses) {
    const auto table = ParseBrightnessRemap("4:4, 61:5 ,100:8");
    ASSERT_EQ(3u, table.size());
    EXPECT_EQ(4, table[0].brightness);
    EXPECT_EQ(4, table[0].level);
    EXPECT_EQ(61, table[1].brightness);
    EXPECT_EQ(5, table[1].level);
    EXPECT_EQ(100, table[2].brightness);
    EXPECT_EQ(8, table[2].level);

    EXPECT_EQ(47u, ParseBrightnessRemap(kBrightnessRemap).size());
}

TEST(BrightnessRemapTest, RejectsInvalidTables) {
    EXPECT_TRUE(ParseBrightnessRemap("").empty());
    EXPECT_TRUE(ParseBrightnessRemap("4:4,x:5").empty());
    EXPECT_TRUE(ParseBrightnessRemap("4:4,61").empty());
    EXPECT_TRUE(ParseBrightnessRemap("4:4,61:5:6").empty());
    // Both columns have to increase.
    EXPECT_TRUE(ParseBrightnessRemap("4:4,3:5").empty());
    EXPECT_TRUE(ParseBrightnessRemap("4:4,61:4").empty());
}

TEST(PanelLoadTest, Parses) {
    const auto load = ParsePanelLoad(kPanelLoad);
    ASSERT_TRUE(load.has_value());
    EXPECT_FLOAT_EQ(0.218f, load->drop);
    EXPECT_FLOAT_EQ(0.82f, load->exponent);
    EXPECT_FLOAT_EQ(0.41f, load->weights.uniform);
    EXPECT_FLOAT_EQ(0.256f, load->weights.red);
    EXPECT_FLOAT_EQ(0.232f, load->weights.green);
    EXPECT_FLOAT_EQ(0.528f, load->weights.blue);
    EXPECT_FLOAT_EQ(0.963f, load->scale);
}

TEST(PanelLoadTest, RejectsInvalidValues) {
    EXPECT_FALSE(ParsePanelLoad("").has_value());
    EXPECT_FALSE(ParsePanelLoad("0.2,0.8,0.4,0.2,0.2,0.5").has_value());
    EXPECT_FALSE(ParsePanelLoad("0.2,0.8,0.4,0.2,0.2,0.5,1,1").has_value());
    EXPECT_FALSE(ParsePanelLoad("0.2,0.8,0.4,x,0.2,0.5,1").has_value());
    EXPECT_FALSE(ParsePanelLoad("0.2,0.8,0.4,-0.2,0.2,0.5,1").has_value());
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
