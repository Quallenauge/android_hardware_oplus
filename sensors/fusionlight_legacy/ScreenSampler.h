/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include <android-base/macros.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {

struct ScreenRegion {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

struct ScreenColor {
    int32_t red;
    int32_t green;
    int32_t blue;
};

// Averages the region of an RGBA_8888 frame into the color the panel shows there.
ScreenColor SampleFrame(const uint8_t* pixels, size_t row_bytes, const ScreenRegion& region);

// Periodically samples the average color of the screen region above the light sensor through
// concurrent writeback of the display composer.
class ScreenSampler {
  public:
    ScreenSampler(ScreenRegion region, std::chrono::milliseconds period);
    virtual ~ScreenSampler();

    DISALLOW_COPY_AND_ASSIGN(ScreenSampler);

    virtual void start();
    virtual void stop();
    virtual void setCapturing(bool capturing);

    // Latest sampled color, or std::nullopt when nothing was captured yet.
    virtual std::optional<ScreenColor> latest();

  private:
    class Client;

    void threadLoop();

    const ScreenRegion region_;
    const std::chrono::milliseconds period_;

    std::mutex mutex_;
    std::condition_variable condition_;
    bool running_ = false;
    bool capturing_ = false;
    std::optional<ScreenColor> latest_;
    std::thread thread_;
};

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
