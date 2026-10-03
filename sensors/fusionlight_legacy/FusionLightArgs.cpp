/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "FusionLightArgs.h"

#include <cstdlib>
#include <sstream>

#include <aidl/vendor/oplus/hardware/oplusSensor/ISensorFeature.h>
#include <android-base/logging.h>
#include <android-base/parsedouble.h>
#include <android-base/strings.h>
#include <android/binder_manager.h>
#include <json/json.h>
#include <tinyxml2.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace {

using aidl::vendor::oplus::hardware::oplusSensor::ISensorFeature;
using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

constexpr auto kProfilePath = "/odm/etc/fusionlight_profile/oplus_fusion_light_args.xml";
constexpr auto kStk32600ProfilePath = "/odm/etc/fusionlight_profile/oplus_fusion_light_args_2.xml";

float ToFloat(const XMLElement* element) {
    const char* text = element->GetText();
    return text == nullptr ? 0.0f : strtof(text, nullptr);
}

int32_t ToInt(const XMLElement* element) {
    const char* text = element->GetText();
    return text == nullptr ? 0 : atoi(text);
}

// Profiles are read by position: inside a group the order of the nodes matters, not their names.
template <typename Callback>
void ForEachChild(const XMLElement* parent, size_t limit, Callback callback) {
    const XMLElement* child = parent == nullptr ? nullptr : parent->FirstChildElement();
    for (size_t index = 0; child != nullptr && index < limit; ++index) {
        callback(index, child);
        child = child->NextSiblingElement();
    }
}

void LoadChannel(const XMLElement* args, const char* name, ChannelArgs& channel) {
    ForEachChild(args->FirstChildElement(name), 6, [&](size_t index, const XMLElement* element) {
        const float value = ToFloat(element);
        switch (index) {
            case 0:
                channel.max = value;
                break;
            case 1:
                channel.profile_max = value;
                break;
            case 5:
                channel.offset = value;
                break;
            default:
                channel.coefficients[index - 2] = value;
                break;
        }
    });
}

void LoadGrayAndCal(const XMLElement* args, FusionLightArgs& result) {
    const XMLElement* gray = args->FirstChildElement("Gray");
    if (gray == nullptr) {
        LOG(WARNING) << "Missing Gray args";
        return;
    }
    ForEachChild(gray, 3, [&](size_t index, const XMLElement* element) {
        result.grey_weights[index] = ToFloat(element);
    });
    ForEachChild(gray->NextSiblingElement(), 6, [&](size_t index, const XMLElement* element) {
        const float value = ToFloat(element);
        if (index == 0) {
            result.raw_per_level = value;
        } else if (index == 5) {
            result.calibration_factor = value;
        } else {
            result.lux_per_raw[index - 1] = value;
        }
    });
}

void LoadArrays(const XMLElement* args, FusionLightArgs& result) {
    const XMLElement* raw_arrays = args->FirstChildElement("RawArrays");
    if (raw_arrays == nullptr) {
        LOG(WARNING) << "Missing RawArrays args";
        return;
    }
    LuxRanges& tables = result.lux_range_count == 15 ? result.lux_ranges_15 : result.lux_ranges_9;
    const size_t length = tables.window_upper.size();
    std::vector<float>* float_tables[] = {&tables.window_upper, &tables.window_lower,
                                          &tables.raw_at_level, &tables.window_upper_lux,
                                          &tables.window_lower_lux};
    ForEachChild(raw_arrays, std::size(float_tables), [&](size_t table, const XMLElement* node) {
        ForEachChild(node, length, [&](size_t index, const XMLElement* element) {
            (*float_tables[table])[index] = ToFloat(element);
        });
    });

    std::vector<int32_t>* int_tables[] = {&tables.level, &tables.lux_limit};
    ForEachChild(raw_arrays->NextSiblingElement(), std::size(int_tables),
                 [&](size_t table, const XMLElement* node) {
                     ForEachChild(node, length, [&](size_t index, const XMLElement* element) {
                         (*int_tables[table])[index] = ToInt(element);
                     });
                 });
}

void LoadSeparateLux(const XMLElement* args, FusionLightArgs& result) {
    const XMLElement* node = args->FirstChildElement("SeperateLux");
    if (node == nullptr) {
        LOG(WARNING) << "Missing SeperateLux args";
        return;
    }
    // Stops at the first missing entry.
    const XMLElement* element;
    if ((element = node->FirstChildElement("RetType")) == nullptr) return;
    result.clear_measure = ToInt(element);
    if ((element = node->FirstChildElement("ParagraphCount")) == nullptr) return;
    result.lux_factor_segments = ToInt(element);
    for (size_t i = 0; i < result.clear_thresholds.size(); ++i) {
        const std::string name = "SeperatePoint" + std::to_string(i + 1);
        if ((element = node->FirstChildElement(name.c_str())) == nullptr) return;
        result.clear_thresholds[i] = ToFloat(element);
    }
    for (size_t i = 0; i < result.lux_factor_lines.size(); ++i) {
        for (size_t j = 0; j < 2; ++j) {
            const std::string name = "SP" + std::to_string(i + 1) + "value" + std::to_string(j + 1);
            if ((element = node->FirstChildElement(name.c_str())) == nullptr) return;
            result.lux_factor_lines[i][j] = ToFloat(element);
        }
    }
    if ((element = node->FirstChildElement("SeperateLuxThreshold")) == nullptr) return;
    result.lux_factor_min_raw = ToInt(element);
}

