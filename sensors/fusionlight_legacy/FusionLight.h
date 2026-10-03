/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <android-base/macros.h>
#include <android/hardware/sensors/1.0/types.h>

#include "FusionLightArgs.h"
#include "FusionLightMath.h"
#include "PanelState.h"
#include "ScreenSampler.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {

using ::android::hardware::sensors::V1_0::Event;

std::vector<BrightnessRemapPoint> ParseBrightnessRemap(const std::string& value);
std::optional<PanelLoadArgs> ParsePanelLoad(const std::string& value);

// What the correction reads from its surroundings.
struct FusionLightEnvironment {
    std::unique_ptr<PanelState> panel;
    std::unique_ptr<ScreenSampler> sampler;
    std::vector<BrightnessRemapPoint> brightness_remap;
    std::optional<PanelLoadArgs> panel_load;
    std::function<FusionLightArgs(const std::string& sensor_name)> load_args;
    std::function<int64_t()> now_ms;
};

// Corrects the light sensor below the panel, which also sees the light of the panel itself.
//
// The light the panel leaks into the sensor is estimated from the color the screen shows above
// the sensor and from the brightness of the panel, and subtracted from the raw value of the
// sensor. What remains is converted to lux.
//
// Capturing the screen costs power, so the estimate is not renewed for every event. After a
// calculation the raw value is expected to stay within a tolerance window around what was
// compensated. While it does, the previous lux is reported. Once it leaves the window, and every
// few seconds anyway, a capture is requested and the event after it calculates again.
class FusionLight final {
  public:
    FusionLight() = default;

    DISALLOW_COPY_AND_ASSIGN(FusionLight);

    // Sets the correction up from the properties of the device. Returns whether it can run, it
    // needs the screen region above the sensor.
    bool initialize(const std::string& sensor_name);
    void initialize(const std::string& sensor_name, FusionLightEnvironment environment);
    // Follows the sensor being activated. Loads the profile and starts capturing when enabled.
    void setEnabled(bool enabled);

    // Corrects a wise light event in place, returns whether it should be reported.
    bool process(Event& event);

  private:
    // Reports an uncorrected lux while the screen is off, drops events that follow the previous
    // one too closely and marks a recalculation as due every few seconds.
    bool acceptEvent(Event& event, int32_t level);
    // Decides between reporting the previous lux, requesting a capture and calculating anew.
    bool updateLux(Event& event, int32_t level);
    // Starts capturing the screen for the next event and reports the previous lux meanwhile.
    // Returns whether the event should be reported.
    bool requestCapture(Event& event, float raw_lux, int32_t level, bool forced);
    // Reports the previous lux for a raw value inside the tolerance window.
    bool reportUnchanged(Event& event, float raw_lux, int32_t level);
    // Tracks the screen turning off and on, which resets what is held back.
    void updateScreenState(int32_t level);
    // Color the screen shows above the sensor according to the latest capture.
    ScreenColor screenColor();

    // Subtracts the light of the panel from the raw value of the event and returns the lux of
    // what remains. Fills the event with the intermediate values.
    float calculateLux(Event& event, int32_t level, int32_t max_level, const ScreenColor& color);
    // Brightness behind the dim layer of the fingerprint sensor. Notes when the layer disappears.
    float levelBehindDimLayer(float level, int32_t max_level);
    // Brightness with DC dimming, which dims by the image instead of by the backlight.
    float levelWithDcDimming(float level);
    // Sets the tolerance window of the raw value around the light just compensated.
    void updateToleranceWindow();
    // Range of the tables a lux falls into.
    int32_t findLuxRange(float lux);

