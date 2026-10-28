#include "look_transform.hpp"
#include "lut_control.hpp"
#include "stable_file_change.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
    bool close(float actual, double expected, double tolerance = 3.0e-6)
    {
        return std::fabs(static_cast<double>(actual) - expected) <= tolerance;
    }

    double referenceChannel(double value, double contrast, double gamma, double gain)
    {
        contrast = std::clamp(contrast, 0.0, 1.0);
        gamma = std::clamp(gamma, 0.50, 2.50);
        gain = std::clamp(gain, 0.50, 1.50);
        const double color = std::pow(std::clamp(value * gain, 0.0, 1.0), 1.0 / gamma);
        const double smooth = color * color * (3.0 - 2.0 * color);
        return std::clamp(color * (1.0 - contrast) + smooth * contrast, 0.0, 1.0);
    }

    std::array<double, 3> referenceLook(const std::array<float, 3>& rgb,
                                       const std::array<float, 12>& values)
    {
        std::array<double, 3> result;
        for (std::size_t channel = 0; channel < result.size(); ++channel)
            result[channel] = referenceChannel(rgb[channel], values[channel], values[channel + 3], values[channel + 6]);

        const double luminance = result[0] * 0.2126 + result[1] * 0.7152 + result[2] * 0.0722;
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            const double saturation = std::clamp(static_cast<double>(values[channel + 9]), 0.0, 2.0);
            result[channel] = std::clamp(luminance + (result[channel] - luminance) * saturation, 0.0, 1.0);
        }
        return result;
    }

    bool matchesReference(const std::array<float, 3>& rgb, const std::array<float, 12>& values)
    {
        const auto actual = vkBasalt::applyLookTransform(rgb, values);
        const auto expected = referenceLook(rgb, values);
        for (std::size_t channel = 0; channel < actual.size(); ++channel)
            if (!close(actual[channel], expected[channel]))
                return false;
        return true;
    }

    bool testsStableFileChangeDetector()
    {
        using Detector = vkBasalt::StableFileChangeDetector;
        using Stamp = vkBasalt::FileStamp;
        using namespace std::chrono;

        const Stamp initial{std::filesystem::file_time_type{} + seconds(1), 100};
        const Stamp sizeChange{initial.modified, 120};
        const Stamp timeChange{initial.modified + seconds(1), 120};
        const auto start = Detector::Clock::now();
        Detector detector;
        detector.initialize(initial);

        if (detector.observe(sizeChange, start) ||
            detector.observe(sizeChange, start + milliseconds(199)) ||
            !detector.observe(sizeChange, start + milliseconds(200)) ||
            detector.observe(sizeChange, start + milliseconds(400)))
            return false;

        if (detector.observe(timeChange, start + milliseconds(500)) ||
            !detector.waitingForStableSample() ||
            detector.observe(sizeChange, start + milliseconds(550)) ||
            detector.waitingForStableSample())
            return false;

        if (detector.observe(timeChange, start + milliseconds(600)) ||
            detector.observe(timeChange, start + milliseconds(799)) ||
            !detector.observe(timeChange, start + milliseconds(800)) ||
            detector.observe(timeChange, start + milliseconds(1000)))
            return false;
        return true;
    }

    bool testsLutSelectionParsing()
    {
        const auto valid = vkBasalt::parseLutSelection("enabled 1\nfile q2f-c-gamma.cube\n");
        if (!valid.valid || !valid.enabled || valid.file != "q2f-c-gamma.cube")
            return false;
        const auto empty = vkBasalt::parseLutSelection("enabled 0\n");
        if (!empty.valid || empty.enabled || !empty.file.empty())
            return false;
        for (const char* invalid : {"enabled 1\nfile ../escape.cube\n",
                                    "enabled 1\nfile folder\\escape.cube\n",
                                    "enabled 1\nfile /tmp/escape.cube\n",
                                    "enabled 1\nfile .\n",
                                    "enabled 1\nfile ..\n",
                                    "enabled 1\nfile \n"})
            if (vkBasalt::parseLutSelection(invalid).valid)
                return false;
        return true;
    }

    bool testsLutResampling()
    {
        const auto identity = vkBasalt::makeIdentityLutCube(5);
        const auto resampledIdentity = vkBasalt::resampleLutCube(identity, 33);
        if (resampledIdentity.size != 33 || resampledIdentity.values.size() != 33u * 33u * 33u)
            return false;
        for (int blue = 0; blue < 33; ++blue)
            for (int green = 0; green < 33; ++green)
                for (int red = 0; red < 33; ++red)
                {
                    const auto& value = resampledIdentity.values[(blue * 33 + green) * 33 + red];
                    if (!close(value[0], red / 32.0, 1.0e-6) ||
                        !close(value[1], green / 32.0, 1.0e-6) ||
                        !close(value[2], blue / 32.0, 1.0e-6))
                        return false;
                }

        const auto twoCube = vkBasalt::makeIdentityLutCube(2);
        const auto resampled = vkBasalt::resampleLutCube(twoCube, 33);
        for (int blue : {0, 32})
            for (int green : {0, 32})
                for (int red : {0, 32})
                {
                    const auto& value = resampled.values[(blue * 33 + green) * 33 + red];
                    if (value != std::array<float, 3>{{red == 0 ? 0.0f : 1.0f,
                                                       green == 0 ? 0.0f : 1.0f,
                                                       blue == 0 ? 0.0f : 1.0f}})
                        return false;
                }
        return true;
    }

    bool testsLutStatusFormatting()
    {
        const vkBasalt::LutStatus status{vkBasalt::LutStatusState::Active, "q2f-c-gamma.cube", "ready"};
        const std::string formatted = vkBasalt::formatLutStatus(status);
        if (formatted != "state active\nfile q2f-c-gamma.cube\nmessage ready\n")
            return false;
        vkBasalt::LutStatus parsed;
        if (!vkBasalt::parseLutStatus(formatted, parsed) || parsed != status)
            return false;
        return vkBasalt::formatLutStatus({vkBasalt::LutStatusState::Error, "../bad.cube", "bad\npath"}) ==
               "state error\nfile -\nmessage bad?path\n";
    }
}

