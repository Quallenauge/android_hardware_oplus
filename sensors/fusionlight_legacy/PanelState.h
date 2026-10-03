/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include <android-base/unique_fd.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {

// State of the panel the correction depends on, read from the display driver.
class PanelState {
  public:
    virtual ~PanelState() = default;

    // Backlight level applied to the panel, after the kernel brightness remapping.
    virtual int32_t brightness();
    // Alpha of the fingerprint dim layer, 0 when inactive.
    virtual int32_t dimAlpha();
    // Alpha used for DC dimming, 0 when inactive.
    virtual int32_t dimDcAlpha();
    // Whether a finger is on the under display fingerprint sensor, which flashes the panel.
    virtual bool fingerprintPressed();

  private:
    int32_t get(unsigned long request);

    android::base::unique_fd fd_;
};

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