    // The ways to report without calculating: the previous lux, the raw value scaled for very
    // dark or very bright surroundings, zero, and the raw value of a screen that is off.
    void reportPreviousLux(float raw_lux, Event& event, int32_t level);
    void reportLowLight(Event& event, float raw_lux, int32_t level);
    void reportBrightLight(Event& event, float raw_lux, int32_t level);
    void reportDarkness(Event& event, float raw_lux, int32_t level);
    void reportWithScreenOff(Event& event, float raw_lux, int32_t level);
    // Applies the substitute for the highest brightness level some profiles ask for.
    void finishReport(Event& event);
    void stopCapture();
    // Optional filters over the last six reported values.
    float movingAverage(Event& event);
    void updateTrend();
    float medianFilter(Event& event);

    std::mutex mutex_;
    std::string sensor_name_;
    FusionLightArgs args_;
    std::unique_ptr<PanelState> panel_;
    std::function<FusionLightArgs(const std::string& sensor_name)> load_args_;
    std::function<int64_t()> now_ms_;
    std::vector<BrightnessRemapPoint> brightness_remap_;
    std::optional<PanelLoadArgs> panel_load_;
    int32_t panel_level_ = 0;
    std::unique_ptr<ScreenSampler> sampler_;
    bool enabled_ = false;
    bool debug_ = false;

    // Whether a lux was determined since the sensor was enabled.
    bool lux_valid_ = false;
    // Whether the event at hand reported the previous lux instead of a new one.
    bool reported_previous_lux_ = false;
    // A capture of the screen was requested and the next event calculates with it.
    bool capturing_ = false;
    // The recalculation period has passed, so the next event inside the window captures too.
    bool recalculation_due_ = false;
    // The first event after enabling arrived before the screen was reported on.
    bool screen_turning_on_ = false;
    // Events at low brightness that were inside the window, every second one captures anyway.
    int32_t unchanged_count_ = 0;
    // Calculations left that do not wait for the raw value to leave the window.
    int32_t forced_recalculations_ = 0;
    // Lux range of the last calculated lux.
    int32_t lux_range_ = 0;
    // Brightness level the last calculation used, behind the dim layers.
    int32_t effective_level_ = 0;
    int32_t last_level_ = 0;
    int32_t lux_range_count_ = 9;
    int64_t last_calculation_ns_ = 0;
    int64_t last_event_ms_ = 0;
    int64_t last_period_ms_ = 0;
    float last_raw_lux_ = 0.0f;
    float last_lux_ = 0.0f;
    float last_lux_per_raw_ = 0.0f;
    // Light of the panel the last calculation subtracted.
    float last_leakage_ = 0.0f;
    float last_panel_load_factor_ = 1.0f;
    // Tolerance window of the raw value, negative limits force a calculation.
    float window_upper_ = -1.0f;
    float window_lower_ = -10.0f;

    // 2 while the screen is on, 0 while it is off.
    int32_t screen_state_ = 2;
    int64_t last_screen_on_ms_ = 0;
    // 2 from the screen turning on until the first capture was requested.
    int32_t pending_screen_on_ = 0;
    // Fingerprint flash: whether it is on, was on at the last calculation, and whether the hold
    // after its end was already applied.
    int32_t flash_active_ = 0;
    int32_t last_flash_active_ = 0;
    int32_t flash_end_handled_ = 0;
    int32_t past_first_calculation_ = 0;
    // Dim layer of the fingerprint sensor: its last alpha, and 1 once it has just disappeared,
    // which reports the previous lux one more time.
    int32_t last_dim_alpha_ = 0;
    int32_t dim_layer_ended_ = 0;

    // Moving average over the last six values, with a faster path for rising light.
    bool use_moving_average_ = false;
    std::array<float, 6> average_values_{};
    int32_t average_count_ = 0;
    int32_t average_head_ = 0;
    float average_sum_ = 0.0f;
    float average_ = 0.0f;
    float average_input_ = 0.0f;
    float trend_value_ = 0.0f;
    int32_t trend_count_ = 0;
    bool trend_up_ = false;

    bool use_median_filter_ = false;
    std::array<float, 6> median_values_{};
    int32_t median_count_ = 0;
    int32_t median_head_ = 0;
};

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
