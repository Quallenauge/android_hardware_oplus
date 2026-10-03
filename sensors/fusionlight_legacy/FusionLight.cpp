/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "FusionLight.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <android-base/logging.h>
#include <android-base/parsedouble.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <utils/Timers.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace {

using android::base::GetBoolProperty;
using android::base::GetIntProperty;
using android::base::GetProperty;

// Screen region above the light sensor as "left,top,right,bottom" in pixels.
constexpr auto kRegionProperty = "ro.vendor.oplus.sensor.fusionlight.region";
constexpr auto kPeriodProperty = "ro.vendor.oplus.sensor.fusionlight.screenshot_period";
// Table of the brightness the framework sets and the level the panel gets for it, as
// "brightness:level,brightness:level,...", for example "4:4,61:5,100:8,...,2047:2047".
// Set it on devices whose kernel sends the brightness to the panel unchanged, although the
// fusionlight profile was made with a kernel that remaps it (oplus,dsi-brightness-remapping in
// the device tree of the panel, which is where the values come from). Leave it unset if the
// running kernel has the table: find /proc/device-tree -name "oplus,dsi-brightness-remapping".
constexpr auto kBrightnessRemapProperty = "ro.vendor.oplus.sensor.fusionlight.brightness_remap";
// Parameters of the panel load correction as "drop,exponent,uniform,red,green,blue,scale", for
// example "0.218,0.82,0.41,0.256,0.232,0.528,0.963", see PanelLoadArgs and ScreenLoadWeights.
// They belong to a panel and have to be measured, they cannot be taken from a profile. Without
// the property the load of the panel is not taken into account.
constexpr auto kPanelLoadProperty = "ro.vendor.oplus.sensor.fusionlight.panel_load";
constexpr auto kDebugProperty = "persist.vendor.sensors.fusionlight.debug";
// The load correction needs a capture of the whole screen instead of the few pixels above the
// sensor, which costs a lot more. It changes the light of the panel by a fifth at most, which
// only matters while that light is a large part of what the sensor sees. So the whole screen is
// only captured while the raw value is below this multiple of the brightest light of the panel
// according to the factory calibration. In brighter surroundings the correction is left out.
constexpr float kPanelLoadRawLuxFactor = 2.0f;
constexpr int64_t kCalculationHoldNs = 50'000'000;
constexpr int64_t kFlashHoldNs = 800'000'000;

int64_t NowMs() {
    return systemTime(SYSTEM_TIME_BOOTTIME) / 1'000'000;
}

std::optional<ScreenRegion> ParseRegion(const std::string& value) {
    const auto fields = android::base::Split(value, ",");
    if (fields.size() != 4) {
        return std::nullopt;
    }
    ScreenRegion region;
    int32_t* targets[] = {&region.left, &region.top, &region.right, &region.bottom};
    for (size_t i = 0; i < fields.size(); ++i) {
        if (!android::base::ParseInt(android::base::Trim(fields[i]), targets[i], 0)) {
            return std::nullopt;
        }
    }
    if (region.right <= region.left || region.bottom <= region.top) {
        return std::nullopt;
    }
    return region;
}

}  // anonymous namespace

// Parses the brightness remapping table. Both columns have to increase from entry to entry,
// otherwise the conversion would be ambiguous. An invalid table is dropped as a whole, which
// leaves the level unconverted, rather than used in part.
std::vector<BrightnessRemapPoint> ParseBrightnessRemap(const std::string& value) {
    std::vector<BrightnessRemapPoint> table;
    if (value.empty()) {
        return table;
    }
    for (const auto& entry : android::base::Split(value, ",")) {
        const auto fields = android::base::Split(entry, ":");
        BrightnessRemapPoint point;
        if (fields.size() != 2 ||
            !android::base::ParseInt(android::base::Trim(fields[0]), &point.brightness, 0) ||
            !android::base::ParseInt(android::base::Trim(fields[1]), &point.level, 0) ||
            (!table.empty() &&
             (point.brightness <= table.back().brightness || point.level <= table.back().level))) {
            LOG(ERROR) << "Invalid " << kBrightnessRemapProperty << " entry " << entry;
            return {};
        }
        table.push_back(point);
    }
    return table;
}

