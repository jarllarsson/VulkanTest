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
	VulkanDepthStencilFactory(VkDevice in_device);

	void CreateDepthStencil(VkFormat in_format, uint32_t in_width, uint32_t in_height, const std::shared_ptr<VulkanMemoryHelper> in_memory,
		VulkanDepthStencil& out_depthStencil);

private:
	VkDevice m_device;
};