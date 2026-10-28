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

#include "vulkan_include.hpp"

#include "effect_simple.hpp"
#include "config.hpp"
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
        bool                  externalLut = false;
        VkFormat              lutFormat = VK_FORMAT_R8G8B8A8_UNORM;
        std::vector<std::array<float, 3>> externalBaseRgb;
        std::vector<unsigned char> externalBasePixels;
        std::string externalLutPath;
        StableFileChangeDetector externalLutChangeDetector;
        bool                  externalLutDeletionLogged = false;
        std::chrono::steady_clock::time_point nextExternalLutCheck{};
        bool                  hasFrameColorValues = false;
        std::array<float, 12> frameColorValues{};
        std::chrono::steady_clock::time_point nextFrameColorCheck{};

        void setExternalBase(const LutCube& cube, const unsigned char* pixels, uint32_t pixelBytes);
        void checkExternalLutFile(std::chrono::steady_clock::time_point now);
    };
} // namespace vkBasalt

#endif // EFFECT_LUT_HPP_INCLUDED
