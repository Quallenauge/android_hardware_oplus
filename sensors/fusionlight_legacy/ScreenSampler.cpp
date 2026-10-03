/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ScreenSampler.h"

#include <algorithm>
#include <array>
#include <limits>

#include <android-base/logging.h>
#include <config/client_interface.h>
#include <hidl/HidlTransportSupport.h>
#include <hwbinder/ProcessState.h>
#include <ui/GraphicBuffer.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace {

using namespace std::chrono_literals;

constexpr auto kRequestTimeout = 1s;
constexpr auto kRetryPeriod = 2s;
constexpr int32_t kFailureThreshold = 3;
constexpr int32_t kSampleStep = 3;
constexpr uint64_t kBufferUsage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;

// Light the panel emits per 8-bit code value, relative to 255 for the brightest one. The region
// above the sensor is averaged by this light, not by the code values.
// clang-format off
constexpr std::array<float, 256> kPanelResponse = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.02f, 0.04f, 0.04f, 0.26f, 0.49f, 0.71f, 0.94f, 1.17f, 1.4f,
        1.64f, 1.87f, 2.12f, 2.36f, 2.61f, 2.86f, 3.12f, 3.37f, 3.63f, 3.9f, 4.17f, 4.44f, 4.71f,
        4.99f, 5.27f, 5.56f, 5.85f, 6.14f, 6.44f, 6.74f, 7.04f, 7.35f, 7.67f, 7.98f, 8.3f, 8.63f,
        8.96f, 9.29f, 9.63f, 9.97f, 10.31f, 10.66f, 11.02f, 11.38f, 11.74f, 12.11f, 12.48f, 12.86f,
        13.24f, 13.63f, 14.02f, 14.42f, 14.82f, 15.23f, 15.64f, 16.06f, 16.48f, 16.91f, 17.34f,
        17.78f, 18.22f, 18.67f, 19.12f, 19.58f, 20.04f, 20.51f, 20.99f, 21.47f, 21.96f, 22.45f,
        22.95f, 23.45f, 23.96f, 24.48f, 25.0f, 25.52f, 26.06f, 26.6f, 27.14f, 27.7f, 28.25f, 28.82f,
        29.39f, 29.97f, 30.55f, 31.14f, 31.74f, 32.34f, 32.95f, 33.57f, 34.19f, 34.82f, 35.46f,
        36.1f, 36.75f, 37.41f, 38.07f, 38.74f, 39.42f, 40.1f, 40.8f, 41.5f, 42.2f, 42.92f, 43.64f,
        44.37f, 45.11f, 45.85f, 46.6f, 47.36f, 48.13f, 48.9f, 49.68f, 50.47f, 51.27f, 52.08f,
        52.89f, 53.71f, 54.54f, 55.38f, 56.22f, 57.08f, 57.94f, 58.81f, 59.69f, 60.57f, 61.47f,
        62.37f, 63.28f, 64.2f, 65.13f, 66.07f, 67.02f, 67.97f, 68.94f, 69.91f, 70.89f, 71.88f,
        72.88f, 73.89f, 74.9f, 75.93f, 76.97f, 78.01f, 79.06f, 80.13f, 81.2f, 82.28f, 83.37f,
        84.47f, 85.58f, 86.7f, 87.83f, 88.97f, 90.12f, 91.28f, 92.45f, 93.62f, 94.81f, 96.01f,
        97.22f, 98.43f, 99.66f, 100.9f, 102.15f, 103.41f, 104.67f, 105.95f, 107.24f, 108.54f,
        109.85f, 111.17f, 112.5f, 113.85f, 115.2f, 116.56f, 117.93f, 119.32f, 120.71f, 122.12f,
        123.54f, 124.96f, 126.4f, 127.85f, 129.31f, 130.79f, 132.27f, 133.76f, 135.27f, 136.79f,
        138.31f, 139.85f, 141.41f, 142.97f, 144.54f, 146.13f, 147.73f, 149.34f, 150.96f, 152.59f,
        154.24f, 155.89f, 157.56f, 159.24f, 160.93f, 162.64f, 164.36f, 166.08f, 167.83f, 169.58f,
        171.35f, 173.12f, 174.91f, 176.72f, 178.53f, 180.36f, 182.2f, 184.05f, 185.92f, 187.8f,
        189.69f, 191.59f, 193.51f, 195.44f, 197.39f, 199.34f, 201.31f, 203.29f, 205.29f, 207.3f,
        209.32f, 211.35f, 213.4f, 215.46f, 217.54f, 219.63f, 221.73f, 223.85f, 225.98f, 228.12f,
        230.28f, 232.45f, 234.63f, 236.83f, 239.04f, 241.27f, 243.51f, 245.76f, 248.03f, 250.31f,
        252.61f, 255.0f,
};
// clang-format on

