#include "layer.h"

#include <vulkan/vk_enum_string_helper.h>

// Runtime
XrSession xrSessionHandle = XR_NULL_HANDLE;
std::array<XrSwapchain, 2> xrSwapchains = { VK_NULL_HANDLE, VK_NULL_HANDLE };
std::array<XrViewConfigurationView, 2> xrViewConfs = {};
std::array<std::vector<XrSwapchainImageD3D12KHR>, 2> xrSwapchainImages = {};
XrSpace xrSpaceHandle = XR_NULL_HANDLE;

HANDLE vkSharedFence;
std::array<VkImage, 2> vkSharedImages = {VK_NULL_HANDLE, VK_NULL_HANDLE};
uint32_t sharedTexture_width = 0;
uint32_t sharedTexture_height = 0;

bool beganRendering = false;

// Current frame
XrFrameState frameState = { XR_TYPE_FRAME_STATE };
XrViewState frameViewState = { XR_TYPE_VIEW_STATE };
std::array<XrView, 2> frameViews;
std::vector<XrCompositionLayerBaseHeader*> frameLayers;
std::array<XrCompositionLayerProjectionView, 2> frameProjectionViews;
XrCompositionLayerProjection frameRenderLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };

// Functions

DXGI_FORMAT RNDUtil_GetSwapchainFormat() {
	// Finds the first matching DXGI_FORMAT (int) that matches the int64 from OpenXR
	auto getBestSwapchainFormat = [](const std::vector<int64_t>& runtimePreferredFormats, const std::vector<DXGI_FORMAT>& applicationSupportedFormats) -> DXGI_FORMAT {
		auto found = std::find_first_of(std::begin(runtimePreferredFormats), std::end(runtimePreferredFormats), std::begin(applicationSupportedFormats), std::end(applicationSupportedFormats));
		if (found == std::end(runtimePreferredFormats)) {
			throw std::runtime_error("OpenXR runtime doesn't support any of the presenting modes that the GPU drivers support.");
		}
		return (DXGI_FORMAT)*found;
	};

	uint32_t swapchainCount = 0;
	xrEnumerateSwapchainFormats(xrSharedSession, 0, &swapchainCount, NULL);
	std::vector<int64_t> xrSwapchainFormats(swapchainCount);
	xrEnumerateSwapchainFormats(xrSharedSession, swapchainCount, &swapchainCount, (int64_t*)xrSwapchainFormats.data());

	logPrint("OpenXR supported swapchain formats:");
	for (uint32_t i = 0; i < swapchainCount; i++) {
		logPrint(std::format(" - {}", (std::underlying_type_t<DXGI_FORMAT>)xrSwapchainFormats[i]));
	}

	std::vector<DXGI_FORMAT> preferredColorFormats = {
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB // Currently the framebuffer that gets caught is using VK_FORMAT_A2B10G10R10_UNORM_PACK32
	};

	DXGI_FORMAT xrSwapchainFormat = getBestSwapchainFormat(xrSwapchainFormats, preferredColorFormats);
	logPrint(std::format("Picked {} as the texture format for swapchain", (std::underlying_type_t<DXGI_FORMAT>)xrSwapchainFormat));

	return xrSwapchainFormat;
}

XrSwapchain RND_CreateSwapchain(XrSession xrSession, XrViewConfigurationView& viewConf, DXGI_FORMAT swapchainFormat) {
	logPrint("Creating OpenXR swapchain...");

	XrSwapchainCreateInfo swapchainCreateInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	swapchainCreateInfo.width = viewConf.recommendedImageRectWidth;
	swapchainCreateInfo.height = viewConf.recommendedImageRectHeight;
	swapchainCreateInfo.arraySize = 1;
	swapchainCreateInfo.sampleCount = viewConf.recommendedSwapchainSampleCount;
	swapchainCreateInfo.format = swapchainFormat;
	swapchainCreateInfo.mipCount = 1;
	swapchainCreateInfo.faceCount = 1;
	// todo: Transfer DST bit could probably be dropped
	swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.createFlags = 0;

	XrSwapchain retSwapchain = XR_NULL_HANDLE;
	checkXRResult(xrCreateSwapchain(xrSessionHandle, &swapchainCreateInfo, &retSwapchain), "Failed to create OpenXR swapchain images!");

	logPrint("Created OpenXR swapchain!");
	return retSwapchain;
}

