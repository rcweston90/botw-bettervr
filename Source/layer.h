#pragma once
#include "pch.h"

// single global lock, for simplicity
extern std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;

// use the loader's dispatch table pointer as a key for dispatch map lookups
template<typename DispatchableType>
void* GetKey(DispatchableType inst) {
	return *(void**)inst;
}

enum OpenXRRuntime {
	UNKNOWN,
	OCULUS_RUNTIME,
	STEAMVR_RUNTIME
};

// layer book-keeping information, to store dispatch tables by key
extern std::map<void*, VkLayerInstanceDispatchTable> instance_dispatch;
extern std::map<void*, VkLayerDispatchTable> device_dispatch;

// Shared variables
// todo: Remove this shortcut, should be reworked into classes
extern VkInstance vkSharedInstance;
extern VkPhysicalDevice vkSharedPhysicalDevice;
extern VkDevice vkSharedDevice;

extern D3D_FEATURE_LEVEL d3d12SharedFeatureLevel;
extern LUID d3d12SharedAdapter;

extern XrInstance xrSharedInstance;
extern XrSystemId xrSharedSystemId;
extern XrSession xrSharedSession;

extern std::list<VkInstance> steamInstances;
extern std::list<VkDevice> steamDevices;
extern std::list<VkInstance> oculusInstances;
extern std::list<VkDevice> oculusDevices;
extern VkInstance topVkInstance;
extern VkDevice topVkDevice;
extern OpenXRRuntime currRuntime;

// hook functions
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
//VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
//VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdEndRenderPass(VkCommandBuffer commandBuffer);
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);

// internal functions of all the subsystems
bool cemuInitialize();

void logInitialize();
void logShutdown();
void logPrint(const char* message);
void logPrint(const std::string_view& message_view);
void logTimeElapsed(char* prefixMessage, LARGE_INTEGER time);
void checkXRResult(XrResult result, const char* errorMessage = nullptr);
void checkVkResult(VkResult result, const char* errorMessage = nullptr);
void checkHResult(HRESULT result, const char* errorMessage = nullptr);
void checkAssert(bool assert, const char* errorMessage = nullptr);

// xr functions
void XR_initInstance();
void XR_deinitInstance();
std::array<XrViewConfigurationView, 2> XR_CreateViewConfiguration();
void XR_GetSupportedAdapter(D3D_FEATURE_LEVEL* minFeatureLevel, LUID* adapterLUID);
XrSession XR_CreateSession(XrGraphicsBindingD3D12KHR& d3d12Binding);
void XR_BeginSession();
XrSpace XR_CreateSpace();

void D3D12_CreateInstance(D3D_FEATURE_LEVEL minFeatureLevel, LUID adapterLUID);
void D3D12_CreateShaderPipeline(DXGI_FORMAT swapchainFormat, uint32_t swapchainWidth, uint32_t swapchainHeight);
HANDLE D3D12_CreateSharedFence();
HANDLE D3D12_CreateSharedTexture(uint32_t width, uint32_t height, DXGI_FORMAT format);
void D3D12_RenderFrameToSwapchain(uint32_t textureIdx, ID3D12Resource* swapchain, uint32_t swapchainWidth, uint32_t swapchainHeight);
XrGraphicsBindingD3D12KHR D3D12_GetGraphicsBinding();
void D3D12_DestroyInstance();

VkSemaphore RNDVK_CreateSemaphore(HANDLE d3d12Fence);
VkImage RNDVK_ImportImage(HANDLE d3d12Texture, uint32_t width, uint32_t height, VkFormat format);
void RNDVK_CopyImage(VkCommandBuffer currCmdBuffer, VkImage srcImage, VkImage dstImage);

// rendering functions
void RND_InitRendering(uint32_t srcWidth, uint32_t srcHeight, VkFormat srcFormat);
void RND_BeginFrame();
void RND_RenderFrame(VkCommandBuffer copyCmdBuffer, VkImage copyImage);
void RND_EndFrame();
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);


// framebuffer functions
VkExtent2D FB_GetFrameDimensions();

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage);
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView);
VK_LAYER_EXPORT void VKAPI_CALL Layer_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies);
VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdEndRenderPass(VkCommandBuffer commandBuffer);

// layer_setup functions
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);

static HMODULE vulkanModule = NULL;

static bool initializeLayer() {
	if (!cemuInitialize()) {
		// Vulkan layer is hooking something that's not Cemu
		return false;
	}
	logInitialize();
	return true;
}

static void shutdownLayer() {
	logShutdown();
}