int32_t ToCodeValue(float response) {
    // Nearest entry, compared with two decimals.
    const auto target = static_cast<uint32_t>(response * 100.0f);
    uint32_t best_difference = 100000;
    int32_t best = 0;
    for (int32_t value = 0; value < static_cast<int32_t>(kPanelResponse.size()); ++value) {
        const auto entry = static_cast<uint32_t>(kPanelResponse[value] * 100.0f);
        const uint32_t difference = entry > target ? entry - target : target - entry;
        if (difference < best_difference) {
            best_difference = difference;
            best = value;
        }
    }
    return best;
}

void StartHwBinderThreadPool() {
    // The multihal service only runs an AIDL thread pool, but DisplayConfig 2.0 reports CWB
    // completion through a HIDL callback.
    static std::once_flag once;
    std::call_once(once, [] {
        configureRpcThreadpool(1, false /* callerWillJoin */);
        ProcessState::self()->startThreadPool();
    });
}

}  // anonymous namespace

class ScreenSampler::Client final : public DisplayConfig::ConfigCallback {
  public:
    // Never destroyed, since DisplayConfig keeps a raw pointer to the callback.
    static Client* get() {
        static Client* client = new Client();
        return client;
    }

    bool connect() {
        if (interface_ != nullptr) {
            return true;
        }
        StartHwBinderThreadPool();
        if (DisplayConfig::ClientInterface::Create("fusionlight_legacy", this, &interface_) != 0) {
            interface_ = nullptr;
        }
        return interface_ != nullptr;
    }

    bool getResolution(uint32_t* width, uint32_t* height) {
        uint32_t config;
        DisplayConfig::Attributes attributes;
        if (interface_->GetActiveConfig(DisplayConfig::DisplayType::kPrimary, &config) != 0 ||
            interface_->GetDisplayAttributes(config, DisplayConfig::DisplayType::kPrimary,
                                             &attributes) != 0) {
            return false;
        }
        *width = attributes.x_res;
        *height = attributes.y_res;
        return *width > 0 && *height > 0;
    }

    // Returns the capture status, or std::nullopt on timeout.
    std::optional<int> capture(const native_handle_t* buffer, const ScreenRegion& region) {
        uint64_t request;
        {
            std::lock_guard lock(mutex_);
            request = ++requested_;
        }
        const DisplayConfig::Rect rect = {
                static_cast<uint32_t>(region.left), static_cast<uint32_t>(region.top),
                static_cast<uint32_t>(region.right), static_cast<uint32_t>(region.bottom)};
        if (const int error = interface_->SetCWBOutputBuffer(
                    static_cast<uint32_t>(DisplayConfig::DisplayType::kPrimary), rect,
                    true /* post_processed */, buffer);
            error != 0) {
            std::lock_guard lock(mutex_);
            // Nothing was queued, so no completion will arrive for this request.
            --requested_;
            return error;
        }

        std::unique_lock lock(mutex_);
        if (!condition_.wait_for(lock, kRequestTimeout, [&] { return completed_ >= request; })) {
            // Every request targets the same buffer, so a late completion is harmless.
            completed_ = requested_;
            return std::nullopt;
        }
        return error_;
    }

    // Reconnects on the next capture, e.g. after the composer restarted.
    void reset() {
        if (interface_ != nullptr) {
            DisplayConfig::ClientInterface::Destroy(interface_);
            interface_ = nullptr;
        }
    }

    void NotifyCWBBufferDone(int error, const native_handle_t* /* buffer */) override {
        {
            std::lock_guard lock(mutex_);
            ++completed_;
            error_ = error;
        }
        condition_.notify_all();
    }

  private:
    Client() = default;

    DisplayConfig::ClientInterface* interface_ = nullptr;
    std::mutex mutex_;
    std::condition_variable condition_;
    uint64_t requested_ = 0;
    uint64_t completed_ = 0;
    int error_ = 0;
};