std::vector<XrSwapchainImageD3D12KHR> RND_EnumerateSwapchainImages(XrSwapchain xrSwapchain) {
	logPrint("Creating OpenXR swapchain images...");
	uint32_t swapchainChainCount = 0;
	xrEnumerateSwapchainImages(xrSwapchain, 0, &swapchainChainCount, NULL);
	std::vector<XrSwapchainImageD3D12KHR> swapchainImages(swapchainChainCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
	checkXRResult(xrEnumerateSwapchainImages(xrSwapchain, swapchainChainCount, &swapchainChainCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages.data())), "Failed to enumerate OpenXR swapchain images!");
	logPrint(std::format("Created {} OpenXR swapchain images", swapchainChainCount));
	return swapchainImages;
}

VkImage RND_CreateSharedTexture(uint32_t srcWidth, uint32_t srcHeight, VkFormat srcFormat) {
	checkAssert(srcFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32, "Currently only VK_FORMAT_A2B10G10R10_UNORM_PACK32 is supported for the shared texture");

	// Use closest resembling texture type, see https://github.com/doitsujin/dxvk/blob/master/src/dxgi/dxgi_format.cpp
	HANDLE handle = D3D12_CreateSharedTexture(srcWidth, srcHeight, DXGI_FORMAT_R10G10B10A2_UNORM);
	VkImage image = RNDVK_ImportImage(handle, srcWidth, srcHeight, srcFormat);
	
	return image;
}

void RND_InitRendering(uint32_t srcWidth, uint32_t srcHeight, VkFormat srcFormat) {
	// Initializing shared rendering
	D3D12_CreateInstance(d3d12SharedFeatureLevel, d3d12SharedAdapter);
	
	XrGraphicsBindingD3D12KHR graphicsBinding = D3D12_GetGraphicsBinding();
	xrSessionHandle = XR_CreateSession(graphicsBinding);
	xrViewConfs = XR_CreateViewConfiguration();
	xrSpaceHandle = XR_CreateSpace();

	// Creating swapchain
	DXGI_FORMAT swapchainFormat = RNDUtil_GetSwapchainFormat();
	xrSwapchains[0] = RND_CreateSwapchain(xrSessionHandle, xrViewConfs[0], swapchainFormat);
	xrSwapchains[1] = RND_CreateSwapchain(xrSessionHandle, xrViewConfs[1], swapchainFormat);
	
	xrSwapchainImages[0] = RND_EnumerateSwapchainImages(xrSwapchains[0]);
	xrSwapchainImages[1] = RND_EnumerateSwapchainImages(xrSwapchains[1]);

	D3D12_CreateShaderPipeline(swapchainFormat, xrViewConfs[0].recommendedImageRectWidth, xrViewConfs[0].recommendedImageRectHeight);

	// Initialize shared textures and fences
	vkSharedFence = D3D12_CreateSharedFence();
	RNDVK_CreateSemaphore(vkSharedFence);
	vkSharedImages[0] = RND_CreateSharedTexture(srcWidth, srcHeight, srcFormat);
	vkSharedImages[1] = RND_CreateSharedTexture(srcWidth, srcHeight, srcFormat);
	sharedTexture_width = srcWidth;
	sharedTexture_height = srcHeight;
}

