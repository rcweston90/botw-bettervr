#pragma once

#include <Windows.h>
#include <winrt/base.h>

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "vk_layer_dispatch_table.h"

#define HOOK_PROC_FUNC(func) if(!strcmp(pName, "vk" #func)) return (PFN_vkVoidFunction)&Layer_##func;

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <mutex>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <string>
#include <assert.h>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <bitset>

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/projection.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>

#undef VK_LAYER_EXPORT
#if defined(WIN32)
#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define VK_LAYER_EXPORT extern "C"
#endif

static PFN_xrGetVulkanGraphicsRequirements2KHR func_xrGetVulkanGraphicsRequirements2KHR = nullptr;
static PFN_xrCreateVulkanInstanceKHR func_xrCreateVulkanInstanceKHR = nullptr;
static PFN_xrCreateVulkanDeviceKHR func_xrCreateVulkanDeviceKHR = nullptr;
static PFN_xrGetVulkanGraphicsDevice2KHR func_xrGetVulkanGraphicsDevice2KHR = nullptr;

static PFN_xrConvertTimeToWin32PerformanceCounterKHR func_xrConvertTimeToWin32PerformanceCounterKHR = nullptr;
static PFN_xrConvertWin32PerformanceCounterToTimeKHR func_xrConvertWin32PerformanceCounterToTimeKHR = nullptr;

static PFN_xrCreateDebugUtilsMessengerEXT func_xrCreateDebugUtilsMessengerEXT = nullptr;
static PFN_xrDestroyDebugUtilsMessengerEXT func_xrDestroyDebugUtilsMessengerEXT = nullptr;

template <typename T>
static T* find_layer_info(const void* structure_chain, VkStructureType type, VkLayerFunction function) {
	T* next = reinterpret_cast<T*>(const_cast<void*>(structure_chain));
	while (next != nullptr && !(next->sType == type && next->function == function))
		next = reinterpret_cast<T*>(const_cast<void*>(next->pNext));
	return next;
}

static inline void* dispatch_key_from_handle(const void* dispatch_handle) {
	// The Vulkan loader writes the dispatch table pointer right to the start of the object, so use that as a key for lookup
	// This ensures that all objects of a specific level (device or instance) will use the same dispatch table
	return *(void**)dispatch_handle;
}
