#pragma once

#include "vulkan/vulkan.h"
#include <memory>

class VulkanMemoryHelper;

struct VulkanDepthStencil
{
	VkImage m_image;
	VkDeviceMemory m_gpuMem;
	VkImageView m_imageView;
};

class VulkanDepthStencilFactory
{
public:
	VulkanDepthStencilFactory(VkDevice in_device, std::shared_ptr<VulkanMemoryHelper> in_memory);

	void CreateDepthStencil(VkFormat in_format, uint32_t in_width, uint32_t in_height,
		VulkanDepthStencil& out_depthStencil);

private:
	VkDevice m_device;
	std::shared_ptr<VulkanMemoryHelper> m_memory;
};