int main()
{
    const std::array<float, 12> neutral{{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    const std::array<float, 3> source{{0.23f, 0.61f, 0.87f}};
    if (!vkBasalt::isNeutralLook(neutral) || vkBasalt::applyLookTransform(source, neutral) != source)
    {
        std::fputs("neutral look was not an exact identity\n", stderr);
        return 1;
    }

    const std::array<float, 12> varied{{0.15f, 0.75f, 0.42f, 0.65f, 1.30f, 2.20f,
                                        1.40f, 0.72f, 1.15f, 0.30f, 1.40f, 1.80f}};
    if (vkBasalt::isNeutralLook(varied) || !matchesReference(source, varied))
    {
        std::fputs("per-channel look did not match the reference formulas\n", stderr);
        return 1;
    }

    const std::array<float, 12> outsideRange{{-4.0f, 3.0f, 0.55f, 0.1f, 4.0f, 1.7f,
                                              0.1f, 3.0f, 1.5f, -2.0f, 4.0f, 0.4f}};
    const auto bounded = vkBasalt::applyLookTransform({{1.4f, -0.2f, 0.63f}}, outsideRange);
    if (!matchesReference({{1.4f, -0.2f, 0.63f}}, outsideRange))
    {
        std::fputs("clamped look did not match the reference formulas\n", stderr);
        return 1;
    }
    for (float channel : bounded)
        if (!std::isfinite(channel) || channel < 0.0f || channel > 1.0f)
        {
            std::fputs("look output escaped the 0..1 range\n", stderr);
            return 1;
        }

    if (!testsStableFileChangeDetector())
    {
        std::fputs("stable file change detector did not wait for a second matching sample\n", stderr);
        return 1;
    }

    if (!testsLutSelectionParsing())
    {
        std::fputs("LUT selection parser accepted an unsafe name or rejected a valid selection\n", stderr);
        return 1;
    }
    if (!testsLutResampling())
    {
        std::fputs("LUT resampling did not preserve identity and exact corners\n", stderr);
        return 1;
    }
    if (!testsLutStatusFormatting())
    {
        std::fputs("LUT status formatting did not round trip safely\n", stderr);
        return 1;
    }

    std::puts("look transform, LUT control, resampling, and stable file change tests passed");
    return 0;
}
