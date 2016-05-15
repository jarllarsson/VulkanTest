#pragma once

#include "vulkan/vulkan.h"
#include "MathTypes.h"

// Allocation and structure of a uniform buffer 
// to be updated for each rendered frame

struct VulkanUniformBufferPerFrame
{
	struct BufferAllocation
	{
		VkBuffer m_buffer;
		VkDeviceMemory m_gpuMem;
		VkDescriptorBufferInfo m_descriptorBufferInfo;
	};

	struct BufferDataLayout
	{
		glm::mat4 m_projectionMatrix;
		glm::mat4 m_worldMatrix;
		glm::mat4 m_viewMatrix;
	};

	BufferAllocation m_allocation;
	BufferDataLayout m_data;
};

