/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "FusionLightArgs.h"
#include "ScreenSampler.h"

// The calculations of the correction. They only depend on their arguments.

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {

// Light the panel leaks into the sensor at full brightness.
struct PanelLeakage {
    float red;
    float green;
    float blue;
    // What the three channels leak less together than their sum.
    float overlap;

    float total() const { return red + green + blue - overlap; }
};

// Leakage of a screen that shows the color everywhere, at full brightness.
PanelLeakage LeakageAtFullBrightness(const FusionLightArgs& args, const ScreenColor& color);

// Leakage at a brightness level, according to the brightness curve of the profile. The dim alpha
// is that of the fingerprint dim layer, which one form of the curve takes into account.
float LeakageAtBrightness(const FusionLightArgs& args, PanelLeakage channels, int32_t level,
                          int32_t max_level, const ScreenColor& color, int32_t dim_alpha);

// Factor between a raw value without the light of the panel and lux. For brighter light it is
// selected by the clear channel c of the sensor, for one profile form together with the value g
// the sensor delivers next to it.
float LuxPerRaw(const FusionLightArgs& args, float raw_lux, float c, float g);

// Brightness level that emits as much light as the panel behind the dim layer of the fingerprint
// sensor, which has the given alpha.
float LevelBehindDimLayer(const FusionLightArgs& args, float level, int32_t max_level,
                          int32_t dim_alpha);

// The same for DC dimming, which dims the image instead of the backlight at low brightness.
float LevelWithDcDimming(const FusionLightArgs& args, float level, int32_t dc_alpha);

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
