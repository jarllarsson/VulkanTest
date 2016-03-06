#pragma once

#include "vulkan/vulkan.h"

struct VulkanDepthStencil
{
	VkImage m_image;
	VkDeviceMemory m_gpuMem;
	VkImageView m_imageView;
};