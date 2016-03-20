#pragma once

#include "vulkan/vulkan.h"
#include <vector>

struct VulkanVertexLayout
{
	std::vector<VkVertexInputBindingDescription>   m_bindingDescriptions;

	// An entry for each data type in the vertex, for example: [0]:pos, [1]:col, [2]: normal
	std::vector<VkVertexInputAttributeDescription> m_attributeDescriptions;
};
