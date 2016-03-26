#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <memory>
#include "MathTypes.h"

class VulkanMemoryHelper;
class VulkanMesh;
struct VulkanUniformBufferPerFrame;

class VulkanBufferFactory
{
public:
	VulkanBufferFactory(VkDevice in_device, std::shared_ptr<VulkanMemoryHelper> in_memory);

	void CreateTriangle(VulkanMesh& out_mesh) const;
	
	void CreateUniformBufferPerFrame(VulkanUniformBufferPerFrame& out_buffer,
		const glm::mat4& in_projMat, const glm::mat4& in_worldMat, const glm::mat4 in_viewMat) const;
	
	// Create a buffer, allocate gpu memory, copy optional init data and bind the buffer
	bool CreateBuffer(VkBufferUsageFlags in_usage,
		VkDeviceSize in_size,
		void* in_data,
		VkBuffer& out_buffer,
		VkDeviceMemory& out_allocatedDeviceMemory) const;

private:
	VkDevice m_device;
	std::shared_ptr<VulkanMemoryHelper> m_memory;
};