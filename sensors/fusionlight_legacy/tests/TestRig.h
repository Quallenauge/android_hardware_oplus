/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "FusionLight.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace test {

constexpr auto kSensorName = "stk32600 wise Light Sensor Non-wakeup";
constexpr auto kProfilePath = "/odm/etc/fusionlight_profile/oplus_fusion_light_args_2.xml";

// Factory calibration of the phone the reference values were recorded on.
constexpr auto kCalibration =
        R"({"B_MAX":"1231","G_MAX":"1562","R_MAX":"1073","W_MAX":"3611","cali_lux":"815",)"
        R"("cali_para":"1226","cali_para_extra":"1226.49426270","cali_para_ver":"2",)"
        R"("row_coe":"110"})";

// Values of the lunaa device tree.
constexpr auto kBrightnessRemap =
        "4:4,61:5,100:8,122:10,244:19,431:67,550:114,645:161,725:209,797:256,860:304,919:351,"
        "974:399,1025:446,1073:494,1120:541,1163:589,1245:684,1283:732,1321:779,1357:827,"
        "1391:874,1425:922,1458:969,1491:1017,1522:1064,1582:1159,1611:1207,1640:1254,1668:1302,"
        "1695:1349,1722:1397,1748:1445,1774:1492,1800:1540,1825:1587,1850:1635,1874:1682,"
        "1898:1730,1921:1777,1944:1825,1967:1872,1990:1920,2012:1967,2034:2015,2047:2047,"
        "2784:2784";
constexpr auto kPanelLoad = "0.218,0.82,0.41,0.256,0.232,0.528,0.963";

// Whether the profile the reference values depend on is installed.
inline bool HasProfile() {
    FusionLightArgs args;
    LoadFusionLightProfile(kProfilePath, args);
    return args.brightness_curve.type == 7 && args.lux_range_count == 15;
}

inline FusionLightArgs ProfileArgs() {
    FusionLightArgs args;
    LoadFusionLightProfile(kProfilePath, args);
    ApplyCalibration(kCalibration, args);
    DeriveLimits(args);
    return args;
}

#define SKIP_WITHOUT_PROFILE()                                                        \
    if (!HasProfile()) {                                                              \
        GTEST_SKIP() << "Needs the stk32600 fusionlight profile at " << kProfilePath; \
    }

struct FakePanelState final : public PanelState {
    int32_t level = 0;
    int32_t alpha = 0;
    int32_t dc_alpha = 0;
    bool finger = false;

    int32_t brightness() override { return level; }
    int32_t dimAlpha() override { return alpha; }
    int32_t dimDcAlpha() override { return dc_alpha; }
    bool fingerprintPressed() override { return finger; }
};

struct FakeScreenSampler final : public ScreenSampler {
    FakeScreenSampler()
        : ScreenSampler(ScreenRegion{0, 0, 3, 3}, std::chrono::milliseconds(50), std::nullopt) {}

    // What a capture would see. Like the real sampler the load is only known for a capture of
    // the whole screen.
    std::optional<ScreenColor> color;
    std::optional<float> load;

    bool started = false;
    bool capturing = false;
    bool whole_screen = false;
    int32_t samples = 0;

    void start() override { started = true; }
    void stop() override { started = false; }
    void setCapturing(bool value) override { capturing = value; }
    void setWholeScreen(bool value) override { whole_screen = value; }
    std::optional<ScreenColor> latest() override {
        ++samples;
        if (!color.has_value()) {
            return std::nullopt;
        }
        ScreenColor result = *color;
        result.load = whole_screen ? load : std::nullopt;
        return result;
    }
};

// One sensor event and the state of the phone it arrives in.
struct Step {
    int32_t dt_ms = 200;
    float raw = 0.0f;
    float c = 5.0f;
    // Level applied to the panel.
    int32_t level = 0;
    int32_t red = 0;
    int32_t green = 0;
    int32_t blue = 0;
    // Load of the whole screen, negative if it shows the color above the sensor everywhere.
    float load = -1.0f;
    int32_t alpha = 0;
    int32_t dc_alpha = 0;
    bool finger = false;
    float period = 20.0f;
    // Disables (0) or enables (1) the sensor before the event.
    int32_t enable = -1;
    // No screen capture is available.
    bool no_capture = false;
};

struct Record {
    bool reported;
    float lux;
    float comp;
    float level;
    bool capturing;
    bool whole_screen;
};

struct RigOptions {
    bool remap = true;
    bool load = true;
    std::optional<FusionLightArgs> args;
};

class Rig {
  public:
    explicit Rig(const RigOptions& options = {}) {
        auto panel = std::make_unique<FakePanelState>();
        auto sampler = std::make_unique<FakeScreenSampler>();
        panel_ = panel.get();
        sampler_ = sampler.get();

        FusionLightEnvironment environment;
        environment.panel = std::move(panel);
        environment.sampler = std::move(sampler);
        if (options.remap) {
            environment.brightness_remap = ParseBrightnessRemap(kBrightnessRemap);
        }
        if (options.load) {
            environment.panel_load = ParsePanelLoad(kPanelLoad);
            weights_ = environment.panel_load->weights;
        }
        const FusionLightArgs args = options.args.has_value() ? *options.args : ProfileArgs();
        environment.load_args = [args](const std::string&) { return args; };
        environment.now_ms = [this] { return now_ms_; };
        fusion_light_.initialize(kSensorName, std::move(environment));
        fusion_light_.setEnabled(true);
    }

    Record step(const Step& step) {
        if (step.enable >= 0) {
            fusion_light_.setEnabled(step.enable != 0);
        }
        now_ms_ += step.dt_ms;
        panel_->level = step.level;
        panel_->alpha = step.alpha;
        panel_->dc_alpha = step.dc_alpha;
        panel_->finger = step.finger;
        if (step.no_capture) {
            sampler_->color = std::nullopt;
        } else {
            const ScreenColor color{step.red, step.green, step.blue, std::nullopt};
            sampler_->color = color;
            sampler_->load = step.load >= 0.0f      ? step.load
                             : weights_.has_value() ? weights_->of(color)
                                                    : 0.0f;
        }

        Event event{};
        event.timestamp = now_ms_ * 1'000'000;
        event.u.data[0] = step.raw;
        event.u.data[1] = step.period;
        event.u.data[2] = step.c;
        const bool reported = fusion_light_.process(event);
        return Record{reported,
                      reported ? event.u.data[0] : 0.0f,
                      reported ? event.u.data[3] : 0.0f,
                      reported ? event.u.data[9] : 0.0f,
                      sampler_->capturing,
                      sampler_->whole_screen};
    }

    FakePanelState& panel() { return *panel_; }
    FakeScreenSampler& sampler() { return *sampler_; }
    FusionLight& fusionLight() { return fusion_light_; }

  private:
    FusionLight fusion_light_;
    FakePanelState* panel_;
    FakeScreenSampler* sampler_;
    std::optional<ScreenLoadWeights> weights_;
    // Far from 0, which the correction takes as "never".
    int64_t now_ms_ = 1'000'000;
};

inline float Tolerance(float value) {
    return std::max(1e-3f, std::abs(value) * 1e-4f);
}

}  // namespace test
}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
