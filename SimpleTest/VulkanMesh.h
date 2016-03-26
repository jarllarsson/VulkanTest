#pragma once

#include "vulkan/vulkan.h"
#include <vector>

class VulkanMesh
{
public:
	struct Vertices
	{
		uint32_t m_count;
		VkBuffer m_buffer;
		VkDeviceMemory m_gpuMem;
	};

	struct Indices
	{
		uint32_t m_count;
		VkBuffer m_buffer;
		VkDeviceMemory m_gpuMem;
	};


	Vertices m_vertices;
	Indices  m_indices;

private:

};