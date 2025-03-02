/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "OPlusSensorPropsShim"

#include <aidl/android/hardware/biometrics/fingerprint/SensorProps.h>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include <dlfcn.h>

// Display info headers
#include <gui/SurfaceComposerClient.h>
#include <ui/DisplayInfo.h>

using android::DisplayInfo;
using android::IBinder;
using android::sp;
using android::status_t;
using android::SurfaceComposerClient;
using android::base::GetProperty;
using android::base::ParseInt;
using android::base::Tokenize;

using aidl::android::hardware::biometrics::fingerprint::FingerprintSensorType;
using aidl::android::hardware::biometrics::fingerprint::SensorProps;

namespace {

void getScreenDimensions(int32_t* width, int32_t* height) {
    sp<IBinder> display = SurfaceComposerClient::getInternalDisplayToken();
    if (display == nullptr) {
        LOG(ERROR) << "Failed to get display token";
        return;
    }

    DisplayInfo info;
    status_t status = SurfaceComposerClient::getDisplayInfo(display, &info);
    if (status) {
        LOG(ERROR) << "Failed to get display info: " << status;
        return;
    }

    *width = info.w;
    *height = info.h;

    if (info.orientation == DISPLAY_ORIENTATION_90 || info.orientation == DISPLAY_ORIENTATION_270) {
        std::swap(*width, *height);
    }

    LOG(DEBUG) << "Display info: width=" << *width << ", height=" << *height
               << ", orientation=" << info.orientation;
}

bool isWithinBounds(int32_t x, int32_t y, int32_t width, int32_t height) {
    return (x >= 0 && x <= width && y >= 0 && y <= height);
}

SensorProps SensorPropsInit(SensorProps props) {
    auto type = GetProperty("persist.vendor.fingerprint.sensor_type", "");
    if (!type.empty()) {
        if (type == "back")
            props.sensorType = FingerprintSensorType::REAR;
        else if (type == "ultrasonic")
            props.sensorType = FingerprintSensorType::UNDER_DISPLAY_ULTRASONIC;
        else if (type == "optical")
            props.sensorType = FingerprintSensorType::UNDER_DISPLAY_OPTICAL;
        else if (type == "side")
            props.sensorType = FingerprintSensorType::POWER_BUTTON;
        else if (type == "front")
            props.sensorType = FingerprintSensorType::HOME_BUTTON;
    }

    int32_t width = 0, height = 0;
    getScreenDimensions(&width, &height);

    LOG(DEBUG) << "Screen dimensions: " << width << "x" << height;

    bool valid_location = false;
    auto loc_prop = GetProperty("persist.vendor.fingerprint.optical.sensorlocation", "");
    if (!loc_prop.empty()) {
        auto loc = Tokenize(loc_prop, ":");
        if (loc.size() >= 2) {
            int32_t x, y;
            if (ParseInt(loc[0], &x) && ParseInt(loc[1], &y)) {
                // Check if coordinates are within screen bounds
                if (isWithinBounds(x, y, width, height)) {
                    props.sensorLocations[0].sensorLocationX = x;
                    props.sensorLocations[0].sensorLocationY = y;
                    valid_location = true;
                    LOG(DEBUG) << "Using direct sensor location: " << x << ":" << y;
                } else {
                    LOG(WARNING) << "Sensor location out of bounds: " << x << ":" << y
                                 << " (screen: " << width << "x" << height << ")";
                }
            } else {
                LOG(WARNING) << "Invalid sensor location format: " << loc_prop;
            }
        }
    }

    if (!valid_location) {
        props.sensorLocations[0].sensorLocationX = width / 2;

        auto iconlocation = GetProperty("persist.vendor.fingerprint.optical.iconlocation", "");
        int32_t iconY = 0;
        if (!iconlocation.empty() && ParseInt(iconlocation, &iconY)) {
            props.sensorLocations[0].sensorLocationY = height - iconY;
            LOG(DEBUG) << "Calculated Y location: height (" << height << ") - iconlocation ("
                       << iconY << ") = " << props.sensorLocations[0].sensorLocationY;
        } else {
            props.sensorLocations[0].sensorLocationY = height * 3 / 4;  // 75% down the screen
            LOG(WARNING) << "No iconlocation property, using default Y location: "
                         << props.sensorLocations[0].sensorLocationY;
        }
    }

    auto size = GetProperty("persist.vendor.fingerprint.optical.iconsize", "");
    if (!size.empty()) {
        if (ParseInt(size, &props.sensorLocations[0].sensorRadius)) {
            props.sensorLocations[0].sensorRadius /= 2;
            LOG(DEBUG) << "Setting radius to " << props.sensorLocations[0].sensorRadius
                       << " (half of iconsize: " << size << ")";
        } else {
            LOG(WARNING) << "Invalid sensor size input: " << size;
        }
    }

    LOG(DEBUG) << "Final sensor properties: location=(" << props.sensorLocations[0].sensorLocationX
               << ", " << props.sensorLocations[0].sensorLocationY
               << "), radius=" << props.sensorLocations[0].sensorRadius;

    return props;
}
}  // anonymous namespace

extern "C" void
_ZNK4aidl7android8hardware10biometrics11fingerprint11SensorProps13writeToParcelEP7AParcel(
        SensorProps* thisptr, AParcel* parcel) {
    static auto props = SensorPropsInit(*thisptr);
    static auto writeToParcel = reinterpret_cast<
            typeof(_ZNK4aidl7android8hardware10biometrics11fingerprint11SensorProps13writeToParcelEP7AParcel)*>(
            dlsym(RTLD_NEXT, __func__));

    LOG(DEBUG) << "Original props=" << thisptr->toString() << ", new props=" << props.toString();
    writeToParcel(&props, parcel);
}
