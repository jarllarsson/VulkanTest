#pragma once

#include "vulkan/vulkan.h"
#include <memory>
#include "VkObj.h"

class VulkanMemoryHelper;

struct VulkanDepthStencil
{
	VulkanDepthStencil(const VkObj<VkDevice>& in_device);
	VkObj<VkImage> m_image;
	VkObj<VkDeviceMemory> m_gpuMem;
	VkObj<VkImageView> m_imageView;
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