// Parses the parameters of the panel load correction. All seven are required and none may be
// negative, otherwise the correction stays off.
std::optional<PanelLoadArgs> ParsePanelLoad(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    const auto fields = android::base::Split(value, ",");
    std::array<float, 7> values;
    if (fields.size() != values.size()) {
        LOG(ERROR) << "Invalid " << kPanelLoadProperty;
        return std::nullopt;
    }
    for (size_t i = 0; i < values.size(); ++i) {
        if (!android::base::ParseFloat(android::base::Trim(fields[i]), &values[i]) ||
            values[i] < 0.0f) {
            LOG(ERROR) << "Invalid " << kPanelLoadProperty << " entry " << fields[i];
            return std::nullopt;
        }
    }
    return PanelLoadArgs{
            values[0], values[1], {values[3], values[4], values[5], values[2]}, values[6]};
}

namespace {

void FillUncalculated(Event& event, float raw_lux, int32_t level) {
    event.u.data[1] = raw_lux;
    for (size_t i = 2; i <= 8; ++i) {
        event.u.data[i] = -1.0f;
    }
    event.u.data[9] = level;
}

}  // anonymous namespace

bool FusionLight::initialize(const std::string& sensor_name) {
    const auto region = ParseRegion(GetProperty(kRegionProperty, ""));
    if (!region.has_value()) {
        LOG(ERROR) << "Missing or invalid " << kRegionProperty << ", not correcting "
                   << sensor_name;
        return false;
    }
    const std::chrono::milliseconds period(GetIntProperty(kPeriodProperty, 50, 1, 1000));
    FusionLightEnvironment environment;
    environment.brightness_remap = ParseBrightnessRemap(GetProperty(kBrightnessRemapProperty, ""));
    environment.panel_load = ParsePanelLoad(GetProperty(kPanelLoadProperty, ""));
    environment.panel = std::make_unique<PanelState>();
    environment.sampler = std::make_unique<ScreenSampler>(
            *region, period,
            environment.panel_load.has_value() ? std::make_optional(environment.panel_load->weights)
                                               : std::nullopt);
    environment.load_args = LoadFusionLightArgs;
    environment.now_ms = NowMs;
    initialize(sensor_name, std::move(environment));
    LOG(INFO) << "Correcting " << sensor_name << " with screen region [" << region->left << ", "
              << region->top << ", " << region->right << ", " << region->bottom << "]";
    return true;
}

void FusionLight::initialize(const std::string& sensor_name, FusionLightEnvironment environment) {
    std::lock_guard lock(mutex_);
    sensor_name_ = sensor_name;
    panel_ = std::move(environment.panel);
    sampler_ = std::move(environment.sampler);
    brightness_remap_ = std::move(environment.brightness_remap);
    panel_load_ = environment.panel_load;
    load_args_ = std::move(environment.load_args);
    now_ms_ = std::move(environment.now_ms);
}

