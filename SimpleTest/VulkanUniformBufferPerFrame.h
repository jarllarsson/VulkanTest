#pragma once

#include "vulkan/vulkan.h"
#include "MathTypes.h"
#include "VkObj.h"

// Allocation and structure of a uniform buffer 
// to be updated for each rendered frame

struct VulkanUniformBufferPerFrame
{
	struct BufferAllocation
	{
		BufferAllocation(const VkObj<VkDevice>& in_device)
			: m_buffer(in_device, vkDestroyBuffer)
			, m_gpuMem(in_device, vkFreeMemory)
		{
#ifdef _DEBUG
			m_buffer.SetDbgName(std::string("UniformBuffer"));
			m_gpuMem.SetDbgName(std::string("UniformBufferMemory"));
#endif // _DEBUG
		}
		VkObj<VkBuffer> m_buffer;
		VkObj<VkDeviceMemory> m_gpuMem;
		VkDescriptorBufferInfo m_descriptorBufferInfo;
	};

	struct BufferDataLayout
	{
		glm::mat4 m_projectionMatrix;
		glm::mat4 m_worldMatrix;
		glm::mat4 m_viewMatrix;
	};

	VulkanUniformBufferPerFrame(const VkObj<VkDevice>& in_device)
		: m_allocation(in_device)
	{}

	BufferAllocation m_allocation;
	BufferDataLayout m_data;
};

