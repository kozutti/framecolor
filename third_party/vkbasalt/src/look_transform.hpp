#ifndef LOOK_TRANSFORM_HPP_INCLUDED
#define LOOK_TRANSFORM_HPP_INCLUDED

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace vkBasalt
{
    inline bool isNeutralLook(const std::array<float, 12>& values)
    {
        return values[0] == 0.0f && values[1] == 0.0f && values[2] == 0.0f &&
               values[3] == 1.0f && values[4] == 1.0f && values[5] == 1.0f &&
               values[6] == 1.0f && values[7] == 1.0f && values[8] == 1.0f &&
               values[9] == 1.0f && values[10] == 1.0f && values[11] == 1.0f;
    }

    inline float transformLookChannel(float value, float contrast, float gamma, float gain)
    {
        contrast = std::clamp(contrast, 0.0f, 1.0f);
        gamma = std::clamp(gamma, 0.50f, 2.50f);
        gain = std::clamp(gain, 0.50f, 1.50f);
        const float color = std::pow(std::clamp(value * gain, 0.0f, 1.0f), 1.0f / gamma);
        const float smooth = color * color * (3.0f - 2.0f * color);
        return std::clamp(color * (1.0f - contrast) + smooth * contrast, 0.0f, 1.0f);
    }

    inline std::array<float, 3> applyLookTransform(const std::array<float, 3>& rgb,
                                                    const std::array<float, 12>& values)
    {
        if (isNeutralLook(values))
            return rgb;

        std::array<float, 3> corrected;
        for (std::size_t channel = 0; channel < corrected.size(); ++channel)
            corrected[channel] = transformLookChannel(rgb[channel], values[channel],
                                                        values[channel + 3], values[channel + 6]);

        const std::array<float, 3> saturation{{std::clamp(values[9], 0.0f, 2.0f),
                                                std::clamp(values[10], 0.0f, 2.0f),
                                                std::clamp(values[11], 0.0f, 2.0f)}};
        if (saturation == std::array<float, 3>{{1.0f, 1.0f, 1.0f}})
            return corrected;

        const float luminance = corrected[0] * 0.2126f + corrected[1] * 0.7152f + corrected[2] * 0.0722f;
        for (std::size_t channel = 0; channel < corrected.size(); ++channel)
            corrected[channel] = std::clamp(luminance + (corrected[channel] - luminance) * saturation[channel],
                                            0.0f, 1.0f);
        return corrected;
    }
} // namespace vkBasalt

#endif // LOOK_TRANSFORM_HPP_INCLUDED