ScreenColor SampleFrame(const uint8_t* pixels, size_t row_bytes, const ScreenRegion& region) {
    float red = 0.0f, green = 0.0f, blue = 0.0f;
    int32_t count = 0;
    for (int32_t y = region.top; y < region.bottom; y += kSampleStep) {
        for (int32_t x = region.left; x < region.right; x += kSampleStep) {
            const uint8_t* pixel = pixels + y * row_bytes + x * 4;
            red += kPanelResponse[pixel[0]];
            green += kPanelResponse[pixel[1]];
            blue += kPanelResponse[pixel[2]];
            ++count;
        }
    }
    return ScreenColor{ToCodeValue(red / count), ToCodeValue(green / count),
                       ToCodeValue(blue / count)};
}

ScreenSampler::ScreenSampler(ScreenRegion region, std::chrono::milliseconds period)
    : region_(region), period_(period) {}

ScreenSampler::~ScreenSampler() {
    stop();
}

void ScreenSampler::start() {
    std::lock_guard lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    capturing_ = false;
    thread_ = std::thread(&ScreenSampler::threadLoop, this);
}

void ScreenSampler::stop() {
    {
        std::lock_guard lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    condition_.notify_all();
    thread_.join();
}

void ScreenSampler::setCapturing(bool capturing) {
    {
        std::lock_guard lock(mutex_);
        capturing_ = capturing;
    }
    condition_.notify_all();
}

std::optional<ScreenColor> ScreenSampler::latest() {
    std::lock_guard lock(mutex_);
    return latest_;
}

void ScreenSampler::threadLoop() {
    Client* client = Client::get();
    sp<GraphicBuffer> buffer;
    int32_t failures = 0;
    auto next_sample = std::chrono::steady_clock::now();

    for (;;) {
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return !running_ || capturing_; });
            condition_.wait_until(lock, next_sample, [&] { return !running_ || !capturing_; });
            if (!running_) {
                break;
            }
            if (!capturing_) {
                continue;
            }
        }

        auto retry = [&] {
            if (++failures >= kFailureThreshold) {
                client->reset();
                buffer.clear();
                failures = 0;
            }
            next_sample = std::chrono::steady_clock::now() + kRetryPeriod;
        };

        if (!client->connect()) {
            LOG(ERROR) << "DisplayConfig is unavailable";
            retry();
            continue;
        }

        if (buffer == nullptr) {
            uint32_t width, height;
            if (!client->getResolution(&width, &height)) {
                LOG(ERROR) << "Unable to get the display resolution";
                retry();
                continue;
            }
            if (static_cast<uint32_t>(region_.right) > width ||
                static_cast<uint32_t>(region_.bottom) > height) {
                LOG(ERROR) << "Sensor region exceeds the " << width << "x" << height << " display";
                retry();
                continue;
            }
            buffer = sp<GraphicBuffer>::make(width, height, PIXEL_FORMAT_RGBA_8888, 1, kBufferUsage,
                                             "FusionLightLegacyCWB");
            if (buffer->initCheck() != NO_ERROR) {
                LOG(ERROR) << "Unable to allocate the CWB buffer: " << buffer->initCheck();
                buffer.clear();
                retry();
                continue;
            }
        }

        const auto status = client->capture(buffer->handle, region_);
        if (!status.has_value() || *status != 0) {
            LOG(WARNING) << "CWB capture failed: "
                         << (status.has_value() ? std::to_string(*status) : "timeout");
            retry();
            continue;
        }

        void* data = nullptr;
        if (const status_t error = buffer->lock(GRALLOC_USAGE_SW_READ_OFTEN, &data);
            error != NO_ERROR || data == nullptr) {
            LOG(ERROR) << "Unable to map the CWB buffer: " << error;
            retry();
            continue;
        }
        const auto* pixels = static_cast<const uint8_t*>(data);
        const size_t row_bytes = static_cast<size_t>(buffer->getStride()) * 4;
        const ScreenColor color = SampleFrame(pixels, row_bytes, region_);
        buffer->unlock();

        {
            std::lock_guard lock(mutex_);
            latest_ = color;
        }
        failures = 0;
        next_sample += period_;
        next_sample = std::max(next_sample, std::chrono::steady_clock::now());
    }
}

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