void LoadFunction(const XMLElement* args, const char* name, FunctionArgs& function,
                  std::array<float, 3>* limits) {
    const size_t limit_count = limits == nullptr ? 0 : limits->size();
    ForEachChild(args->FirstChildElement(name), 11 + limit_count,
                 [&](size_t index, const XMLElement* element) {
                     if (index == 0) {
                         function.type = ToInt(element);
                     } else if (index == 1) {
                         function.segment_count = ToInt(element);
                     } else if (index < 5) {
                         function.points[index - 2] = ToFloat(element);
                     } else if (index < 5 + limit_count) {
                         (*limits)[index - 5] = ToFloat(element);
                     } else {
                         function.params[index - 5 - limit_count] = ToFloat(element);
                     }
                 });
}

void LoadSpecialCustom(const XMLElement* args, FusionLightArgs& result) {
    const XMLElement* node = args->FirstChildElement("SpecialCustom");
    if (node == nullptr) {
        LOG(WARNING) << "Missing SpecialCustom args";
        return;
    }
    int32_t ignored;
    int32_t* positional[] = {&result.report_bright_light, &result.report_low_light, &ignored,
                             &ignored, &result.use_moving_average};
    ForEachChild(node, std::size(positional), [&](size_t index, const XMLElement* element) {
        *positional[index] = ToInt(element);
    });

    const XMLElement* element;
    if ((element = node->FirstChildElement("raw_rou_coe_level_special_1")) != nullptr) {
        result.lux_per_raw_override = ToFloat(element);
    }
    if ((element = node->FirstChildElement("MaxLevel")) != nullptr) {
        const int32_t max_level = ToInt(element);
        if (max_level == 1023 || max_level == 2047 || max_level == 3515 || max_level == 4096) {
            result.max_level = max_level;
        } else {
            LOG(WARNING) << "Ignoring invalid MaxLevel " << max_level;
        }
    }
    if ((element = node->FirstChildElement("DCLevelMax")) != nullptr) {
        result.dc_dim_max_level = ToInt(element);
    }
    if ((element = node->FirstChildElement("ApolloLight")) != nullptr) {
        result.substitute_max_level = ToInt(element);
        if ((element = node->FirstChildElement("ApolloMaxBrightness")) != nullptr) {
            result.max_level_substitute = ToInt(element);
        }
    }
    if ((element = node->FirstChildElement("LowLightAccuracy")) != nullptr) {
        result.min_lux = ToFloat(element);
        if ((element = node->FirstChildElement("LowLightAccuracy1")) != nullptr) {
            result.min_lux_low_light = ToFloat(element);
        }
    }
    if ((element = node->FirstChildElement("BrightnessFromEvent")) != nullptr) {
        result.level_from_event = ToInt(element);
    }
    if ((element = node->FirstChildElement("DefinitionRouCalCoe")) != nullptr) {
        result.fixed_lux_factors = ToInt(element);
    }
    if ((element = node->FirstChildElement("MedianFilterQueue")) != nullptr) {
        result.use_median_filter = ToInt(element);
        if ((element = node->FirstChildElement("MedianFilterEnableLevel")) != nullptr) {
            result.median_filter_max_lux = ToInt(element);
        }
    }
}

void LoadProfile(const std::string& path, FusionLightArgs& result) {
    XMLDocument document;
    if (document.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
        LOG(WARNING) << "Unable to load " << path << ", using built-in defaults";
        return;
    }
    const XMLElement* root = document.FirstChildElement("Attributes");
    const XMLElement* project = root == nullptr ? nullptr : root->FirstChildElement("Project");
    if (project == nullptr || project->FirstChildElement("ProjectCode") == nullptr) {
        LOG(WARNING) << path << " has no project statement, using built-in defaults";
        return;
    }
    if (const XMLElement* level_max = project->FirstChildElement("LevelMax")) {
        result.lux_range_count = ToInt(level_max);
        if (result.lux_range_count != 9 && result.lux_range_count != 15) {
            LOG(WARNING) << "Illegal LevelMax " << result.lux_range_count << ", using 9";
            result.lux_range_count = 9;
        }
    }
    const XMLElement* args = root->FirstChildElement("Args");
    if (args == nullptr) {
        LOG(WARNING) << path << " has no Args, using built-in defaults";
        return;
    }

    LoadChannel(args, "R", result.r);
    LoadChannel(args, "G", result.g);
    LoadChannel(args, "B", result.b);
    LoadChannel(args, "W", result.w);
    LoadGrayAndCal(args, result);
    LoadArrays(args, result);
    LoadSeparateLux(args, result);
    LoadFunction(args, "FingerPrintFunction", result.fingerprint_dim, nullptr);
    LoadFunction(args, "DCFunction", result.dc_dim, nullptr);
    LoadFunction(args, "PWMFunction", result.brightness_curve,
                 &result.brightness_curve_color_limits);
    LoadSpecialCustom(args, result);
    LOG(INFO) << "Loaded " << path;
}

