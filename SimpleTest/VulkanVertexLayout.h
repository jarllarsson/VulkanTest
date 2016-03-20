#pragma once

#include "vulkan/vulkan.h"
#include <vector>

struct VulkanVertexLayout
{
	VkPipelineVertexInputStateCreateInfo           m_inputLayout;
	std::vector<VkVertexInputBindingDescription>   m_bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> m_attributeDescriptions;
};
