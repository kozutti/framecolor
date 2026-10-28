#include "effect_lut.hpp"

#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <cstdlib>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "image.hpp"
#include "lut_cube.hpp"
#include "look_transform.hpp"

#include "shader_sources.hpp"

namespace vkBasalt
{
    namespace
    {
        VkFormat selectExternalLutFormat(LogicalDevice* device)
        {
            VkFormatProperties properties{};
            device->vki.GetPhysicalDeviceFormatProperties(device->physicalDevice,
                VK_FORMAT_R16G16B16A16_UNORM, &properties);
            const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                  VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            return (properties.optimalTilingFeatures & required) == required
                ? VK_FORMAT_R16G16B16A16_UNORM
                : VK_FORMAT_R8G8B8A8_UNORM;
        }
    } // namespace

    LutEffect::LutEffect(LogicalDevice*       pLogicalDevice,
                         VkFormat             format,
                         VkExtent2D           imageExtent,
                         std::vector<VkImage> inputImages,
                         std::vector<VkImage> outputImages,
                         Config*              pConfig)
    {
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = lut_frag;

        std::string lutFile = pConfig->lutFile();

        const char* home = std::getenv("HOME");
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        const std::string root = xdg && *xdg ? xdg : std::string(home ? home : "/tmp") + "/.config";
        const std::string externalPath = root + "/framecolor/external-lut.cube";
        // External mode is fixed for this session. Switching requires restart.
        if (std::ifstream(externalPath).good())
        {
            lutFile = externalPath;
            externalLutPath = externalPath;
            externalLut = true;
        }

        // FrameColor generates only .cube LUTs; PNG/image loaders are omitted.
        LutCube lutCube(lutFile);
        if (!lutCube.valid)
            Logger::err("Invalid LUT; using identity: " + lutCube.error);
        int32_t height = lutCube.size;
        const unsigned char* pixels = lutCube.colorCube.data();
        lutFormat = VK_FORMAT_R8G8B8A8_UNORM;
        uint32_t pixelBytes = 4;
        if (externalLut)
        {
            lutFormat = selectExternalLutFormat(pLogicalDevice);
            if (lutFormat == VK_FORMAT_R16G16B16A16_UNORM)
            {
                pixelBytes = 8;
                pixels = reinterpret_cast<const unsigned char*>(lutCube.colorCube16.data());
                Logger::warn("external LUT active (16-bit); sliders layered on top");
            }
            else
                Logger::warn("FrameColor external LUT active with 8-bit fallback: 16-bit filtering unsupported");
        }
        if (externalLut)
            setExternalBase(lutCube, pixels, pixelBytes);
        VkSpecializationMapEntry specMapEntry{0, 0, sizeof(int32_t)};

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = 1;
        fragmentSpecializationInfo.pMapEntries   = &specMapEntry;
        fragmentSpecializationInfo.dataSize      = sizeof(height);
        fragmentSpecializationInfo.pData         = &height;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        VkExtent3D lutImageExtent = {(uint32_t) height, (uint32_t) height, (uint32_t) height};
        lutSize = static_cast<uint32_t>(height);

        lutImage = createImages(pLogicalDevice,
                                1,
                                lutImageExtent,
                                lutFormat,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                lutMemory)[0];

        uploadToImage(pLogicalDevice, lutImage, lutImageExtent, height * height * height * pixelBytes, pixels);
        if (externalLut)
        {
            FileStamp stamp{};
            if (readFileStamp(externalLutPath, stamp) == FileStampResult::Available)
                externalLutChangeDetector.initialize(stamp);
            nextExternalLutCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }

        lutImageView = createImageViews(pLogicalDevice, lutFormat, std::vector<VkImage>(1, lutImage), VK_IMAGE_VIEW_TYPE_3D)[0];

        lutDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 1);
        descriptorSetLayouts.push_back(lutDescriptorSetLayout);

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = 1;

        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};

        lutDescriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);

        lutDescriptorSet =
            allocateAndWriteImageSamplerDescriptorSets(pLogicalDevice,
                                                       lutDescriptorPool,
                                                       lutDescriptorSetLayout,
                                                       {sampler},
                                                       std::vector<std::vector<VkImageView>>(1, std::vector<VkImageView>(1, lutImageView)))[0];
    }
    LutEffect::~LutEffect()
    {
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, lutImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, lutImage, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, lutDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, lutDescriptorPool, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, lutMemory, nullptr);
    }

    void LutEffect::setExternalBase(const LutCube& cube, const unsigned char* pixels, uint32_t pixelBytes)
    {
        const size_t nodeCount = static_cast<size_t>(cube.size) * cube.size * cube.size;
        externalBasePixels.assign(pixels, pixels + nodeCount * pixelBytes);
        externalBaseRgb.clear();
        externalBaseRgb.reserve(nodeCount);
        if (lutFormat == VK_FORMAT_R16G16B16A16_UNORM)
        {
            const auto* words = reinterpret_cast<const uint16_t*>(pixels);
            for (size_t node = 0; node < nodeCount; ++node)
            {
                externalBaseRgb.push_back({{words[node * 4] / 65535.0f, words[node * 4 + 1] / 65535.0f,
                                            words[node * 4 + 2] / 65535.0f}});
            }
        }
        else
        {
            for (size_t node = 0; node < nodeCount; ++node)
            {
                externalBaseRgb.push_back({{pixels[node * 4] / 255.0f, pixels[node * 4 + 1] / 255.0f,
                                            pixels[node * 4 + 2] / 255.0f}});
            }
        }
    }

    void LutEffect::checkExternalLutFile(std::chrono::steady_clock::time_point now)
    {
        if (!externalLut || now < nextExternalLutCheck)
            return;

        FileStamp stamp{};
        const FileStampResult result = readFileStamp(externalLutPath, stamp);
        if (result == FileStampResult::Missing)
        {
            externalLutChangeDetector.cancelCandidate();
            if (!externalLutDeletionLogged)
            {
                Logger::warn("FrameColor external LUT was deleted; keeping the current LUT");
                externalLutDeletionLogged = true;
            }
            nextExternalLutCheck = now + std::chrono::milliseconds(500);
            return;
        }
        if (result == FileStampResult::Error)
        {
            nextExternalLutCheck = now + std::chrono::milliseconds(500);
            return;
        }

        externalLutDeletionLogged = false;
        if (!externalLutChangeDetector.observe(stamp, now))
        {
            // A changed stamp gets a second sample after 200 ms. Ordinary
            // filesystem polling remains throttled to 500 ms.
            nextExternalLutCheck = now + (externalLutChangeDetector.waitingForStableSample()
                ? std::chrono::milliseconds(200)
                : std::chrono::milliseconds(500));
            return;
        }

        LutCube replacement(externalLutPath);
        std::string rejection;
        if (!replacement.valid)
            rejection = "invalid LUT";
        else if (replacement.size != static_cast<int>(lutSize))
            rejection = "LUT size changed";
        else if (selectExternalLutFormat(pLogicalDevice) != lutFormat)
            rejection = "LUT format support changed";

        if (!rejection.empty())
        {
            Logger::warn("FrameColor external LUT reload rejected (" + rejection + "); keeping the current LUT");
        }
        else
        {
            const uint32_t pixelBytes = lutFormat == VK_FORMAT_R16G16B16A16_UNORM ? 8 : 4;
            const unsigned char* pixels = lutFormat == VK_FORMAT_R16G16B16A16_UNORM
                ? reinterpret_cast<const unsigned char*>(replacement.colorCube16.data())
                : replacement.colorCube.data();
            setExternalBase(replacement, pixels, pixelBytes);
            hasFrameColorValues = false;

            pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
            uploadToImage(pLogicalDevice, lutImage, {lutSize, lutSize, lutSize},
                          static_cast<uint32_t>(externalBasePixels.size()), externalBasePixels.data(), 1, true);
            Logger::info("FrameColor external LUT reloaded");
        }
        nextExternalLutCheck = now + std::chrono::milliseconds(500);
    }

    void LutEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &(lutDescriptorSet), 0, nullptr);
        SimpleEffect::applyEffect(imageIndex, commandBuffer);
    }
    void LutEffect::updateEffect()
    {
        // The dashboard writes current.txt during slider drags and on release.
        const auto now = std::chrono::steady_clock::now();
        checkExternalLutFile(now);
        if (now < nextFrameColorCheck)
            return;
        nextFrameColorCheck = now + std::chrono::milliseconds(50);
        const char* home = std::getenv("HOME");
        if (!home || lutSize < 2 || lutSize > 65)
            return;
        const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
        const std::string configRoot = xdgConfig && *xdgConfig ? xdgConfig : std::string(home) + "/.config";
        std::ifstream valuesFile(configRoot + "/framecolor/current.txt");
        std::array<float, 12> values{{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                      1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
        std::vector<float> stored;
        for (float value; valuesFile >> value;)
            stored.push_back(value);
        if (stored.size() < 6 || stored.size() == 11)
            return;
        for (size_t i = 0; i < std::min(stored.size(), values.size()); ++i)
            values[i] = stored[i];
        // Older files have no gain or saturation fields. The ten-value file
        // used one saturation setting for all three output channels.
        if (stored.size() == 10)
            values[10] = values[11] = values[9];
        if (!std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); }))
            return;
        if (hasFrameColorValues && values == frameColorValues)
            return;

        if (externalLut)
        {
            const bool neutral = isNeutralLook(values);
            if (neutral)
            {
                // The initial upload is already the exact base. After a prior
                // adjustment, restore its original encoded nodes bit-for-bit.
                if (!hasFrameColorValues || isNeutralLook(frameColorValues))
                {
                    frameColorValues = values;
                    hasFrameColorValues = true;
                    return;
                }
                pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
                uploadToImage(pLogicalDevice, lutImage, {lutSize, lutSize, lutSize},
                              static_cast<uint32_t>(externalBasePixels.size()), externalBasePixels.data(), 1, true);
                frameColorValues = values;
                hasFrameColorValues = true;
                Logger::info("FrameColor live LUT updated");
                return;
            }

            const size_t nodeCount = externalBaseRgb.size();
            if (lutFormat == VK_FORMAT_R16G16B16A16_UNORM)
            {
                std::vector<uint16_t> pixels(nodeCount * 4, 65535);
                for (size_t node = 0; node < nodeCount; ++node)
                {
                    const auto rgb = applyLookTransform(externalBaseRgb[node], values);
                    for (size_t channel = 0; channel < 3; ++channel)
                        pixels[node * 4 + channel] = static_cast<uint16_t>(std::lround(
                            std::clamp(rgb[channel], 0.0f, 1.0f) * 65535.0f));
                }
                pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
                uploadToImage(pLogicalDevice, lutImage, {lutSize, lutSize, lutSize},
                              static_cast<uint32_t>(pixels.size() * sizeof(uint16_t)),
                              reinterpret_cast<const unsigned char*>(pixels.data()), 1, true);
            }
            else
            {
                std::vector<unsigned char> pixels(nodeCount * 4, 255);
                for (size_t node = 0; node < nodeCount; ++node)
                {
                    const auto rgb = applyLookTransform(externalBaseRgb[node], values);
                    for (size_t channel = 0; channel < 3; ++channel)
                        pixels[node * 4 + channel] = static_cast<unsigned char>(std::lround(
                            std::clamp(rgb[channel], 0.0f, 1.0f) * 255.0f));
                }
                pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
                uploadToImage(pLogicalDevice, lutImage, {lutSize, lutSize, lutSize},
                              static_cast<uint32_t>(pixels.size()), pixels.data(), 1, true);
            }
            frameColorValues = values;
            hasFrameColorValues = true;
            Logger::info("FrameColor live LUT updated");
            return;
        }

        std::array<std::array<unsigned char, 65>, 3> curves{};
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            float contrast = std::clamp(values[channel], 0.0f, 1.0f);
            float gamma = std::clamp(values[channel + 3], 0.50f, 2.50f);
            float gain = std::clamp(values[channel + 6], 0.50f, 1.50f);
            for (uint32_t point = 0; point < lutSize; ++point)
            {
                float x = static_cast<float>(point) / static_cast<float>(lutSize - 1);
                float color = std::pow(std::clamp(x * gain, 0.0f, 1.0f), 1.0f / gamma);
                float smooth = color * color * (3.0f - 2.0f * color);
                float corrected = std::clamp(color * (1.0f - contrast) + smooth * contrast, 0.0f, 1.0f);
                curves[channel][point] = static_cast<unsigned char>(std::lround(corrected * 255.0f));
            }
        }
        std::vector<unsigned char> pixels(lutSize * lutSize * lutSize * 4, 255);
        const std::array<float, 3> saturation{{std::clamp(values[9], 0.0f, 2.0f),
                                                std::clamp(values[10], 0.0f, 2.0f),
                                                std::clamp(values[11], 0.0f, 2.0f)}};
        for (uint32_t blue = 0; blue < lutSize; ++blue)
            for (uint32_t green = 0; green < lutSize; ++green)
                for (uint32_t red = 0; red < lutSize; ++red)
                {
                    size_t offset = ((blue * lutSize + green) * lutSize + red) * 4;
                    if (saturation == std::array<float, 3>{{1.0f, 1.0f, 1.0f}})
                    {
                        pixels[offset] = curves[0][red];
                        pixels[offset + 1] = curves[1][green];
                        pixels[offset + 2] = curves[2][blue];
                        continue;
                    }
                    const float r = curves[0][red] / 255.0f;
                    const float g = curves[1][green] / 255.0f;
                    const float b = curves[2][blue] / 255.0f;
                    const float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    pixels[offset] = static_cast<unsigned char>(std::lround(
                        std::clamp(luminance + (r - luminance) * saturation[0], 0.0f, 1.0f) * 255.0f));
                    pixels[offset + 1] = static_cast<unsigned char>(std::lround(
                        std::clamp(luminance + (g - luminance) * saturation[1], 0.0f, 1.0f) * 255.0f));
                    pixels[offset + 2] = static_cast<unsigned char>(std::lround(
                        std::clamp(luminance + (b - luminance) * saturation[2], 0.0f, 1.0f) * 255.0f));
                }

        // A previous present can still sample this image. Dashboard writes are
        // throttled, so wait before replacing its contents in place.
        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
        uploadToImage(pLogicalDevice, lutImage, {lutSize, lutSize, lutSize}, static_cast<uint32_t>(pixels.size()),
                      pixels.data(), 1, true);
        frameColorValues = values;
        hasFrameColorValues = true;
        Logger::info("FrameColor live LUT updated");
    }
} // namespace vkBasalt
