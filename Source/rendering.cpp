#include "layer.h"

#include <vulkan/vk_enum_string_helper.h>

// Runtime
XrSession xrSessionHandle = XR_NULL_HANDLE;
std::array<XrSwapchain, 2> xrSwapchains = { VK_NULL_HANDLE, VK_NULL_HANDLE };
std::array<XrViewConfigurationView, 2> xrViewConfs = {};
std::array<std::vector<XrSwapchainImageVulkan2KHR>, 2> xrSwapchainImages = {};
XrSpace xrSpaceHandle = XR_NULL_HANDLE;

bool beganRendering = false;

// Current frame
XrFrameState frameState = { XR_TYPE_FRAME_STATE };
XrViewState frameViewState = { XR_TYPE_VIEW_STATE };
std::array<XrView, 2> frameViews;
std::vector<XrCompositionLayerBaseHeader*> frameLayers;
std::array<XrCompositionLayerProjectionView, 2> frameProjectionViews;
XrCompositionLayerProjection frameRenderLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };

// Functions

XrSwapchain RND_CreateSwapchain(XrSession xrSession, XrViewConfigurationView& viewConf) {
	logPrint("Creating OpenXR swapchain...");
	
	// Finds the first matching VkFormat (uint32) that matches the int64 from OpenXR
	auto getBestSwapchainFormat = [](const std::vector<int64_t>& runtimePreferredFormats, const std::vector<VkFormat>& applicationSupportedFormats) -> VkFormat {
		auto found = std::find_first_of(std::begin(runtimePreferredFormats), std::end(runtimePreferredFormats), std::begin(applicationSupportedFormats), std::end(applicationSupportedFormats));
		if (found == std::end(runtimePreferredFormats)) {
			throw std::runtime_error("OpenXR runtime doesn't support any of the presenting modes that the GPU drivers support.");
		}
		return (VkFormat)*found;
	};

	uint32_t swapchainCount = 0;
	xrEnumerateSwapchainFormats(xrSharedSession, 0, &swapchainCount, NULL);
	std::vector<int64_t> xrSwapchainFormats(swapchainCount);
	xrEnumerateSwapchainFormats(xrSharedSession, swapchainCount, &swapchainCount, (int64_t*)xrSwapchainFormats.data());

	logPrint("OpenXR supported swapchain formats:");
	for (uint32_t i=0; i<swapchainCount; i++) {
		logPrint(std::format(" - {:08x} = {}", (int64_t)xrSwapchainFormats[i], string_VkFormat((VkFormat)xrSwapchainFormats[i])));
	}

	std::vector<VkFormat> preferredColorFormats = {
		VK_FORMAT_B8G8R8A8_SRGB // Currently the framebuffer that gets caught is using VK_FORMAT_A2B10G10R10_UNORM_PACK32
	};

	VkFormat xrSwapchainFormat = getBestSwapchainFormat(xrSwapchainFormats, preferredColorFormats);
	logPrint(std::format("Picked {} as the texture format for swapchain", string_VkFormat(xrSwapchainFormat)));

	XrSwapchainCreateInfo swapchainCreateInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	swapchainCreateInfo.width = viewConf.recommendedImageRectWidth;
	swapchainCreateInfo.height = viewConf.recommendedImageRectHeight;
	swapchainCreateInfo.arraySize = 1;
	swapchainCreateInfo.sampleCount = viewConf.recommendedSwapchainSampleCount;
	swapchainCreateInfo.format = xrSwapchainFormat;
	swapchainCreateInfo.mipCount = 1;
	swapchainCreateInfo.faceCount = 1;
	swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.createFlags = 0;

	XrSwapchain retSwapchain = VK_NULL_HANDLE;
	checkXRResult(xrCreateSwapchain(xrSessionHandle, &swapchainCreateInfo, &retSwapchain), "Failed to create OpenXR swapchain images!");

	logPrint("Created OpenXR swapchain...");
	return retSwapchain;
}

std::vector<XrSwapchainImageVulkan2KHR> RND_EnumerateSwapchainImages(XrSwapchain xrSwapchain) {
	logPrint("Creating OpenXR swapchain images...");
	uint32_t swapchainChainCount = 0;
	xrEnumerateSwapchainImages(xrSwapchain, 0, &swapchainChainCount, NULL);
	std::vector<XrSwapchainImageVulkan2KHR> swapchainImages(swapchainChainCount, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR });
	checkXRResult(xrEnumerateSwapchainImages(xrSwapchain, swapchainChainCount, &swapchainChainCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages.data())), "Failed to enumerate OpenXR swapchain images!");
	logPrint("Created OpenXR swapchain images");
	return swapchainImages;
}

void RND_InitRendering() {
	xrSessionHandle = XR_CreateSession(vkSharedInstance, vkSharedDevice, vkSharedPhysicalDevice);
	xrViewConfs = XR_CreateViewConfiguration();
	xrSwapchains[0] = RND_CreateSwapchain(xrSessionHandle, xrViewConfs[0]);
	xrSwapchains[1] = RND_CreateSwapchain(xrSessionHandle, xrViewConfs[1]);

	xrSwapchainImages[0] = RND_EnumerateSwapchainImages(xrSwapchains[0]);
	xrSwapchainImages[1] = RND_EnumerateSwapchainImages(xrSwapchains[1]);

	xrSpaceHandle = XR_CreateSpace();
}

