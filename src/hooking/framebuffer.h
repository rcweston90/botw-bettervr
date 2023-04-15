#pragma once

#include "rendering/texture.h"
#include "rendering/openxr.h"

struct CaptureTexture {
    bool initialized;
    VkExtent2D foundSize;
    VkImage foundImage;
    VkFormat format;
    VkExtent2D minSize;
    OpenXR::EyeSide eyeSide = OpenXR::EyeSide::LEFT;
    std::array<std::unique_ptr<SharedTexture>, 2> sharedTextures = { nullptr, nullptr };

    // current frame state
    std::atomic<VkCommandBuffer> captureCmdBuffer = VK_NULL_HANDLE;
};

extern std::array<CaptureTexture, 1> captureTextures;