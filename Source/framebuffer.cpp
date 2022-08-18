#include "layer.h"

bool findFramebuffer = true;
bool tryMatchingUnscaledImages = true;

std::vector<VkImage> unscaledImages;

// Collect images and their related data
std::unordered_map<VkImageView, VkImage> allImageViewsMap;
std::unordered_map<VkImage, VkExtent2D> allImageResolutions;
std::unordered_map<VkDescriptorSet, VkImage> potentialUnscaledImages;

uint32_t fbWidth = 0;
uint32_t fbHeight = 0;

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
	scoped_lock l(global_lock);
	VkResult result = device_dispatch[GetKey(device)].CreateImage(device, pCreateInfo, pAllocator, pImage);
	
	if (pCreateInfo->extent.width >= pCreateInfo->extent.height && pCreateInfo->extent.width != 0 && pCreateInfo->extent.height != 0) {
		allImageResolutions[*pImage] = VkExtent2D{ pCreateInfo->extent.width, pCreateInfo->extent.height };
	}

	return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
	scoped_lock l(global_lock);
	VkResult result = device_dispatch[GetKey(device)].CreateImageView(device, pCreateInfo, pAllocator, pView);

	if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_2D) {
		allImageViewsMap.emplace(*pView, pCreateInfo->image);
	}

	return result;
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) {
	scoped_lock l(global_lock);
	device_dispatch[GetKey(device)].UpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);

	if (descriptorWriteCount == 1 && pDescriptorWrites[0].descriptorCount == 1 && (pDescriptorWrites[0].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || pDescriptorWrites[0].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)) {
		auto imageIterator = allImageViewsMap.find(pDescriptorWrites[0].pImageInfo->imageView);
		if (imageIterator != allImageViewsMap.end()) {
			potentialUnscaledImages[pDescriptorWrites->dstSet] = imageIterator->second;
		}
	}
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
	scoped_lock l(global_lock);
	device_dispatch[GetKey(commandBuffer)].CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);

	if (tryMatchingUnscaledImages && (descriptorSetCount == 1)) {
		assert(potentialUnscaledImages.find(pDescriptorSets[0]) != potentialUnscaledImages.end());
		VkExtent2D imageResolution = allImageResolutions[potentialUnscaledImages[pDescriptorSets[0]]];
		if (imageResolution.width != 0 && imageResolution.height != 0) {
			unscaledImages.emplace_back(potentialUnscaledImages[pDescriptorSets[0]]);
		}
	}
}

// Collect render passes
std::unordered_map<VkDescriptorSet, VkImage> noOpsImages;
std::unordered_set<VkRenderPass> dontCareOpsRenderPassSet;

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
	scoped_lock l(global_lock);
	VkResult result = device_dispatch[GetKey(device)].CreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);

	if (pCreateInfo->attachmentCount >= 1 && pCreateInfo->pAttachments->loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE) {
		dontCareOpsRenderPassSet.emplace(*pRenderPass);
	}
	
	return result;
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) {
	scoped_lock l(global_lock);
	device_dispatch[GetKey(commandBuffer)].CmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
	
	if (dontCareOpsRenderPassSet.contains(pRenderPassBegin->renderPass)) {
		tryMatchingUnscaledImages = true;
	}
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
	// todo: implement second lock instead of reusing global lock
	if (tryMatchingUnscaledImages && !unscaledImages.empty()) {
		if (findFramebuffer) {
			logPrint("Found unscaled textures:");
			VkExtent2D biggestResolution{};
			for (VkImage image : unscaledImages) {
				VkExtent2D imageResolution = allImageResolutions[image];
				logPrint(std::string("Texture #") + std::to_string((int64_t)image) + ", " + std::to_string(imageResolution.width) + "x" + std::to_string(imageResolution.height));
				if ((biggestResolution.width * biggestResolution.height) < (imageResolution.width * imageResolution.height)) {
					biggestResolution = imageResolution;
				}
			}
			logPrint(std::string("The native resolution of the game's current rendering was guessed to be ") + std::to_string(biggestResolution.width) + "x" + std::to_string(biggestResolution.height));
			if (biggestResolution.width != 0 && biggestResolution.height != 0) {
				findFramebuffer = false;
				//fbWidth = biggestResolution.width;
				//fbHeight = biggestResolution.height;
				fbWidth = 1920;
				fbHeight = 1080;
				RND_InitRendering();
				XR_BeginSession();

				//leftEyeResources.width = biggestResolution.width;
				//leftEyeResources.height = biggestResolution.height;
				//rightEyeResources.width = biggestResolution.width;
				//rightEyeResources.height = biggestResolution.height;
				//dx11CreateSideTexture(&leftEyeResources);
				//dx11CreateSideTexture(&rightEyeResources);
				//importExternalTexture(commandBuffer, &leftEyeResources);
				//importExternalTexture(commandBuffer, &rightEyeResources);

				//std::thread newThread(dx11Thread);
				//newThread.detach();
			}
		}
		else {
			// If framebuffer was found and it's (one of)the last render pass of the frame, copy any texture buffers that match the unscaled viewport resolution
			for (const VkImage image : unscaledImages) {
				VkExtent2D imageResolution = allImageResolutions[image];
				if (imageResolution.width == fbWidth && imageResolution.height == fbHeight) {
					RND_RenderFrame(XR_NULL_HANDLE, commandBuffer, image);
					//sideTextureResources* currSide = currSwapSide == SWAP_SIDE::LEFT ? &leftEyeResources : &rightEyeResources;
					//copyTexture(commandBuffer, image, currSide);
					//increaseTimeSignature(&currSide->copiedTime, &currSide->copiedTimeCounter);
					//currRenderStatus = RENDER_STATUS::IMAGE_COPIED;
				}
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