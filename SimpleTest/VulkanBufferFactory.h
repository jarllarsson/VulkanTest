#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <memory>

class VulkanMemoryHelper;

class VulkanBufferFactory
{
public:
	VulkanBufferFactory(VkDevice in_device);

	// Create a buffer, allocate gpu memory, copy optional init data and bind the buffer
	bool CreateBuffer(VkBufferUsageFlags in_usage,
		VkDeviceSize in_size,
		void* in_data,
		const std::shared_ptr<VulkanMemoryHelper> in_memory,
		VkBuffer& out_buffer,
		VkDeviceMemory& out_allocatedDeviceMemory);

private:
	VkDevice m_device;
};