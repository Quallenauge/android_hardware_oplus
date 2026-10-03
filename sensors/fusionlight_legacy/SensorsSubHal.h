/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <V2_0/SubHal.h>

#include "FusionLight.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorInfo;
using ::android::hardware::sensors::V1_0::SensorType;
using ::android::hardware::sensors::V1_0::SharedMemInfo;
using ::android::hardware::sensors::V2_0::implementation::IHalProxyCallback;
using ::android::hardware::sensors::V2_0::implementation::ISensorsSubHal;
using ::android::hardware::sensors::V2_0::implementation::ScopedWakelock;

// Wraps the multihal 2.0 sensors.ssc.so, which is what lunaa ships.
class SensorsSubHal : public ISensorsSubHal, public IHalProxyCallback {
  public:
    SensorsSubHal();

    // ISensors
    Return<Result> setOperationMode(OperationMode mode) override;
    Return<Result> activate(int32_t sensor_handle, bool enabled) override;
    Return<Result> batch(int32_t sensor_handle, int64_t sampling_period_ns,
                         int64_t max_report_latency_ns) override;
    Return<Result> flush(int32_t sensor_handle) override;
    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb callback) override;
    Return<Result> unregisterDirectChannel(int32_t channel_handle) override;
    Return<void> configDirectReport(int32_t sensor_handle, int32_t channel_handle, RateLevel rate,
                                    configDirectReport_cb callback) override;
    Return<void> getSensorsList(getSensorsList_cb callback) override;
    Return<Result> injectSensorData(const Event& event) override;

    // ISensorsSubHal
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    const std::string getName() override;
    Return<Result> initialize(const sp<IHalProxyCallback>& hal_proxy_callback) override;

    // ISensorsCallback
    Return<void> onDynamicSensorsConnected(const hidl_vec<SensorInfo>& sensor_infos) override;
    Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>& sensor_handles) override;

    // IHalProxyCallback
    void postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) override;
    ScopedWakelock createScopedWakelock(bool lock) override;

  private:
    std::unique_ptr<void, std::function<void(void*)>> lib_handle_;
    ISensorsSubHal* impl_;
    sp<IHalProxyCallback> hal_proxy_callback_;

    FusionLight fusion_light_;
    std::atomic_int32_t wise_light_handle_ = -1;
};

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
