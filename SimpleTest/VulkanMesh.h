#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include "VkObj.h"

class VulkanMesh
{
public:
	struct Vertices
	{
		Vertices (const VkObj<VkDevice>& in_device)
			: m_count()
			, m_buffer(in_device, vkDestroyBuffer)
			, m_gpuMem(in_device, vkFreeMemory)
		{
#ifdef _DEBUG
			m_buffer.SetDbgName(std::string("VertexBuffer"));
			m_gpuMem.SetDbgName(std::string("VertexMemory"));
#endif // _DEBUG
		}
		uint32_t m_count;
		VkObj<VkBuffer> m_buffer;
		VkObj<VkDeviceMemory> m_gpuMem;
	};

	struct Indices
	{
		Indices(const VkObj<VkDevice>& in_device)
			: m_count()
			, m_buffer(in_device, vkDestroyBuffer)
			, m_gpuMem(in_device, vkFreeMemory)
		{
#ifdef _DEBUG
			m_buffer.SetDbgName(std::string("IndexBuffer"));
			m_gpuMem.SetDbgName(std::string("IndexMemory"));
#endif // _DEBUG
		}
		uint32_t m_count;
		VkObj<VkBuffer> m_buffer;
		VkObj<VkDeviceMemory> m_gpuMem;
	};

	VulkanMesh(const VkObj<VkDevice>& in_device)
		: m_vertices(in_device)
		, m_indices(in_device)
	{}

	Vertices m_vertices;
	Indices  m_indices;

private:

};