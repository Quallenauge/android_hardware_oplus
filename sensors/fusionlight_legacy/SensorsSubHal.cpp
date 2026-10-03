/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SensorsSubHal.h"

#include <dlfcn.h>

#include <android-base/logging.h>
#include <hardware/sensors.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace {

#ifdef BACKEND_SUBHAL_LIB_NAME
constexpr auto kLibName = BACKEND_SUBHAL_LIB_NAME;
#else
constexpr auto kLibName = "sensors.ssc.so";
#endif

}  // anonymous namespace

SensorsSubHal::SensorsSubHal()
    : lib_handle_(dlopen(kLibName, RTLD_NOW), [](void* handle) {
          if (handle != nullptr) {
              dlclose(handle);
          }
      }) {
    if (!lib_handle_) {
        LOG(FATAL) << "dlopen " << kLibName << " failed: " << dlerror();
    }

    const auto get_sub_hal = reinterpret_cast<ISensorsSubHal* (*)(uint32_t*)>(
            dlsym(lib_handle_.get(), "sensorsHalGetSubHal"));
    if (get_sub_hal == nullptr) {
        LOG(FATAL) << "sensorsHalGetSubHal is missing from " << kLibName;
    }
    uint32_t version;
    impl_ = get_sub_hal(&version);
    if (impl_ == nullptr) {
        LOG(FATAL) << "sensorsHalGetSubHal returned null";
    }
}

Return<Result> SensorsSubHal::setOperationMode(OperationMode mode) {
    return impl_->setOperationMode(mode);
}

Return<Result> SensorsSubHal::activate(int32_t sensor_handle, bool enabled) {
    auto result = impl_->activate(sensor_handle, enabled);
    if (result.isOk() && result == Result::OK && sensor_handle == wise_light_handle_) {
        fusion_light_.setEnabled(enabled);
    }
    return result;
}

Return<Result> SensorsSubHal::batch(int32_t sensor_handle, int64_t sampling_period_ns,
                                    int64_t max_report_latency_ns) {
    return impl_->batch(sensor_handle, sampling_period_ns, max_report_latency_ns);
}

Return<Result> SensorsSubHal::flush(int32_t sensor_handle) {
    return impl_->flush(sensor_handle);
}

Return<void> SensorsSubHal::registerDirectChannel(const SharedMemInfo& mem,
                                                  registerDirectChannel_cb callback) {
    return impl_->registerDirectChannel(mem, callback);
}

Return<Result> SensorsSubHal::unregisterDirectChannel(int32_t channel_handle) {
    return impl_->unregisterDirectChannel(channel_handle);
}

Return<void> SensorsSubHal::configDirectReport(int32_t sensor_handle, int32_t channel_handle,
                                               RateLevel rate, configDirectReport_cb callback) {
    return impl_->configDirectReport(sensor_handle, channel_handle, rate, callback);
}

Return<void> SensorsSubHal::getSensorsList(getSensorsList_cb callback) {
    return impl_->getSensorsList([&](const auto& source) {
        // Publish the corrected wise light sensor as the standard light sensor, so the framework
        // and apps use it without knowing its vendor specific type.
        hidl_vec<SensorInfo> sensors = source;
        for (auto& sensor : sensors) {
            if (static_cast<int32_t>(sensor.type) != kWiseLightSensorType) {
                continue;
            }
            if (wise_light_handle_ < 0) {
                fusion_light_.initialize(sensor.name);
                wise_light_handle_ = sensor.sensorHandle;
            }
            sensor.type = SensorType::LIGHT;
            sensor.typeAsString = SENSOR_STRING_TYPE_LIGHT;
            break;
        }
        callback(sensors);
    });
}

Return<Result> SensorsSubHal::injectSensorData(const Event& event) {
    return impl_->injectSensorData(event);
}

Return<void> SensorsSubHal::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) {
    return impl_->debug(fd, args);
}

const std::string SensorsSubHal::getName() {
    return impl_->getName();
}

Return<Result> SensorsSubHal::initialize(const sp<IHalProxyCallback>& hal_proxy_callback) {
    hal_proxy_callback_ = hal_proxy_callback;
    return impl_->initialize(this);
}

Return<void> SensorsSubHal::onDynamicSensorsConnected(const hidl_vec<SensorInfo>& sensor_infos) {
    return hal_proxy_callback_->onDynamicSensorsConnected(sensor_infos);
}

Return<void> SensorsSubHal::onDynamicSensorsDisconnected(const hidl_vec<int32_t>& sensor_handles) {
    return hal_proxy_callback_->onDynamicSensorsDisconnected(sensor_handles);
}

void SensorsSubHal::postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) {
    const int32_t wise_light_handle = wise_light_handle_;
    if (wise_light_handle < 0) {
        hal_proxy_callback_->postEvents(events, std::move(wakelock));
        return;
    }

    std::vector<Event> output;
    output.reserve(events.size());
    for (const auto& event : events) {
        if (event.sensorHandle != wise_light_handle ||
            static_cast<int32_t>(event.sensorType) != kWiseLightSensorType) {
            output.push_back(event);
            continue;
        }
        Event corrected = event;
        corrected.sensorType = SensorType::LIGHT;
        if (fusion_light_.process(corrected)) {
            output.push_back(std::move(corrected));
        }
    }
    if (!output.empty()) {
        hal_proxy_callback_->postEvents(output, std::move(wakelock));
    }
}

ScopedWakelock SensorsSubHal::createScopedWakelock(bool lock) {
    return hal_proxy_callback_->createScopedWakelock(lock);
}

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

ISensorsSubHal* sensorsHalGetSubHal(uint32_t* version) {
    static ::android::hardware::sensors::V2_0::subhal::implementation::fusionlight_legacy::
            SensorsSubHal sub_hal;
    *version = SUB_HAL_2_0_VERSION;
    return &sub_hal;
}
