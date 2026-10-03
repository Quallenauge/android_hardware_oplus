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
    // Load the whole screen puts on the panel, see ScreenLoadWeights: about 0 for a black and 1
    // for a white screen. Only known for captures of the whole screen.
    std::optional<float> load;
};

// How much a lit pixel loads the panel, depending on its color and its position.
//
// The load of a pixel is the sum of the linear light of its channels (the code value to the
// power of 2.2, between 0 and 1), each multiplied by the weight of the channel. The weights add
// up to about 1, so that a white pixel counts as 1. On lunaa blue loads the panel about twice as
// much as red or green.
//
// Where a pixel is matters as well: measured on lunaa, with the sensor at the top edge, lit rows
// close to the top take more light away above the sensor than rows at the bottom. The share
// "uniform" of the load counts equally for every row, the rest grows linearly from 0 at the
// bottom edge to twice its mean at the top edge.
struct ScreenLoadWeights {
    float red;
    float green;
    float blue;
    float uniform;

    // Load of a screen that shows the given color everywhere.
    float of(const ScreenColor& color) const;
};

// Averages the region of an RGBA_8888 frame into the color the panel shows there. With load
// weights the frame has to cover the whole screen, whose load is determined as well.
ScreenColor SampleFrame(const uint8_t* pixels, size_t row_bytes, int32_t width, int32_t height,
                        const ScreenRegion& region, const ScreenLoadWeights* load_weights);

// Periodically samples the average color of the screen region above the light sensor through
// concurrent writeback of the display composer.
class ScreenSampler {
  public:
    // The load weights are used for whole screen captures.
    ScreenSampler(ScreenRegion region, std::chrono::milliseconds period,
                  std::optional<ScreenLoadWeights> load_weights);
    virtual ~ScreenSampler();

    DISALLOW_COPY_AND_ASSIGN(ScreenSampler);

    virtual void start();
    virtual void stop();
    virtual void setCapturing(bool capturing);
    // Captures the whole screen instead of only the region, which also determines the load of
    // the screen. The composer then has to write back the complete frame for every capture
    // instead of a few hundred pixels, so only ask for it while the load is needed. Has no effect
    // without load weights.
    virtual void setWholeScreen(bool whole_screen);

    // Latest sampled color, or std::nullopt when nothing was captured yet.
    virtual std::optional<ScreenColor> latest();

  private:
    class Client;

    void threadLoop();

    const ScreenRegion region_;
    const std::chrono::milliseconds period_;
    const std::optional<ScreenLoadWeights> load_weights_;

    std::mutex mutex_;
    std::condition_variable condition_;
    bool running_ = false;
    bool capturing_ = false;
    bool whole_screen_ = false;
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