void RND_BeginFrame() {
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

void RND_UpdateViews() {
	uint32_t viewCountOutput;

	XrViewLocateInfo viewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
	viewLocateInfo.displayTime = frameState.predictedDisplayTime;
	viewLocateInfo.space = xrSpaceHandle;
	viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	checkXRResult(xrLocateViews(xrSessionHandle, &viewLocateInfo, &frameViewState, (uint32_t)frameViews.size(), &viewCountOutput, frameViews.data()), "Failed to locate views in OpenXR!");
	assert(viewCountOutput == 2);
}

void RND_CopyVulkanTexture(VkCommandBuffer copyCMDBuffer, VkImage sourceImage, VkImage destinationImage) {
	VkImageMemoryBarrier copyToBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	copyToBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyToBarrier.subresourceRange.baseMipLevel = 0;
	copyToBarrier.subresourceRange.levelCount = 1;
	copyToBarrier.subresourceRange.baseArrayLayer = 0;
	copyToBarrier.subresourceRange.layerCount = 1;
	copyToBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	copyToBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	copyToBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	copyToBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	copyToBarrier.image = sourceImage;
	device_dispatch[GetKey(copyCMDBuffer)].CmdPipelineBarrier(copyCMDBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyToBarrier);

	VkImageCopy copyRegion = {};
	copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.srcSubresource.mipLevel = 0;
	copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.dstSubresource.mipLevel = 0;
	copyRegion.extent.width = 720;
	copyRegion.extent.height = 720;
	//copyRegion.extent.width = std::max(1u, 800 >> 1);
	//copyRegion.extent.height = std::max(1u, presentTextureSize.height >> 1);
	copyRegion.extent.depth = 1;

	device_dispatch[GetKey(copyCMDBuffer)].CmdCopyImage(copyCMDBuffer, sourceImage, VK_IMAGE_LAYOUT_GENERAL, destinationImage, VK_IMAGE_LAYOUT_GENERAL, 1, &copyRegion);
	logPrint("Copied the source image to the VR presenting image!");

	VkImageMemoryBarrier optimalFormatBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	optimalFormatBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	optimalFormatBarrier.subresourceRange.baseMipLevel = 0;
	optimalFormatBarrier.subresourceRange.levelCount = 1;
	optimalFormatBarrier.subresourceRange.baseArrayLayer = 0;
	optimalFormatBarrier.subresourceRange.layerCount = 1;
	optimalFormatBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	optimalFormatBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	optimalFormatBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	optimalFormatBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	optimalFormatBarrier.image = destinationImage;
	device_dispatch[GetKey(copyCMDBuffer)].CmdPipelineBarrier(copyCMDBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &optimalFormatBarrier);
}


void RND_RenderFrame(XrSwapchain xrSwapchain, VkCommandBuffer copyCmdBuffer, VkImage copyImage) {
	if (!beganRendering) return;

	xrSwapchain = xrSwapchains[0];
	
	if (frameState.shouldRender && false) {
		RND_UpdateViews();
		if (frameViewState.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
			uint32_t swapchainImageIndex = 0;
			checkXRResult(xrAcquireSwapchainImage(xrSwapchain, NULL, &swapchainImageIndex), "Can't acquire OpenXR swapchain image!");

			XrSwapchainImageWaitInfo waitSwapchainInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			waitSwapchainInfo.timeout = XR_INFINITE_DURATION;
			if (XrResult waitResult = xrWaitSwapchainImage(xrSwapchain, &waitSwapchainInfo); waitResult == XR_TIMEOUT_EXPIRED && XR_SUCCEEDED(waitResult)) {
				checkXRResult(waitResult, "Failed to wait for swapchain image!");
			}

			float leftHalfFOV = glm::degrees(frameViews[0].fov.angleLeft);
			float rightHalfFOV = glm::degrees(frameViews[0].fov.angleRight);
			float upHalfFOV = glm::degrees(frameViews[0].fov.angleUp);
			float downHalfFOV = glm::degrees(frameViews[0].fov.angleDown);

			float horizontalHalfFOV = (abs(frameViews[0].fov.angleLeft) + abs(frameViews[0].fov.angleRight)) * 0.5;
			float verticalHalfFOV = (abs(frameViews[0].fov.angleUp) + abs(frameViews[0].fov.angleDown)) * 0.5;

			for (size_t i = 0; i < frameProjectionViews.size(); i++) {
				frameProjectionViews[i].pose = frameViews[i].pose;
				frameProjectionViews[i].fov = frameViews[i].fov;
				frameProjectionViews[i].fov.angleLeft = -horizontalHalfFOV;
				frameProjectionViews[i].fov.angleRight = horizontalHalfFOV;
				frameProjectionViews[i].fov.angleUp = verticalHalfFOV;
				frameProjectionViews[i].fov.angleDown = -verticalHalfFOV;
				frameProjectionViews[i].subImage.swapchain = xrSwapchain;
				frameProjectionViews[i].subImage.imageRect = { .offset = {0, 0}, .extent = {.width = (int32_t)xrViewConfs[i].recommendedImageRectWidth, .height = (int32_t)xrViewConfs[i].recommendedImageRectHeight} };
				frameProjectionViews[i].subImage.imageArrayIndex = i;
			}
			
			frameRenderLayer.layerFlags = NULL;
			frameRenderLayer.space = xrSpaceHandle;
			frameRenderLayer.viewCount = (uint32_t)frameProjectionViews.size();
			frameRenderLayer.views = frameProjectionViews.data();

			frameLayers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&frameRenderLayer));
		}
		checkXRResult(xrReleaseSwapchainImage(xrSwapchain, NULL), "Failed to release OpenXR swapchain image!");
	}
}

void RND_EndFrame() {
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
	if (beganRendering) RND_EndFrame();
	RND_BeginFrame();
	beganRendering = true;
	return result;
}