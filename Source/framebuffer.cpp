#include "layer.h"

constexpr VkFormat FB_searchFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
constexpr VkExtent2D FB_searchMinSize = {.width = 1280, .height = 720};
bool FB_searchSizeFound = false;
VkExtent2D FB_searchSize = {.width = 0, .height = 0};



bool lostFramebuffer = true;
bool tryMatchingUnscaledImages = true;

std::vector<VkImage> unscaledImages;

// Collect images and their related data
// stored using multiple maps for faster indexed access
std::unordered_map<VkImageView, VkImage> bestImageViews;
std::unordered_map<VkImage, VkExtent2D> bestImageResolution;
std::unordered_map<VkDescriptorSet, VkImage> bestDescriptorSets;

std::unordered_map<VkRenderPass, VkImage> bestFramebuffers;


VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
	scoped_lock l(global_lock);
	VkResult result = device_dispatch[GetKey(device)].CreateImage(device, pCreateInfo, pAllocator, pImage);
	
	// If the size has already been found, just match the resolution since it's fixed
	const VkExtent3D *imageSize = &pCreateInfo->extent;
	if (FB_searchSizeFound && (imageSize->width == FB_searchSize.width && imageSize->height == FB_searchSize.height) && imageSize->depth == 1) {
		bestImageResolution[*pImage] = FB_searchSize;
		logPrint(std::format("Found image with fixed size {} {}x{}", (void*)*pImage, FB_searchSize.width, FB_searchSize.height));
	}
	// Else, match everything that's 16:9-ish with a minimum size and format
	else if (!FB_searchSizeFound && pCreateInfo->format == FB_searchFormat && imageSize->depth == 1 && (imageSize->width >= FB_searchMinSize.width && imageSize->height >= FB_searchMinSize.height)) {
		if (!bestImageResolution.empty() && (bestImageResolution[0].width > imageSize->width && bestImageResolution[0].height > imageSize->height)) {
			bestImageResolution.clear();
			bestImageViews.clear();
			bestDescriptorSets.clear();
		}
		logPrint(std::format("Found image with non-fixed size {} {}x{}", (void*)*pImage, imageSize->width, imageSize->height));
		bestImageResolution[*pImage] = VkExtent2D{ pCreateInfo->extent.width, pCreateInfo->extent.height };
	}

	return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
	scoped_lock l(global_lock);
	VkResult result = device_dispatch[GetKey(device)].CreateImageView(device, pCreateInfo, pAllocator, pView);

	// Create link between image views and images
	if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_2D && pCreateInfo->format == FB_searchFormat && bestImageResolution.contains(pCreateInfo->image)) {
		bestImageViews.emplace(*pView, pCreateInfo->image);
	}

	return result;
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) {
	scoped_lock l(global_lock);
	device_dispatch[GetKey(device)].UpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);

	// Link any descriptors that use the image views
	for (uint32_t i=0; i<descriptorWriteCount; i++) {
		if (pDescriptorWrites[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || pDescriptorWrites[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
			std::unordered_map<VkImageView, VkImage>::iterator imageSet = bestImageViews.find(pDescriptorWrites[i].pImageInfo->imageView);
			if (imageSet != bestImageViews.end()) {
				logPrint(std::format("Found descriptor set {} that uses an image {}", (void*)pDescriptorWrites->dstSet, (void*)imageSet->second));
				bestDescriptorSets[pDescriptorWrites->dstSet] = imageSet->second;
			}
		}
	}
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
	scoped_lock l(global_lock);
	device_dispatch[GetKey(commandBuffer)].CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);

	// Test whenever a descriptor is used within this frame!
	for (uint32_t i=0; i<descriptorSetCount; i++) {
		auto descriptorSet = bestDescriptorSets.find(pDescriptorSets[i]);
		if (descriptorSet != bestDescriptorSets.end()) {
			VkExtent2D imageResolution = bestImageResolution[descriptorSet->second];
			if (imageResolution.width != 0 && imageResolution.height != 0) {
				//logPrint(std::format("Binding image that likely contains framebuffer!"));
				unscaledImages.emplace_back(descriptorSet->second);
			}
		}
	}
	//if (descriptorSetCount == 2 && dynamicOffsetCount == 2) {
	//	for (uint32_t i=0; i<descriptorSetCount; i++) {
	//		auto descriptorSet = bestDescriptorSets.find(pDescriptorSets[0]);
	//		if (descriptorSet != bestDescriptorSets.end()) {
	//			VkExtent2D imageResolution = bestImageResolution[descriptorSet->second];
	//			if (imageResolution.width != 0 && imageResolution.height != 0) {
	//				unscaledImages.emplace_back(descriptorSet->second);
	//			}
	//		}
	//	}
	//}
}

// Collect render passes
//std::unordered_map<VkDescriptorSet, VkImage> noOpsImages;
//std::unordered_set<VkRenderPass> dontCareOpsRenderPassSet;

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
	scoped_lock l(global_lock);
	VkResult result = device_dispatch[GetKey(device)].CreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);

	//if (pCreateInfo->attachmentCount >= 1 && pCreateInfo->pAttachments->loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE) {
	//	dontCareOpsRenderPassSet.emplace(*pRenderPass);
	//}
	
	return result;
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) {
	scoped_lock l(global_lock);
	device_dispatch[GetKey(commandBuffer)].CmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
	
	//if (dontCareOpsRenderPassSet.contains(pRenderPassBegin->renderPass)) {
	//	tryMatchingUnscaledImages = true;
	//}
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
	// todo: implement second lock instead of reusing global lock
	if (!unscaledImages.empty()) {
		if (!FB_searchSizeFound) {
			logPrint("Found unscaled textures:");
			VkExtent2D biggestResolution = { .width = 0, .height = 0 };
			for (VkImage image : unscaledImages) {
				VkExtent2D imageResolution = bestImageResolution[image];
				logPrint(std::string("Texture #") + std::to_string((int64_t)image) + ", " + std::to_string(imageResolution.width) + "x" + std::to_string(imageResolution.height));
				if ((biggestResolution.width * biggestResolution.height) < (imageResolution.width * imageResolution.height)) {
					biggestResolution = imageResolution;
				}
			}
			logPrint(std::string("The native resolution of the game's current rendering was guessed to be ") + std::to_string(biggestResolution.width) + "x" + std::to_string(biggestResolution.height));
			if (biggestResolution.width != 0 && biggestResolution.height != 0) {
				FB_searchSizeFound = true;
				FB_searchSize.width = biggestResolution.width;
				FB_searchSize.height = biggestResolution.height;
				RND_InitRendering(FB_searchSize.width, FB_searchSize.height, FB_searchFormat);
				XR_BeginSession();
				RND_BeginFrame();
			}
		}
		else {
			// If framebuffer was found and it's (one of) the last render pass of the frame, copy any texture buffers that match the unscaled viewport resolution
			checkAssert(unscaledImages.size());			
			for (const VkImage& image : unscaledImages) {
				//logPrint(std::format("Pick image {}", (void*)image));
				RND_RenderFrame(commandBuffer, image);
			}

			tryMatchingUnscaledImages = false;
			unscaledImages.clear();
		}
	}
	
	{
		scoped_lock l(global_lock);
		device_dispatch[GetKey(commandBuffer)].CmdEndRenderPass(commandBuffer);
	}
}

VkExtent2D FB_GetFrameDimensions() {
	return FB_searchSize;
}