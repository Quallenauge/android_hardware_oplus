/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "PanelState.h"

#include <fcntl.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <oplus/oplus_display_panel.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace {

constexpr auto kOplusDisplayDevice = "/dev/oplus_display";
constexpr auto kFingerprintStateNode = "/sys/kernel/oplus_display/fp_state";

}  // anonymous namespace

int32_t PanelState::get(unsigned long request) {
    // The sensors HAL starts before the node is ready, so open it on first use.
    if (fd_ < 0) {
        fd_.reset(open(kOplusDisplayDevice, O_RDWR | O_CLOEXEC));
        if (fd_ < 0) {
            PLOG(ERROR) << "Unable to open " << kOplusDisplayDevice;
            return 0;
        }
    }
    unsigned int value = 0;
    if (ioctl(fd_.get(), request, &value) != 0) {
        PLOG(ERROR) << "ioctl " << request << " failed";
        return 0;
    }
    return static_cast<int32_t>(value);
}

int32_t PanelState::brightness() {
    return get(PANEL_IOCTL_GET_OPLUS_BRIGHTNESS);
}

int32_t PanelState::dimAlpha() {
    return get(PANEL_IOCTL_GET_DIM_ALPHA);
}

int32_t PanelState::dimDcAlpha() {
    return get(PANEL_IOCTL_GET_DIM_DC_ALPHA);
}

bool PanelState::fingerprintPressed() {
    std::string state;
    if (!android::base::ReadFileToString(kFingerprintStateNode, &state)) {
        return false;
    }
    // Formatted as "x,y,touch_state".
    const auto fields = android::base::Split(android::base::Trim(state), ",");
    return fields.size() == 3 && fields[2] == "1";
}

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
