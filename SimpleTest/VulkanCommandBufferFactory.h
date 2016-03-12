#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <memory>

class VulkanSwapChain;
struct VulkanDepthStencil;

class VulkanCommandBufferFactory
{
public:
	VulkanCommandBufferFactory(VkDevice in_device); 

	// Initializations
	VkResult CreateCommandBuffer(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, VkCommandBuffer& out_buffer);
	VkResult CreateCommandBuffers(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, std::vector<VkCommandBuffer> out_buffers);

	// Constructs
	void ConstructSwapchainDepthStencilInitializationCommandBuffer(VkCommandBuffer& inout_buffer, 
		std::shared_ptr<VulkanSwapChain> in_swapChain, VulkanDepthStencil& in_depthStencil);
	void ConstructDrawCommandBuffer() {/* TODO!! */}
	void ConstructPostPresentCommandBuffer() {/* TODO!! */ }


private:
	VkCommandBufferAllocateInfo MakeInfoStruct(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, int in_bufferCount = 1);

	VkDevice m_device;

	// Image layout helper
	void AddImageLayoutChangeToCommandBuffer(VkCommandBuffer inout_cmdbuffer, VkImage in_image, VkImageAspectFlags in_aspectMask, VkImageLayout in_oldImageLayout, VkImageLayout in_newImageLayout);
};