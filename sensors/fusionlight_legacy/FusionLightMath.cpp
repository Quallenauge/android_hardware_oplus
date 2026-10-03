/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "FusionLightMath.h"

#include <algorithm>
#include <cmath>

#include <android-base/logging.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace subhal {
namespace implementation {
namespace fusionlight_legacy {
namespace {

float Leakage(const ChannelArgs& channel, float value, float scale) {
    const double x = value;
    return static_cast<float>(
            (std::pow(x, 3.0) * channel.coefficients[0] + x * x * channel.coefficients[1] +
             static_cast<double>(channel.coefficients[2] * value) + channel.offset) *
            scale);
}

float Quadratic(float a, float b, float c, float x) {
    return static_cast<float>(static_cast<double>(x) * x * a + static_cast<double>(b * x) + c);
}

float Cubic(float a, float b, float c, float d, float x) {
    return static_cast<float>(std::pow(static_cast<double>(x), 3.0) * a +
                              static_cast<double>(x) * x * b + static_cast<double>(c * x) + d);
}

}  // anonymous namespace

PanelLeakage LeakageAtFullBrightness(const FusionLightArgs& args, const ScreenColor& color) {
    const float red = color.red;
    const float green = color.green;
    const float blue = color.blue;
    PanelLeakage channels;
    channels.red = std::max(Leakage(args.r, red, args.r.max / args.r.profile_max), 0.0f);
    channels.green = std::max(Leakage(args.g, green, args.g.max / args.g.profile_max), 0.0f);
    channels.blue = std::max(Leakage(args.b, blue, args.b.max / args.b.profile_max), 0.0f);
    const float grey =
            args.grey_weights[0] * red + args.grey_weights[1] * green + args.grey_weights[2] * blue;
    channels.overlap = Leakage(
            args.w, grey, (args.r.max + args.g.max + args.b.max - args.w.max) / args.w.profile_max);
    return channels;
}

float LeakageAtBrightness(const FusionLightArgs& args, PanelLeakage channels, int32_t level,
                          int32_t max_level, const ScreenColor& color, int32_t dim_alpha) {
    const auto& p = args.brightness_curve.params;
    const auto& points = args.brightness_curve.points;
    const float l = level;
    const auto alpha = static_cast<float>(static_cast<double>(level) * level * p[0] +
                                          static_cast<double>(level) * p[1] + p[2]);
    const float leakage = channels.total();
    const float scaled = leakage * level / static_cast<float>(max_level);
    // Some functions of a profile depend on the manufacturer of the panel. It is not known
    // here, 0 selects the behaviour without such a distinction.
    constexpr int32_t kLcdManufacture = 0;

    switch (args.brightness_curve.type) {
        case 1:
            return (points[0] < l && l <= points[1] ? alpha : 1.0f) * scaled;
        case 2:
            if (points[0] > l || points[1] < l) {
                return scaled;
            }
            channels.overlap *= alpha;
            return (channels.red + channels.green + channels.blue - channels.overlap) * l /
                   max_level;
        case 3:
            if (points[0] >= l) {
                channels.red *= p[0];
                channels.overlap *= p[1];
            }
            return (channels.red + channels.green + channels.blue - channels.overlap) * l /
                   max_level;
        case 4:
            if (color.red < args.brightness_curve_color_limits[0] &&
                color.green < args.brightness_curve_color_limits[1] &&
                color.blue < args.brightness_curve_color_limits[2]) {
                return 0.0f;
            }
            if (kLcdManufacture != 1 || points[0] <= l || dim_alpha != 0) {
                return scaled;
            }
            return std::max(scaled, 0.0f);
        case 5:
            if (points[0] >= l) {
                return scaled;
            }
            return (kLcdManufacture == 1 && points[1] >= l ? alpha : 1.0f) * scaled;
        case 6:
        case 7: {
            float x = l;
            float coef;
            if (points[0] > l) {
                x = static_cast<int32_t>(points[0]);
                coef = p[0] * x * x + p[1] * x + p[2];
            } else if (points[1] <= l) {
                coef = p[3] * x * x + p[4] * x + p[5];
            } else {
                coef = p[0] * x * x + p[1] * x + p[2];
            }
            coef /= 1000.0f;
            return args.brightness_curve.type == 6 ? coef * (leakage * x / max_level)
                                                   : coef * leakage;
        }
        case 8: {
            if (kLcdManufacture != 2) {
                return scaled;
            }
            if (level < 1) {
                return 0.0f;
            }
            const float coef =
                    points[0] >= l
                            ? static_cast<float>(std::pow(static_cast<double>(l), p[1]) * p[0])
                            : static_cast<float>(
                                      std::max(std::pow(static_cast<double>(l), p[3]) * p[2], 1.0));
            return leakage * coef * l / max_level;
        }
        case 9:
        case 11: {
            if (level < 1) {
                return 0.0f;
            }
            double coef;
            if (points[0] >= l) {
                coef = std::pow(static_cast<double>(l), p[1]) * p[0];
            } else if (points[1] < l) {
                coef = std::pow(static_cast<double>(l), p[5]) * p[4];
            } else {
                coef = std::pow(static_cast<double>(l), p[3]) * p[2];
            }
            if (args.brightness_curve.type == 9) {
                return leakage * static_cast<float>(coef) * l / max_level;
            }
            return leakage * static_cast<float>(coef / 1000.0);
        }
        case 10: {
            if (level < 1) {
                return 0.0f;
            }
            float coef;
            if (points[0] >= l) {
                coef = static_cast<float>(
                        std::pow(static_cast<double>(l), args.brightness_curve_color_limits[1]) *
                        args.brightness_curve_color_limits[0]);
            } else if (points[1] < l) {
                coef = (p[3] * l * l + p[4] * l + p[5]) / 1000.0f;
            } else {
                coef = (p[0] * l * l + p[1] * l + p[2]) / 1000.0f;
            }
            return coef * leakage * l / max_level;
        }
        case 0:
            return scaled;
        default:
            LOG(WARNING) << "Unsupported PWM function type " << args.brightness_curve.type;
            return scaled;
    }
}

float LuxPerRaw(const FusionLightArgs& args, float raw_lux, float c, float g) {
    float ret;
    switch (args.clear_measure) {
        case 3:
            ret = g >= 0.25f ? c * 1000.0f / g : c * 1000.0f * 4.0f;
            break;
        case 2:
            ret = c / 1000.0f;
            break;
        case 1:
            ret = c > 0.0f ? raw_lux / c : c;
            break;
        default:
            ret = c;
            break;
    }

    const auto& points = args.clear_thresholds;
    const auto& v = args.lux_factor_lines;
    const auto& rou = args.lux_per_raw;
    switch (args.lux_factor_segments) {
        case 4:
            for (size_t i = 0; i < 3; ++i) {
                if (ret <= points[i]) {
                    return v[i][0] * ret + v[i][1];
                }
            }
            return v[3][0] * ret + v[3][1];
        case 6:
            if (raw_lux <= 1000.0f) {
                return rou[0];
            }
            [[fallthrough]];
        case 5:
            for (size_t i = 0; i < 4; ++i) {
                if (ret <= points[i]) {
                    return v[i][0] * ret + v[i][1];
                }
            }
            return v[4][0] * ret + v[4][1];
        case 7:
            if (ret < points[0]) {
                return rou[0];
            }
            if (ret < points[1]) {
                const double x = (v[0][0] + ret) / v[0][1];
                const double poly = std::pow(x, 3.0) * v[2][1] + x * x * v[2][0] +
                                    static_cast<float>(v[1][0] + v[1][1] * static_cast<float>(x));
                return rou[0] * static_cast<float>(poly) / v[3][0];
            }
            return ret < points[2] ? rou[1] : ret < points[3] ? rou[2] : rou[3];
        case 8:
            if (ret < points[0]) {
                return args.lux_per_raw_override;
            }
            if (ret < points[1]) {
                return v[0][0];
            }
            return ret < points[2] ? rou[1] : ret < points[3] ? rou[2] : rou[3];
        default:
            return rou[0];
    }
}

float LevelBehindDimLayer(const FusionLightArgs& args, float level, int32_t max_level,
                          int32_t dim_alpha) {
    if (level == 0.0f || dim_alpha == 0) {
        return level;
    }
    float result = level;
    const auto& p = args.fingerprint_dim.params;
    const auto& points = args.fingerprint_dim.points;
    const float l = level;
    const double a = dim_alpha;
    float coef = 1.0f;
    bool scale_level = true;
    switch (args.fingerprint_dim.type) {
        case 0:
            coef = p[0] * std::pow(l, p[1]);
            break;
        case 1:
            coef = Quadratic(p[0], p[1], p[2], l);
            break;
        case 2:
            coef = Cubic(p[0], p[1], p[2], p[3], l);
            break;
        case 3:
            coef = p[0];
            break;
        case 4:
            result = max_level * static_cast<float>((a * a * p[1] + p[2] * dim_alpha + p[3]) *
                                                    (args.w.max / p[0]));
            scale_level = false;
            break;
        case 5:
            coef = points[0] >= l ? p[2] * std::pow(l, p[3]) : p[0] * std::pow(l, p[1]);
            break;
        case 6:
            coef = points[0] < l ? Quadratic(p[0], p[1], p[2], l) : Quadratic(p[3], p[4], p[5], l);
            break;
        case 7:
            coef = points[0] > l   ? p[0] * std::pow(l, p[1])
                   : points[1] > l ? p[2] * std::pow(l, p[3])
                                   : p[4] * std::pow(l, p[5]);
            break;
        case 8:
            result = max_level * static_cast<float>(a * a * p[0] + p[1] * dim_alpha + p[2]);
            scale_level = false;
            break;
        case 9:
            coef = points[0] < l ? Quadratic(p[0], p[1], p[2], l) : p[3] * std::pow(l, p[4]);
            break;
        case 10:
            if (points[0] >= l) {
                coef = p[0];
            } else if (points[1] < l) {
                coef = p[5];
            } else {
                const double x = l * 0.1;
                coef = static_cast<float>(std::pow(x, 3.0) * p[1] + x * x * p[2] + p[3] * 0.1 * l +
                                          p[4]);
            }
            break;
        case 11:
            result = p[4] * max_level *
                     static_cast<float>((a * a * p[0] + p[1] * dim_alpha + p[2]) / p[3]);
            scale_level = false;
            break;
        case 12:
            if (points[0] > l) {
                result = p[4] * l * std::pow(l, p[5]);
            }
            scale_level = false;
            break;
        default:
            break;
    }
    if (scale_level) {
        result = coef * l;
    }
    return result;
}

float LevelWithDcDimming(const FusionLightArgs& args, float level, int32_t dc_alpha) {
    if (level == 0.0f || dc_alpha == 0 || !(level < args.dc_dim_max_level)) {
        return level;
    }

    const auto& p = args.dc_dim.params;
    const auto& points = args.dc_dim.points;
    const float l = level;
    // Some functions of a profile depend on the manufacturer of the panel. It is not known
    // here, 0 selects the behaviour without such a distinction.
    constexpr int32_t kLcdManufacture = 0;
    float coef;
    switch (args.dc_dim.type) {
        case 0:
            coef = p[0] * std::pow(l, p[1]);
            break;
        case 1:
        case 8:
            coef = Quadratic(p[0], p[1], p[2], l);
            break;
        case 2:
            coef = Cubic(p[0], p[1], p[2], p[3], l);
            break;
        case 3:
            coef = p[0];
            break;
        case 4:
            coef = static_cast<float>(
                    (static_cast<double>(l) * l * p[1] + static_cast<double>(p[2] * l) + p[3]) *
                    (args.w.max / p[0]));
            break;
        case 5:
            coef = points[0] < l ? p[0] * std::pow(l, p[1]) : p[2] * std::pow(l, p[3]);
            break;
        case 6:
            coef = points[0] < l ? Quadratic(p[0], p[1], p[2], l) : Quadratic(p[3], p[4], p[5], l);
            break;
        case 7:
            coef = points[0] > l   ? p[0] * std::pow(l, p[1])
                   : points[1] > l ? p[2] * std::pow(l, p[3])
                                   : p[4] * std::pow(l, p[5]);
            break;
        case 9:
            coef = kLcdManufacture == 1 && points[0] <= l && l <= points[1]
                           ? Cubic(p[0], p[1], p[2], p[3], l)
                           : 1.0f;
            break;
        case 10:
            coef = points[0] >= l ? p[3] * std::pow(l, p[4]) : p[0] * std::pow(l, p[1]) + p[2];
            break;
        case 11:
            coef = points[0] > l || points[1] <= l ? p[2] : p[0] * l + p[1];
            break;
        case 12:
            coef = points[0] > l    ? p[0] * std::pow(l, p[1])
                   : points[1] <= l ? p[4] * l + p[5]
                                    : p[2] * l + p[3];
            break;
        case 13:
            coef = points[0] < l ? Quadratic(p[0], p[1], p[2], l) : p[3] * std::pow(l, p[4]);
            break;
        case 14:
            coef = static_cast<float>(
                    1.0 / (points[0] >= l ? static_cast<double>(l) * l * p[3] +
                                                    static_cast<double>(p[4] * l) + p[5]
                                          : static_cast<double>(l) * l * p[0] * 0.0001 +
                                                    static_cast<double>(p[1] * l) + p[2]));
            break;
        default:
            coef = 1.0f;
            break;
    }
    return coef * l;
}

}  // namespace fusionlight_legacy
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
