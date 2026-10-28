#include "effect_lut.hpp"

#include <cstring>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <iterator>
#include <system_error>
#include <utility>

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

        bool supportsLutFormat(LogicalDevice* device, VkFormat format)
        {
            VkFormatProperties properties{};
            device->vki.GetPhysicalDeviceFormatProperties(device->physicalDevice, format, &properties);
            const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                  VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            return (properties.optimalTilingFeatures & required) == required;
        }

        bool hasCubeExtension(const std::string& name)
        {
            if (name.size() < 5)
                return false;
            std::string extension = name.substr(name.size() - 5);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return extension == ".cube";
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

        const std::string lutFile = pConfig->lutFile();
        LutCube configuredLut(lutFile);
        if (!configuredLut.valid)
            Logger::err("Invalid configured LUT; using identity size: " + configuredLut.error);
        const int32_t height = configuredLut.size;
        lutSize = static_cast<uint32_t>(height);
        lutFormat = selectExternalLutFormat(pLogicalDevice);
        formatUnsupported = !supportsLutFormat(pLogicalDevice, lutFormat);
        if (lutFormat == VK_FORMAT_R16G16B16A16_UNORM)
            Logger::info("FrameColor LUT uses 16-bit UNORM; sliders layered on top");
        else
            Logger::warn("FrameColor LUT uses 8-bit fallback: 16-bit filtering unsupported");
        if (formatUnsupported)
            Logger::err("FrameColor LUT unsupported: 8-bit sampled linear filtering is unavailable");
        setBaseCube(makeIdentityLutCube(height));

        const char* home = std::getenv("HOME");
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        const std::string homeRoot = home && *home ? home : "/tmp";
        configRoot = xdg && *xdg ? xdg : homeRoot + "/.config";
        lutControlPath = configRoot + "/framecolor/lut.txt";
        lutStatusPath = configRoot + "/framecolor/lut-status.txt";
        lutLibraryPath = homeRoot + "/Documents/FrameColor/luts";
        legacyLutPath = configRoot + "/framecolor/external-lut.cube";

        VkSpecializationMapEntry specMapEntry{0, 0, sizeof(int32_t)};

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = 1;
        fragmentSpecializationInfo.pMapEntries   = &specMapEntry;
        fragmentSpecializationInfo.dataSize      = sizeof(height);
        fragmentSpecializationInfo.pData         = &height;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        VkExtent3D lutImageExtent = {(uint32_t) height, (uint32_t) height, (uint32_t) height};
        lutImage = createImages(pLogicalDevice,
                                1,
                                lutImageExtent,
                                lutFormat,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                lutMemory)[0];

        uploadToImage(pLogicalDevice, lutImage, lutImageExtent, static_cast<uint32_t>(basePixels.size()), basePixels.data());
        baseDirty = false;

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

        nextLutControlCheck = std::chrono::steady_clock::now();
        lutLoaderThread = std::thread(&LutEffect::lutLoaderLoop, this);
        writeLutStatus({formatUnsupported ? LutStatusState::Unsupported : LutStatusState::Off, {},
                        formatUnsupported ? "8-bit format unsupported" : ""});
    }
    LutEffect::~LutEffect()
    {
        {
            std::lock_guard<std::mutex> lock(lutLoaderMutex);
            lutLoaderStopping = true;
            lutLoadRequest.reset();
        }
        lutLoaderCondition.notify_one();
        if (lutLoaderThread.joinable())
            lutLoaderThread.join();
        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, lutImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, lutImage, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, lutDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, lutDescriptorPool, nullptr);
        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, lutMemory, nullptr);
    }

    void LutEffect::setBaseCube(const LutRgbCube& cube, const std::string& path,
                                const FileStamp& stamp, bool fileBase)
    {
        const size_t nodeCount = static_cast<size_t>(lutSize) * lutSize * lutSize;
        if (cube.size != static_cast<int>(lutSize) || cube.values.size() != nodeCount)
            return;
        if (fileBase && activeBaseIsFile && activeBasePath == path && activeBaseStamp == stamp)
            return;
        if (!fileBase && !activeBaseIsFile && !baseRgb.empty())
            return;

        baseRgb = cube.values;
        if (lutFormat == VK_FORMAT_R16G16B16A16_UNORM)
        {
            std::vector<uint16_t> words(nodeCount * 4, 65535);
            for (size_t node = 0; node < nodeCount; ++node)
                for (size_t channel = 0; channel < 3; ++channel)
                    words[node * 4 + channel] = static_cast<uint16_t>(std::lround(
                        std::clamp(baseRgb[node][channel], 0.0f, 1.0f) * 65535.0f));
            basePixels.resize(words.size() * sizeof(uint16_t));
            std::memcpy(basePixels.data(), words.data(), basePixels.size());
        }
        else
        {
            basePixels.assign(nodeCount * 4, 255);
            for (size_t node = 0; node < nodeCount; ++node)
                for (size_t channel = 0; channel < 3; ++channel)
                    basePixels[node * 4 + channel] = static_cast<unsigned char>(std::lround(
                        std::clamp(baseRgb[node][channel], 0.0f, 1.0f) * 255.0f));
        }
        activeBaseIsFile = fileBase;
        activeBasePath = fileBase ? path : std::string{};
        activeBaseStamp = fileBase ? stamp : FileStamp{};
        baseDirty = true;
        hasFrameColorValues = false;
    }

    void LutEffect::useIdentityBase()
    {
        if (!activeBaseIsFile && !baseRgb.empty())
            return;
        setBaseCube(makeIdentityLutCube(static_cast<int>(lutSize)));
    }

    void LutEffect::setSelectionState(const LutSelection& nextSelection, bool legacy)
    {
        const bool changed = !selectionInitialized || selection.enabled != nextSelection.enabled ||
                             selection.file != nextSelection.file || desiredLutIsLegacy != legacy;
        if (!changed)
            return;

        selection = nextSelection;
        selectionInitialized = true;
        desiredLutIsLegacy = legacy;
        desiredLutPath.clear();
        selectedFileMissing = false;
        // A new selection starts a fresh stability check. Keeping the old handled
        // stamp would ignore a file that was selected, left and selected again
        // before its load finished.
        selectedLutChangeDetector = StableFileChangeDetector{};
        hasSubmittedLoad = false;
        ++loadRequestId;

        if (!selection.enabled)
        {
            useIdentityBase();
            writeLutStatus({LutStatusState::Off, selection.file, {}});
            return;
        }
        if (selection.file.empty())
        {
            useIdentityBase();
            writeLutStatus({LutStatusState::Off, {}, "no file selected"});
            return;
        }
        if (!hasCubeExtension(selection.file))
        {
            writeLutStatus({LutStatusState::Error, selection.file, "file is not a .cube"});
            return;
        }

        desiredLutPath = legacy ? legacyLutPath : lutLibraryPath + "/" + selection.file;
        if (formatUnsupported)
            writeLutStatus({LutStatusState::Unsupported, selection.file, "8-bit format unsupported"});
    }

    void LutEffect::writeLutStatus(const LutStatus& status)
    {
        if (hasWrittenStatus && status == lastWrittenStatus)
            return;
        if (!hasWrittenStatus)
        {
            std::ifstream previous(lutStatusPath);
            if (previous)
            {
                const std::string contents((std::istreambuf_iterator<char>(previous)), std::istreambuf_iterator<char>());
                LutStatus parsed;
                if (parseLutStatus(contents, parsed) && parsed == status)
                {
                    lastWrittenStatus = parsed;
                    hasWrittenStatus = true;
                    return;
                }
            }
        }

        const std::filesystem::path path(lutStatusPath);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return;
        const std::string temporary = lutStatusPath + ".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            if (!output)
                return;
            output << formatLutStatus(status);
            output.close();
            if (!output)
            {
                std::filesystem::remove(temporary, error);
                return;
            }
        }
        if (std::rename(temporary.c_str(), lutStatusPath.c_str()) != 0)
        {
            std::filesystem::remove(temporary, error);
            return;
        }
        lastWrittenStatus = status;
        hasWrittenStatus = true;
    }

    void LutEffect::requestLutLoad(const std::string& path, const FileStamp& stamp)
    {
        LutLoadRequest request;
        request.id = ++loadRequestId;
        request.path = path;
        request.stamp = stamp;
        hasSubmittedLoad = true;
        submittedLutPath = path;
        submittedLutStamp = stamp;
        {
            std::lock_guard<std::mutex> lock(lutLoaderMutex);
            lutLoadRequest = std::move(request);
        }
        lutLoaderCondition.notify_one();
    }

    void LutEffect::lutLoaderLoop()
    {
        for (;;)
        {
            LutLoadRequest request;
            {
                std::unique_lock<std::mutex> lock(lutLoaderMutex);
                lutLoaderCondition.wait(lock, [this] { return lutLoaderStopping || lutLoadRequest.has_value(); });
                if (lutLoaderStopping)
                    return;
                request = std::move(*lutLoadRequest);
                lutLoadRequest.reset();
            }

            LutLoadResult result;
            result.id = request.id;
            result.path = request.path;
            result.stamp = request.stamp;
            LutCube parsed(request.path);
            FileStamp after{};
            result.stable = readFileStamp(request.path, after) == FileStampResult::Available && after == request.stamp;
            if (result.stable && !parsed.valid)
                result.error = parsed.error;
            else if (result.stable)
            {
                LutRgbCube source;
                source.size = parsed.size;
                source.values.reserve(static_cast<size_t>(parsed.size) * parsed.size * parsed.size);
                for (size_t node = 0; node < static_cast<size_t>(parsed.size) * parsed.size * parsed.size; ++node)
                    source.values.push_back({{parsed.colorCube16[node * 4] / 65535.0f,
                                              parsed.colorCube16[node * 4 + 1] / 65535.0f,
                                              parsed.colorCube16[node * 4 + 2] / 65535.0f}});
                result.cube = resampleLutCube(source, static_cast<int>(lutSize));
                if (result.cube.values.empty())
                    result.error = "LUT resample failed";
            }

            {
                std::lock_guard<std::mutex> lock(lutLoaderMutex);
                if (!lutLoaderStopping)
                    lutLoadResult = std::move(result);
            }
        }
    }

    void LutEffect::processLutLoadResult()
    {
        std::optional<LutLoadResult> result;
        {
            std::lock_guard<std::mutex> lock(lutLoaderMutex);
            if (lutLoadResult)
            {
                result = std::move(lutLoadResult);
                lutLoadResult.reset();
            }
        }
        if (!result || result->id != loadRequestId || !selection.enabled || hasSelectionError ||
            result->path != desiredLutPath)
            return;
        hasSubmittedLoad = false;
        if (!result->stable)
        {
            // Changed while it was being read: observe it again from scratch.
            selectedLutChangeDetector = StableFileChangeDetector{};
            return;
        }
        if (!result->error.empty())
        {
            writeLutStatus({LutStatusState::Error, selection.file, result->error});
            Logger::warn("FrameColor LUT load failed (" + result->error + "); keeping the current base");
            return;
        }

        cachedCube = std::move(result->cube);
        cachedLutPath = result->path;
        cachedLutStamp = result->stamp;
        hasCachedCube = true;
        setBaseCube(cachedCube, cachedLutPath, cachedLutStamp, true);
        writeLutStatus({formatUnsupported ? LutStatusState::Unsupported : LutStatusState::Active,
                        selection.file, formatUnsupported ? "8-bit format unsupported" :
                        (lutFormat == VK_FORMAT_R8G8B8A8_UNORM ? "8-bit fallback" : "")});
        Logger::info("FrameColor LUT loaded and resampled");
    }

    void LutEffect::updateLutControl(std::chrono::steady_clock::time_point now)
    {
        if (now < nextLutControlCheck)
            return;
        nextLutControlCheck = now + std::chrono::milliseconds(250);

        FileStamp controlStamp{};
        const FileStampResult controlResult = readFileStamp(lutControlPath, controlStamp);
        if (controlResult == FileStampResult::Available &&
            (!selectionInitialized || !selectionFilePresent || !hasSelectionFileStamp || controlStamp != selectionFileStamp))
        {
            std::ifstream input(lutControlPath);
            if (!input)
                return;
            const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            selectionFilePresent = true;
            selectionFileStamp = controlStamp;
            hasSelectionFileStamp = true;
            const LutSelection parsed = parseLutSelection(contents);
            if (!parsed.valid)
            {
                selectionInitialized = true;
                hasSelectionError = true;
                hasSubmittedLoad = false;
                ++loadRequestId;
                writeLutStatus({LutStatusState::Error, {}, parsed.error});
                return;
            }
            hasSelectionError = false;
            setSelectionState(parsed, false);
        }
        else if (controlResult == FileStampResult::Missing && (!selectionInitialized || selectionFilePresent))
        {
            selectionFilePresent = false;
            hasSelectionFileStamp = false;
            LutSelection fallback;
            fallback.valid = true;
            FileStamp legacyStamp{};
            if (readFileStamp(legacyLutPath, legacyStamp) == FileStampResult::Available)
            {
                fallback.enabled = true;
                fallback.file = "external-lut.cube";
            }
            hasSelectionError = false;
            setSelectionState(fallback, fallback.enabled);
        }

        if (!selection.enabled || selection.file.empty() || desiredLutPath.empty() || hasSelectionError)
            return;

        FileStamp stamp{};
        const FileStampResult result = readFileStamp(desiredLutPath, stamp);
        if (result == FileStampResult::Missing)
        {
            if (!selectedFileMissing)
            {
                selectedFileMissing = true;
                hasSubmittedLoad = false;
                ++loadRequestId;
                selectedLutChangeDetector = StableFileChangeDetector{};
                writeLutStatus({LutStatusState::Error, selection.file, "file missing"});
            }
            return;
        }
        if (result == FileStampResult::Error)
            return;
        selectedFileMissing = false;

        if (hasCachedCube && cachedLutPath == desiredLutPath && cachedLutStamp == stamp)
        {
            setBaseCube(cachedCube, cachedLutPath, cachedLutStamp, true);
            writeLutStatus({formatUnsupported ? LutStatusState::Unsupported : LutStatusState::Active,
                            selection.file, formatUnsupported ? "8-bit format unsupported" :
                            (lutFormat == VK_FORMAT_R8G8B8A8_UNORM ? "8-bit fallback" : "")});
            return;
        }
        if (activeBaseIsFile && activeBasePath == desiredLutPath && activeBaseStamp == stamp)
        {
            writeLutStatus({formatUnsupported ? LutStatusState::Unsupported : LutStatusState::Active,
                            selection.file, formatUnsupported ? "8-bit format unsupported" :
                            (lutFormat == VK_FORMAT_R8G8B8A8_UNORM ? "8-bit fallback" : "")});
            return;
        }
        if (hasSubmittedLoad && submittedLutPath == desiredLutPath && submittedLutStamp == stamp)
            return;
        if (!selectedLutChangeDetector.observe(stamp, now))
            return;
        requestLutLoad(desiredLutPath, stamp);
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
        processLutLoadResult();
        updateLutControl(now);
        if (now < nextFrameColorCheck && !baseDirty)
            return;
        nextFrameColorCheck = now + std::chrono::milliseconds(50);
        if (lutSize < 2 || lutSize > 65)
            return;
        std::ifstream valuesFile(configRoot + "/framecolor/current.txt");
        std::array<float, 12> values{{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                      1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
        std::vector<float> stored;
        for (float value; valuesFile >> value;)
            stored.push_back(value);
        const bool storedValid = !(stored.size() < 6 || stored.size() == 11);
        // A half-written or unknown slider file keeps the last look; a pending
        // base change is still uploaded, with neutral sliders.
        if (!storedValid && !baseDirty)
            return;
        if (storedValid)
            for (size_t i = 0; i < std::min(stored.size(), values.size()); ++i)
                values[i] = stored[i];
        // Older files have no gain or saturation fields. The ten-value file
        // used one saturation setting for all three output channels.
        if (stored.size() == 10)
            values[10] = values[11] = values[9];
        if (!std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); }))
        {
            if (!baseDirty)
                return;
            values = {{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                       1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
        }
        if (!baseDirty && hasFrameColorValues && values == frameColorValues)
            return;

        if (baseRgb.empty())
            return;
        if (isNeutralLook(values))
        {
            const bool mustUpload = baseDirty || (hasFrameColorValues && !isNeutralLook(frameColorValues));
            if (mustUpload)
            {
                pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
                uploadToImage(pLogicalDevice, lutImage, {lutSize, lutSize, lutSize},
                              static_cast<uint32_t>(basePixels.size()), basePixels.data(), 1, true);
            }
            frameColorValues = values;
            hasFrameColorValues = true;
            baseDirty = false;
            if (mustUpload)
                Logger::info("FrameColor live LUT updated");
            return;
        }

        const size_t nodeCount = baseRgb.size();
        if (lutFormat == VK_FORMAT_R16G16B16A16_UNORM)
        {
            std::vector<uint16_t> pixels(nodeCount * 4, 65535);
            for (size_t node = 0; node < nodeCount; ++node)
            {
                const auto rgb = applyLookTransform(baseRgb[node], values);
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
                const auto rgb = applyLookTransform(baseRgb[node], values);
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
        baseDirty = false;
        Logger::info("FrameColor live LUT updated");
    }
} // namespace vkBasalt
