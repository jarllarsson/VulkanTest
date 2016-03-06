#pragma once

#include "vulkan/vulkan.h"
#include <vector>

class VulkanCommandBufferFactory
{
public:
	VulkanCommandBufferFactory(VkDevice in_device); 

	VkResult CreateCommandBuffer(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, VkCommandBuffer& out_buffer);
	VkResult CreateCommandBuffers(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, std::vector<VkCommandBuffer> out_buffers);
private:
	VkCommandBufferAllocateInfo MakeInfoStruct(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, int in_bufferCount = 1);

	VkDevice m_device;
}