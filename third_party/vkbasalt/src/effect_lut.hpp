#ifndef EFFECT_LUT_HPP_INCLUDED
#define EFFECT_LUT_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include "vulkan_include.hpp"

#include "effect_simple.hpp"
#include "config.hpp"
#include "lut_control.hpp"
#include "stable_file_change.hpp"

namespace vkBasalt
{
    class LutCube;

    class LutEffect : public SimpleEffect
    {
    public:
        LutEffect(LogicalDevice*       pLogicalDevice,
                  VkFormat             format,
                  VkExtent2D           imageExtent,
                  std::vector<VkImage> inputImages,
                  std::vector<VkImage> outputImages,
                  Config*              pConfig);
        ~LutEffect();
        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        void updateEffect() override;

    private:
        VkImage               lutImage;
        VkDeviceMemory        lutMemory;
        VkImageView           lutImageView;
        VkDescriptorSetLayout lutDescriptorSetLayout;
        VkDescriptorPool      lutDescriptorPool;
        VkDescriptorSet       lutDescriptorSet;
        uint32_t              lutSize = 0;
        VkFormat              lutFormat = VK_FORMAT_R8G8B8A8_UNORM;
        std::vector<std::array<float, 3>> baseRgb;
        std::vector<unsigned char> basePixels;
        std::string configRoot;
        std::string lutControlPath;
        std::string lutStatusPath;
        std::string lutLibraryPath;
        std::string legacyLutPath;
        LutSelection selection;
        bool selectionInitialized = false;
        bool selectionFilePresent = false;
        bool hasSelectionFileStamp = false;
        FileStamp selectionFileStamp{};
        bool hasSelectionError = false;
        std::string desiredLutPath;
        bool desiredLutIsLegacy = false;
        bool selectedFileMissing = false;
        StableFileChangeDetector selectedLutChangeDetector;
        bool hasSubmittedLoad = false;
        std::string submittedLutPath;
        FileStamp submittedLutStamp{};
        uint64_t loadRequestId = 0;
        LutRgbCube cachedCube;
        std::string cachedLutPath;
        FileStamp cachedLutStamp{};
        bool hasCachedCube = false;
        std::string activeBasePath;
        FileStamp activeBaseStamp{};
        bool activeBaseIsFile = false;
        bool baseDirty = false;
        bool formatUnsupported = false;
        LutStatus lastWrittenStatus{};
        bool hasWrittenStatus = false;
        std::chrono::steady_clock::time_point nextLutControlCheck{};
        bool                  hasFrameColorValues = false;
        std::array<float, 12> frameColorValues{};
        std::chrono::steady_clock::time_point nextFrameColorCheck{};

        struct LutLoadRequest
        {
            uint64_t id = 0;
            std::string path;
            FileStamp stamp{};
        };
        struct LutLoadResult
        {
            uint64_t id = 0;
            std::string path;
            FileStamp stamp{};
            LutRgbCube cube;
            std::string error;
            bool stable = false;
        };
        std::mutex lutLoaderMutex;
        std::condition_variable lutLoaderCondition;
        std::optional<LutLoadRequest> lutLoadRequest;
        std::optional<LutLoadResult> lutLoadResult;
        bool lutLoaderStopping = false;
        std::thread lutLoaderThread;

        void setBaseCube(const LutRgbCube& cube, const std::string& path = {},
                         const FileStamp& stamp = {}, bool fileBase = false);
        void useIdentityBase();
        void setSelectionState(const LutSelection& nextSelection, bool legacy);
        void updateLutControl(std::chrono::steady_clock::time_point now);
        void processLutLoadResult();
        void requestLutLoad(const std::string& path, const FileStamp& stamp);
        void lutLoaderLoop();
        void writeLutStatus(const LutStatus& status);
    };
} // namespace vkBasalt

#endif // EFFECT_LUT_HPP_INCLUDED