void RND_UpdateViews() {
	uint32_t viewCountOutput;

	XrViewLocateInfo viewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
	viewLocateInfo.displayTime = frameState.predictedDisplayTime;
	viewLocateInfo.space = xrSpaceHandle;
	viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	checkXRResult(xrLocateViews(xrSessionHandle, &viewLocateInfo, &frameViewState, (uint32_t)frameViews.size(), &viewCountOutput, frameViews.data()), "Failed to locate views in OpenXR!");
	assert(viewCountOutput == 2);
}

uint32_t alternateIndex = 0;

void RND_BeginFrame() {
	checkAssert(xrSessionHandle != XR_NULL_HANDLE, "Tried to begin frame without initializing rendering first!");

	XrFrameWaitInfo waitFrameInfo = { XR_TYPE_FRAME_WAIT_INFO};
	checkXRResult(xrWaitFrame(xrSessionHandle, &waitFrameInfo, &frameState), "Failed to wait for next frame!");

	XrFrameBeginInfo beginFrameInfo = { XR_TYPE_FRAME_BEGIN_INFO };
	checkXRResult(xrBeginFrame(xrSessionHandle, &beginFrameInfo), "Couldn't begin OpenXR frame!");

	frameLayers = {};
	frameViews[0] = { XR_TYPE_VIEW };
	frameViews[1] = { XR_TYPE_VIEW };
	frameProjectionViews[0] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
	frameProjectionViews[1] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
	frameViewState = { XR_TYPE_VIEW_STATE };
	frameRenderLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };

	RND_UpdateViews();
}

// todo: Call xrLocateSpace maybe

