#pragma once

#include "vulkan/vulkan.h"

class VulkanRenderPassFactory
{
public:
	VulkanRenderPassFactory(VkDevice in_device);

	// Initializations
	VkResult CreateStandardRenderPass(VkFormat in_colorFormat, VkFormat in_depthFormat, VkRenderPass& out_renderPass);
private:
	VkDevice m_device;
};