void FusionLight::setEnabled(bool enabled) {
    std::lock_guard lock(mutex_);
    if (sampler_ == nullptr || enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (enabled) {
        debug_ = GetBoolProperty(kDebugProperty, false);
        args_ = load_args_(sensor_name_);
        use_moving_average_ = args_.use_moving_average != 0;
        use_median_filter_ = args_.use_median_filter != 0;
        lux_valid_ = false;
        sampler_->start();
        sampler_->setCapturing(true);
        capturing_ = true;
        forced_recalculations_ = 3;
        return;
    }

    capturing_ = false;
    last_event_ms_ = 0;
    window_upper_ = -1.0f;
    window_lower_ = -10.0f;
    if (use_moving_average_) {
        average_values_.fill(0.0f);
        average_count_ = average_head_ = trend_count_ = 0;
        average_sum_ = average_ = average_input_ = trend_value_ = 0.0f;
        trend_up_ = false;
    }
    if (use_median_filter_) {
        median_values_.fill(0.0f);
        median_count_ = median_head_ = 0;
    }
    // Releases the capture buffer while the sensor is off.
    sampler_->stop();
}

bool FusionLight::process(Event& event) {
    std::lock_guard lock(mutex_);
    if (!enabled_) {
        return true;
    }

    // The panel flashes below a finger on the fingerprint sensor. The touch state stays set
    // after a successful unlock, so also require the panel to be in its fingerprint high
    // brightness mode, which is the only time the dim alpha is reported.
    const int32_t flash_state = panel_->fingerprintPressed() && panel_->dimAlpha() != 0 ? 1 : 0;
    if (flash_state != flash_active_) {
        flash_active_ = flash_state;
        if (flash_state == 1) {
            flash_end_handled_ = 0;
        }
    }

    panel_level_ = panel_->brightness();
    if (panel_load_.has_value()) {
        // Decided for every event from the raw value alone, so it follows the surroundings
        // getting dark without waiting for a calculation.
        const float panel_light = args_.r.max + args_.g.max + args_.b.max;
        sampler_->setWholeScreen(event.u.data[0] < kPanelLoadRawLuxFactor * panel_light);
    }
    int32_t level = ToBrightness(brightness_remap_, panel_level_);
    updateScreenState(level);
    if (args_.level_from_event != 0) {
        level = static_cast<int32_t>(event.u.data[3]);
    }
    reported_previous_lux_ = false;
    bool reported = acceptEvent(event, level);
    last_level_ = level;
    if (reported && reported_previous_lux_ && !lux_valid_) {
        // The last lux is from before the sensor was disabled, possibly hours ago. Do not report
        // it as the first value, e.g. while a fingerprint flash holds the lux back.
        reported = false;
        if (debug_) LOG(INFO) << "Not reporting the lux from before the sensor was enabled";
    } else if (reported) {
        lux_valid_ = true;
    }
    if (debug_) {
        LOG(INFO) << "reported=" << reported << " lux=" << event.u.data[0] << " level=" << level;
    }
    return reported;
}

void FusionLight::updateScreenState(int32_t level) {
    // A powered off panel has no backlight, so the level tells whether the screen is on.
    if (level <= 0) {
        if (screen_state_ != 0) {
            screen_state_ = 0;
            last_screen_on_ms_ = 0;
            flash_end_handled_ = 0;
            dim_layer_ended_ = 0;
            last_dim_alpha_ = 0;
        }
    } else if (screen_state_ != 2) {
        pending_screen_on_ = 2;
        last_screen_on_ms_ = now_ms_();
        screen_state_ = 2;
    }
}

bool FusionLight::acceptEvent(Event& event, int32_t level) {
    const int64_t now = now_ms_();
    if (debug_) {
        LOG(INFO) << "raw=" << event.u.data[0] << " period=" << event.u.data[1]
                  << " clear=" << event.u.data[2] << " screen_state=" << screen_state_
                  << " level[last=" << last_level_ << ", cur=" << level << "]";
    }

    if (level == 0 || (screen_state_ < 4 && screen_state_ != 2)) {
        if (args_.report_low_light == 0 || event.u.data[2] != 0.0f) {
            reportWithScreenOff(event, event.u.data[0], level);
        } else {
            reportLowLight(event, event.u.data[0], level);
        }
        finishReport(event);
        return true;
    }

    if (last_event_ms_ == 0) {
        last_event_ms_ = now;
        last_period_ms_ = now;
        if (screen_state_ == 2) {
            const int32_t interval =
                    static_cast<int32_t>(last_screen_on_ms_) - static_cast<int32_t>(now);
            const int64_t delta = last_screen_on_ms_ - event.timestamp / 1'000'000;
            if (interval > 0 || delta > 0) {
                screen_turning_on_ = true;
            }
        }
    } else {
        screen_turning_on_ = false;
        if (now - last_period_ms_ > args_.recalculation_period_ms) {
            last_period_ms_ = now;
            recalculation_due_ = true;
        }
        const int64_t gap = now - last_event_ms_;
        last_event_ms_ = now;
        if (gap <= args_.min_event_gap_ms) {
            return false;
        }
    }
    return updateLux(event, level);
}

bool FusionLight::updateLux(Event& event, int32_t level) {
    const float raw_lux = event.u.data[0];
    const bool forced = window_lower_ == -10.0f || forced_recalculations_ > 0;
    const bool recent_shot = last_calculation_ns_ != 0 &&
                             event.timestamp - last_calculation_ns_ < kCalculationHoldNs;
    if (debug_) {
        LOG(INFO) << "[" << window_lower_ << ", " << window_upper_ << "] raw=" << raw_lux
                  << " forced=" << forced << " capturing=" << capturing_
                  << " due=" << recalculation_due_;
    }

    const bool outside = raw_lux < window_lower_ || window_upper_ < raw_lux || forced;
    if (!outside && !capturing_) {
        if (recent_shot || !recalculation_due_) {
            return reportUnchanged(event, raw_lux, level);
        }
        return requestCapture(event, raw_lux, level, forced);
    }
    if (recent_shot) {
        return reportUnchanged(event, raw_lux, level);
    }
    if (!capturing_) {
        return requestCapture(event, raw_lux, level, forced);
    }
    stopCapture();

    if (args_.report_low_light != 0 && event.u.data[2] == 0.0f) {
        reportLowLight(event, raw_lux, level);
        if (flash_end_handled_ == 0) {
            last_flash_active_ = flash_active_;
        }
        finishReport(event);
        return true;
    }

    last_calculation_ns_ = event.timestamp;
    if (flash_end_handled_ == 0) {
        if (flash_active_ == 0 && past_first_calculation_ != 0 && last_flash_active_ != 0) {
            flash_end_handled_ = 1;
            reportPreviousLux(raw_lux, event, level);
            last_flash_active_ = flash_active_;
            last_calculation_ns_ = event.timestamp + kFlashHoldNs;
            finishReport(event);
            return true;
        }
        if (flash_active_ == 1) {
            reportPreviousLux(raw_lux, event, level);
            last_flash_active_ = flash_active_;
            finishReport(event);
            return true;
        }
        past_first_calculation_ = 1;
        last_flash_active_ = flash_active_;
    }

    const ScreenColor color = screenColor();
    if (color.red < 0) {
        if (debug_) LOG(INFO) << "No usable capture, reporting the previous lux";
        reportPreviousLux(raw_lux, event, level);
        finishReport(event);
        return true;
    }

    level = std::max(level, 0);
    int32_t calibrated = level;
    if (args_.level_from_event == 0 && level != 0 && level != last_level_) {
        // The brightness ramps while the sensor integrates, limit the change to its speed.
        const int32_t period = static_cast<int32_t>(event.u.data[1] * 1e6) / 1'000'000;
        const int32_t step = static_cast<int32_t>((400 - period) / 1.25);
        const int32_t candidate = last_level_ + (level > last_level_ ? step : -step);
        if (candidate >= 0 && (level > last_level_ ? candidate <= level : level <= candidate)) {
            calibrated = candidate;
        }
    }

    effective_level_ = static_cast<int32_t>(levelBehindDimLayer(calibrated, args_.max_level));
    if (effective_level_ < 0) {
        LOG(WARNING) << "levelBehindDimLayer returned a negative level";
        effective_level_ = 0;
    }
    effective_level_ = static_cast<int32_t>(levelWithDcDimming(effective_level_));
    if (effective_level_ < 0) {
        LOG(WARNING) << "levelWithDcDimming returned a negative level";
        effective_level_ = 0;
    }

    if (dim_layer_ended_ == 1) {
        reportPreviousLux(raw_lux, event, level);
        dim_layer_ended_ = -1;
        finishReport(event);
        return true;
    }

    float adj_lux =
            calculateLux(event, screen_turning_on_ ? 0 : effective_level_, args_.max_level, color);
    adj_lux = std::max(adj_lux, 0.0f);
    const float lux = adj_lux >= args_.min_lux ? adj_lux : 0.0f;

    const auto& tables = args_.ranges(lux_range_count_);
    lux_range_ = findLuxRange(lux);
    // The interval follows the brightness level of the reported lux, not the current one.
    if (effective_level_ != 0) {
        last_leakage_ *= tables.level[lux_range_] / static_cast<float>(effective_level_);
    }
    updateToleranceWindow();
    if (effective_level_ != 0) {
        last_level_ = level;
        last_leakage_ /= tables.level[lux_range_] / static_cast<float>(effective_level_);
    }

    float output = lux;
    if (std::abs(lux - last_lux_) <= 2.0f) {
        // Settles small changes, unless the periodic shot sees the lux increase.
        output = recalculation_due_ && lux > last_lux_ ? last_lux_ : (lux + last_lux_) * 0.5f;
    }
    if (output < args_.min_lux) {
        output = 0.0f;
    }
    event.u.data[0] = output;
    last_lux_ = output;
    last_raw_lux_ = lux / last_lux_per_raw_ / (args_.calibration_factor / 1000.0f);
    recalculation_due_ = false;
    if (forced_recalculations_ > 0) {
        --forced_recalculations_;
    }
    if (use_moving_average_) {
        event.u.data[0] = movingAverage(event);
    }
    if (use_median_filter_) {
        if (args_.median_filter_max_lux == -1 || event.u.data[0] < args_.median_filter_max_lux) {
            event.u.data[0] = medianFilter(event);
        } else {
            median_count_ = 0;
        }
    }
    if (debug_) {
        LOG(INFO) << "lux=" << event.u.data[0] << " leakage=" << event.u.data[3] << " RGB("
                  << event.u.data[5] << ", " << event.u.data[6] << ", " << event.u.data[7]
                  << ") load_factor=" << last_panel_load_factor_ << " clear=" << event.u.data[8]
                  << " ambient_raw=" << last_raw_lux_ << " range[" << lux_range_
                  << "].level=" << tables.level[lux_range_];
    }
    finishReport(event);
    return true;
}

bool FusionLight::requestCapture(Event& event, float raw_lux, int32_t level, bool forced) {
    if (args_.report_bright_light != 0 && raw_lux > args_.w.max * 10.0f * level / 1023.0f) {
        reportBrightLight(event, raw_lux, level);
    } else if (args_.report_low_light != 0 && event.u.data[2] == 0.0f) {
        reportLowLight(event, raw_lux, level);
    } else {
        bool shoot = level > 100 || forced;
        if (!shoot && ++unchanged_count_ == 2) {
            unchanged_count_ = 0;
            forced_recalculations_ = 3;
            shoot = true;
        }
        if (shoot) {
            const float c = event.u.data[2];
            const float adj_lux = std::max(raw_lux - last_leakage_, 0.0f);
            float coe;
            if (adj_lux <= args_.lux_factor_min_raw) {
                coe = args_.lux_per_raw_override > 0.0f ? args_.lux_per_raw_override
                                                        : args_.lux_per_raw[0];
            } else {
                coe = LuxPerRaw(args_, adj_lux, c, event.u.data[1]);
            }
            if (raw_lux * coe * (args_.calibration_factor / 1000.0f) <= 2.5f) {
                reportDarkness(event, raw_lux, level);
                finishReport(event);
                return true;
            }
            sampler_->setCapturing(true);
            capturing_ = true;
            if (pending_screen_on_ == 2) {
                // Do not report the value from before the screen turned on.
                pending_screen_on_ = 0;
                return false;
            }
        }
        reportPreviousLux(raw_lux, event, level);
    }
    finishReport(event);
    return true;
}

bool FusionLight::reportUnchanged(Event& event, float raw_lux, int32_t level) {
    unchanged_count_ = 0;
    effective_level_ = level;
    if (last_level_ != 0) {
        last_leakage_ *= static_cast<float>(level) / last_level_;
    }
    updateToleranceWindow();
    last_level_ = effective_level_;
    reportPreviousLux(raw_lux, event, effective_level_);
    if (lux_range_ != findLuxRange(event.u.data[0])) {
        recalculation_due_ = true;
        return false;
    }
    finishReport(event);
    return true;
}

ScreenColor FusionLight::screenColor() {
    // Black until something was captured, which leaves the raw value uncompensated.
    return sampler_->latest().value_or(ScreenColor{0, 0, 0});
}

float FusionLight::calculateLux(Event& event, int32_t level, int32_t max_level,
                                const ScreenColor& color) {
    lux_range_count_ = args_.lux_range_count;
    const int32_t capped_level = std::min(level, args_.max_level);
    const float raw_lux = event.u.data[0];
    const float c = event.u.data[2];
    const float red = color.red;
    const float green = color.green;
    const float blue = color.blue;

    const PanelLeakage channels = LeakageAtFullBrightness(args_, color);
    const float panel_load_factor =
            panel_load_.has_value()
                    ? PanelLoadFactor(*panel_load_, color, panel_level_, args_.max_level)
                    : 1.0f;
    last_panel_load_factor_ = panel_load_factor;
    const float comp_lux =
            LeakageAtBrightness(args_, channels, capped_level, max_level, color, last_dim_alpha_) *
            panel_load_factor;
    const float truncated_raw_lux = static_cast<int32_t>(raw_lux);
    const float adj_lux = std::max(truncated_raw_lux - comp_lux, 0.0f);
    last_leakage_ = comp_lux;

    float coe;
    if (adj_lux <= args_.lux_factor_min_raw) {
        coe = args_.lux_per_raw_override > 0.0f ? args_.lux_per_raw_override : args_.lux_per_raw[0];
    } else {
        coe = LuxPerRaw(args_, adj_lux, c, event.u.data[1]);
    }
    last_lux_per_raw_ = coe;

    event.u.data[1] = truncated_raw_lux;
    event.u.data[2] = adj_lux;
    event.u.data[3] = comp_lux;
    event.u.data[4] = coe;
    event.u.data[5] = red;
    event.u.data[6] = green;
    event.u.data[7] = blue;
    event.u.data[8] = c;
    event.u.data[9] = effective_level_;
    return args_.calibration_factor / 1000.0f * coe * adj_lux;
}

float FusionLight::levelBehindDimLayer(float level, int32_t max_level) {
    const int32_t dim_alpha = panel_->dimAlpha();
    const float result = LevelBehindDimLayer(args_, level, max_level, dim_alpha);
    if (level != 0.0f && dim_alpha != 0) {
        if (debug_) {
            LOG(INFO) << "Fingerprint dim alpha " << dim_alpha << ", level " << level << " -> "
                      << result;
        }
    } else if (dim_alpha == 0 && last_dim_alpha_ != 0) {
        dim_layer_ended_ = 1;
    }
    last_dim_alpha_ = dim_alpha;
    return result;
}

float FusionLight::levelWithDcDimming(float level) {
    const int32_t dc_alpha = panel_->dimDcAlpha();
    const float result = LevelWithDcDimming(args_, level, dc_alpha);
    if (debug_ && dc_alpha != 0) {
        LOG(INFO) << "DC dim alpha " << dc_alpha << ", level " << level << " -> " << result;
    }
    return result;
}

void FusionLight::updateToleranceWindow() {
    const auto& tables = args_.ranges(lux_range_count_);
    const size_t index = std::min<size_t>(lux_range_, tables.window_upper.size() - 1);
    if (effective_level_ < 11) {
        window_upper_ = last_leakage_ + tables.window_upper[index];
        window_lower_ = -1.0f;
        return;
    }
    const int32_t top_level = lux_range_count_ == 15 ? 1023 : args_.max_level;
    window_upper_ =
            effective_level_ != top_level ? last_leakage_ + tables.window_upper[index] : 67000.0f;
    window_lower_ = last_leakage_ + tables.window_lower[index];
}

int32_t FusionLight::findLuxRange(float lux) {
    const auto& limits = args_.ranges(lux_range_count_).lux_limit;
    for (size_t i = 0; i < limits.size(); ++i) {
        if (lux <= limits[i]) {
            return i;
        }
    }
    return lux_range_count_ - 1;
}

void FusionLight::reportPreviousLux(float raw_lux, Event& event, int32_t level) {
    reported_previous_lux_ = true;
    float value = last_lux_;
    event.u.data[0] = value;
    if (use_moving_average_) {
        value = movingAverage(event);
        event.u.data[0] = value;
    }
    if (value < args_.min_lux) {
        value = 0.0f;
        event.u.data[0] = 0.0f;
    }
    last_lux_ = value;
    if (use_median_filter_) {
        if (args_.median_filter_max_lux == -1 || event.u.data[0] < args_.median_filter_max_lux) {
            event.u.data[0] = medianFilter(event);
        } else {
            median_count_ = 0;
        }
    }
    last_raw_lux_ = raw_lux;
    FillUncalculated(event, raw_lux, level);
}

void FusionLight::reportLowLight(Event& event, float raw_lux, int32_t level) {
    stopCapture();
    if (use_moving_average_) {
        event.u.data[0] = movingAverage(event);
    }
    if (use_median_filter_) {
        if (args_.median_filter_max_lux == -1 || event.u.data[0] < args_.median_filter_max_lux) {
            medianFilter(event);
        } else {
            median_count_ = 0;
        }
    }
    float value = args_.calibration_factor * raw_lux / 1000.0f;
    if (value < args_.min_lux_low_light) {
        value = 0.0f;
    }
    event.u.data[0] = value;
    last_raw_lux_ = raw_lux;
    last_lux_ = value;
    FillUncalculated(event, raw_lux, level);
}

void FusionLight::reportBrightLight(Event& event, float raw_lux, int32_t level) {
    stopCapture();
    const float value = last_lux_per_raw_ * raw_lux * args_.calibration_factor / 1000.0f;
    event.u.data[0] = value;
    if (use_moving_average_) {
        event.u.data[0] = movingAverage(event);
    }
    event.u.data[0] = value;
    last_lux_ = value;
    if (use_median_filter_) {
        if (args_.median_filter_max_lux == -1 || event.u.data[0] < args_.median_filter_max_lux) {
            medianFilter(event);
            event.u.data[0] = value;
        } else {
            median_count_ = 0;
        }
    }
    last_raw_lux_ = raw_lux;
    FillUncalculated(event, raw_lux, level);
}

void FusionLight::reportDarkness(Event& event, float raw_lux, int32_t level) {
    stopCapture();
    event.u.data[0] = 0.0f;
    if (use_moving_average_) {
        movingAverage(event);
    }
    event.u.data[0] = 0.0f;
    last_lux_ = 0.0f;
    if (use_median_filter_) {
        medianFilter(event);
        event.u.data[0] = 0.0f;
    }
    last_raw_lux_ = raw_lux;
    FillUncalculated(event, raw_lux, level);
}

void FusionLight::reportWithScreenOff(Event& event, float raw_lux, int32_t level) {
    stopCapture();
    float value = args_.lux_per_raw[0] * raw_lux * (args_.calibration_factor / 1000.0f);
    if (value < args_.min_lux) {
        value = 0.0f;
    }
    event.u.data[0] = value;
    last_lux_ = value;
    FillUncalculated(event, raw_lux, level);
}

void FusionLight::finishReport(Event& event) {
    if (args_.substitute_max_level != 0 && event.u.data[9] == args_.max_level) {
        event.u.data[9] = args_.max_level_substitute;
    }
}

void FusionLight::stopCapture() {
    if (capturing_) {
        sampler_->setCapturing(false);
        capturing_ = false;
    }
}

float FusionLight::movingAverage(Event& event) {
    const float value = event.u.data[0];
    average_input_ = value;
    float result = value;
    if (average_count_ == 6) {
        const float replaced = average_values_[average_head_];
        average_values_[average_head_] = value;
        average_sum_ = average_sum_ - replaced + value;
        average_ = average_sum_ / 6.0f;
        updateTrend();
        average_head_ = (average_head_ + 1) % 6;
        if (average_ <= args_.min_lux) {
            average_ = 0.0f;
        }
        if (use_moving_average_) {
            result = trend_up_ ? trend_value_ : average_;
        }
    } else {
        average_values_[average_count_++] = value;
        average_sum_ += value;
    }
    last_lux_ = result;
    return result;
}

void FusionLight::updateTrend() {
    const auto at = [&](int32_t offset) { return average_values_[(average_head_ + offset) % 6]; };
    const float middle = at(1) + at(2) + at(3);
    const auto judge = static_cast<float>(std::max(middle * 0.66, 8.0));
    trend_value_ = (at(0) + at(4) + at(5)) / 3.0f;
    if (average_input_ <= judge) {
        trend_count_ = 0;
        trend_up_ = false;
    } else if (++trend_count_ == 3) {
        trend_count_ = 0;
        trend_up_ = true;
    }
}

float FusionLight::medianFilter(Event& event) {
    const float value = event.u.data[0];
    last_lux_ = value;
    if (median_count_ != 6) {
        median_values_[median_count_++] = value;
        return value;
    }
    median_values_[median_head_] = value;
    median_head_ = (median_head_ + 1) % 6;
    auto sorted = median_values_;
    std::sort(sorted.begin(), sorted.end());
    // The second smallest of the last six values, which leans towards the darker readings.
    return sorted[1];
}

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
