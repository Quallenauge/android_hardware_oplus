/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <vector>

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

// One point of the table a kernel uses to turn the brightness the framework sets into the level
// it sends to the panel. Between the points the kernel interpolates linearly.
struct BrightnessRemapPoint {
    int32_t brightness;
    int32_t level;
};

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

// Converts the level applied to the panel into the brightness the profile is based on.
//
// A fusionlight profile describes the light of the panel as a function of the brightness the
// framework sets. Kernels with a remapping table (oplus,dsi-brightness-remapping in the device
// tree of the panel) send a different level to the panel: the table is roughly a gamma curve, so
// a medium brightness becomes a much lower level, and the curve of the profile has that built in.
// A kernel without the table sends the brightness as the level, which makes the panel much
// brighter at medium brightness than the profile assumes, and too little of its light would be
// compensated.
//
// With the table of the kernel the profile was made for, the level can be converted back: this
// looks up which brightness that kernel would have needed to reach the level. Everything in the
// correction that depends on the brightness then works as the profile expects.
//
// The table maps brightness to level, both strictly increasing, and is interpolated in the
// opposite direction, in whole numbers like the kernel does. Below its first point it continues
// to 0, above its last point it continues one to one. Without a table the level is the
// brightness.
int32_t ToBrightness(const std::vector<BrightnessRemapPoint>& table, int32_t level);

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