bool ReadCalibrationValue(const Json::Value& root, const char* key, float& value) {
    if (!root.isMember(key)) {
        return false;
    }
    double parsed;
    const Json::Value& member = root[key];
    if (member.isNumeric()) {
        parsed = member.asDouble();
    } else if (!member.isString() ||
               !android::base::ParseDouble(android::base::Trim(member.asString()), &parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

// Factory calibration of the light sensor as reported by ISensorFeature, empty if there is none.
std::string ReadCalibration() {
    const std::string instance = std::string(ISensorFeature::descriptor) + "/default";
    ndk::SpAIBinder binder(AServiceManager_checkService(instance.c_str()));
    const auto service = ISensorFeature::fromBinder(binder);
    if (service == nullptr) {
        LOG(WARNING) << "ISensorFeature is unavailable, using profile calibration";
        return "";
    }

    std::string response;
    const auto status = service->getSensorCalibrationData(kWiseLightSensorType, &response);
    if (!status.isOk()) {
        LOG(WARNING) << "getSensorCalibrationData failed: " << status.getDescription();
        return "";
    }
    if (response.empty() || response == "default") {
        LOG(INFO) << "No factory calibration, using profile calibration";
        return "";
    }
    return response;
}

}  // anonymous namespace

void LoadFusionLightProfile(const std::string& path, FusionLightArgs& result) {
    LoadProfile(path, result);
}

void ApplyCalibration(const std::string& response, FusionLightArgs& result) {
    if (response.empty()) {
        return;
    }

    Json::CharReaderBuilder builder;
    std::istringstream stream(response);
    Json::Value root;
    std::string error;
    if (!Json::parseFromStream(builder, stream, &root, &error)) {
        LOG(WARNING) << "Invalid factory calibration " << response << ": " << error;
        return;
    }

    ReadCalibrationValue(root, "R_MAX", result.r.max);
    ReadCalibrationValue(root, "G_MAX", result.g.max);
    ReadCalibrationValue(root, "B_MAX", result.b.max);
    ReadCalibrationValue(root, "W_MAX", result.w.max);
    if (float row_coe; ReadCalibrationValue(root, "row_coe", row_coe)) {
        result.lux_per_raw[0] = row_coe / 1000.0f;
    }
    ReadCalibrationValue(root, "cali_para", result.calibration_factor);
    if (result.fixed_lux_factors == 1) {
        result.lux_per_raw[0] = result.lux_per_raw_override;
        result.calibration_factor = 1000.0f;
    }
    LOG(INFO) << "Applied factory calibration " << response;
}

void DeriveLimits(FusionLightArgs& result) {
    result.raw_per_level = result.w.max / 1023.0f;
    const float raw_scale = result.calibration_factor * result.lux_per_raw[0];
    LuxRanges& tables = result.lux_range_count == 15 ? result.lux_ranges_15 : result.lux_ranges_9;
    for (size_t i = 0; i < tables.window_upper.size(); ++i) {
        tables.raw_at_level[i] = result.raw_per_level * tables.level[i];
        tables.window_upper[i] = tables.window_upper_lux[i] * 1000.0f / raw_scale;
        tables.window_lower[i] = tables.window_lower_lux[i] * 1000.0f / raw_scale;
    }
}

FusionLightArgs LoadFusionLightArgs(const std::string& sensor_name) {
    FusionLightArgs result;
    LoadProfile(
            sensor_name.find("stk32600") != std::string::npos ? kStk32600ProfilePath : kProfilePath,
            result);
    ApplyCalibration(ReadCalibration(), result);
    DeriveLimits(result);
    LOG(INFO) << "FusionLight args: RGBW max=[" << result.r.max << ", " << result.g.max << ", "
              << result.b.max << ", " << result.w.max << "] rou_coe=" << result.lux_per_raw[0]
              << " cal_coe=" << result.calibration_factor << " levels=" << result.lux_range_count
              << " max_level=" << result.max_level << " alpha=" << result.fingerprint_dim.type
              << " dc=" << result.dc_dim.type << " pwm=" << result.brightness_curve.type;
    return result;
}

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