void RND_RenderFrame(VkCommandBuffer copyCmdBuffer, VkImage copyImage) {
	//checkAssert(beganRendering, "Tried to render frame without beginning frame first? todo: remove!");
	if (!beganRendering) return;
	
	auto& currSwapchain = xrSwapchains[alternateIndex];
	auto& currSwapchainImages = xrSwapchainImages[alternateIndex];
	auto& currViewConfiguration = xrViewConfs[alternateIndex];
	auto& currViewProjection = frameProjectionViews[alternateIndex];
	uint32_t currSwapchainWidth = xrViewConfs[alternateIndex].recommendedImageRectWidth;
	uint32_t currSwapchainHeight = xrViewConfs[alternateIndex].recommendedImageRectHeight;
	
	if (currRuntime == OpenXRRuntime::OCULUS_RUNTIME ? true/*todo: find out why oculus always returns false in shouldRender*/ : frameState.shouldRender) {
		RND_UpdateViews();
		if (frameViewState.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT) || true) {
			uint32_t acquiredSwapchainImageIdx = 0;
			checkXRResult(xrAcquireSwapchainImage(currSwapchain, NULL, &acquiredSwapchainImageIdx), "Can't acquire OpenXR swapchain image!");

			XrSwapchainImageWaitInfo waitSwapchainInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			waitSwapchainInfo.timeout = XR_INFINITE_DURATION;
			if (XrResult waitResult = xrWaitSwapchainImage(currSwapchain, &waitSwapchainInfo); waitResult == XR_TIMEOUT_EXPIRED && XR_SUCCEEDED(waitResult)) {
				checkXRResult(waitResult, "Failed to wait for swapchain image!");
			}
			
			RNDVK_CopyImage(copyCmdBuffer, copyImage, vkSharedImages[alternateIndex]);
			D3D12_RenderFrameToSwapchain(alternateIndex, currSwapchainImages[acquiredSwapchainImageIdx].texture, currSwapchainWidth, currSwapchainHeight);

			float leftHalfFOV = glm::degrees(frameViews[0].fov.angleLeft);
			float rightHalfFOV = glm::degrees(frameViews[0].fov.angleRight);
			float upHalfFOV = glm::degrees(frameViews[0].fov.angleUp);
			float downHalfFOV = glm::degrees(frameViews[0].fov.angleDown);

			float horizontalHalfFOV = (float)(abs(frameViews[0].fov.angleLeft) + abs(frameViews[0].fov.angleRight)) * 0.5f;
			float verticalHalfFOV = (float)(abs(frameViews[0].fov.angleUp) + abs(frameViews[0].fov.angleDown)) * 0.5f;
			
			currViewProjection.pose = frameViews[alternateIndex].pose;
			currViewProjection.fov = frameViews[alternateIndex].fov;
			currViewProjection.fov.angleLeft = -horizontalHalfFOV;
			currViewProjection.fov.angleRight = horizontalHalfFOV;
			currViewProjection.fov.angleUp = verticalHalfFOV;
			currViewProjection.fov.angleDown = -verticalHalfFOV;
			currViewProjection.subImage.swapchain = currSwapchain;
			currViewProjection.subImage.imageRect = {
				.offset = {0, 0},
				.extent = {
					.width = (int32_t)xrViewConfs[alternateIndex].recommendedImageRectWidth,
					.height = (int32_t)xrViewConfs[alternateIndex].recommendedImageRectHeight
				}
			};
			currViewProjection.subImage.imageArrayIndex = 0;

			//for (size_t i = 0; i < frameProjectionViews.size(); i++) {
			//	frameProjectionViews[i].pose = frameViews[i].pose;
			//	frameProjectionViews[i].fov = frameViews[i].fov;
			//	frameProjectionViews[i].fov.angleLeft = -horizontalHalfFOV;
			//	frameProjectionViews[i].fov.angleRight = horizontalHalfFOV;
			//	frameProjectionViews[i].fov.angleUp = verticalHalfFOV;
			//	frameProjectionViews[i].fov.angleDown = -verticalHalfFOV;
			//	frameProjectionViews[i].subImage.swapchain = currSwapchain;
			//	frameProjectionViews[i].subImage.imageRect = {
			//		.offset = {0, 0},
			//		.extent = {
			//			.width = (int32_t)xrViewConfs[i].recommendedImageRectWidth,
			//			.height = (int32_t)xrViewConfs[i].recommendedImageRectHeight
			//		}
			//	};
			//	frameProjectionViews[i].subImage.imageArrayIndex = 0;
			//}
			
			frameRenderLayer.layerFlags = NULL;
			frameRenderLayer.space = xrSpaceHandle;
			//frameRenderLayer.viewCount = (uint32_t)1;
			//frameRenderLayer.views = &currViewProjection;
			frameRenderLayer.viewCount = (uint32_t)frameProjectionViews.size();
			frameRenderLayer.views = frameProjectionViews.data();

			frameLayers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&frameRenderLayer));
		}
		checkXRResult(xrReleaseSwapchainImage(currSwapchain, NULL), "Failed to release OpenXR swapchain image!");
	}
	alternateIndex = alternateIndex == 0 ? 1 : 0;
}

void RND_EndFrame() {
	checkAssert(xrSessionHandle != XR_NULL_HANDLE, "Tried to begin frame without initializing rendering first!");

	XrFrameEndInfo frameEndInfo = { XR_TYPE_FRAME_END_INFO };
	frameEndInfo.displayTime = frameState.predictedDisplayTime;
	frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	frameEndInfo.layerCount = (uint32_t)frameLayers.size();
	frameEndInfo.layers = frameLayers.data();
	checkXRResult(xrEndFrame(xrSessionHandle, &frameEndInfo), "Failed to render texture!");
}

// Track frame rendering

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
	VkResult result;
	{
		scoped_lock l(global_lock);
		result = device_dispatch[GetKey(queue)].QueueSubmit(queue, submitCount, pSubmits, fence);
	}
	return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
	VkResult result = VK_SUCCESS;
	{
		scoped_lock l(global_lock);
		result = device_dispatch[GetKey(queue)].QueuePresentKHR(queue, pPresentInfo);
	}

	if (xrSessionHandle != VK_NULL_HANDLE) {
		RND_EndFrame();
		RND_BeginFrame();
		beganRendering = true;
	}

	return result;
}