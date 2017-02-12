#include "VulkanBufferFactory.h"
#include "ErrorReporting.h"
#include "VulkanMemoryHelper.h"
#include "Vertex.h"
#include "VulkanMesh.h"
#include "VulkanUniformBufferPerFrame.h"

VulkanBufferFactory::VulkanBufferFactory(VkDevice in_device, std::shared_ptr<VulkanMemoryHelper> in_memory)
	: m_device(in_device)
	, m_memory(in_memory)
{

}

void VulkanBufferFactory::CreateTriangle(VulkanMesh& out_mesh) const
{
	// Data
	
	// Setup vertex data for a triangle
	std::vector<Vertex> vertexData = {
		{ { 1.0f,  1.0f, 0.0f },{ 1.0f, 0.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.0f },{ 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, -1.0f, 0.0f },{ 0.0f, 0.0f, 1.0f } }
	};
	uint32_t vertexCount = static_cast<uint32_t>(vertexData.size());
	uint32_t vertexBufferByteSize = vertexCount * sizeof(Vertex);

	// Setup index data for triangle
	std::vector<uint32_t> indexData = { 0, 1, 2 };
	uint32_t indexCount = static_cast<uint32_t>(indexData.size());
	int indexBufferByteSize = indexCount * sizeof(uint32_t);

	// Buffers

	// TODO: staging buffers

	// Create vertex buffer
	if (CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		vertexBufferByteSize,
		reinterpret_cast<void*>(vertexData.data()),
		*out_mesh.m_vertices.m_buffer.Replace(),
		*out_mesh.m_vertices.m_gpuMem.Replace()))
	{
		out_mesh.m_vertices.m_count = vertexCount;
	}

	// Create index buffer
	if (CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		indexBufferByteSize,
		reinterpret_cast<void*>(indexData.data()),
		*out_mesh.m_indices.m_buffer.Replace(),
		*out_mesh.m_indices.m_gpuMem.Replace()))
	{
		out_mesh.m_indices.m_count = indexCount;
	}
}

void VulkanBufferFactory::CreateUniformBufferPerFrame(VulkanUniformBufferPerFrame& out_buffer,
	const glm::mat4& in_projMat, const glm::mat4& in_worldMat, const glm::mat4 in_viewMat) const
{
	out_buffer.m_data.m_projectionMatrix = in_projMat;
	out_buffer.m_data.m_worldMatrix = in_worldMat;
	out_buffer.m_data.m_viewMatrix = in_viewMat;

	VkDeviceSize dataSize = sizeof(out_buffer.m_data);

	// Specify creation of uniform buffer
	if (CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		dataSize,
		reinterpret_cast<void*>(&out_buffer.m_data),
		*out_buffer.m_allocation.m_buffer.Replace(),
		*out_buffer.m_allocation.m_gpuMem.Replace()))
	{
		// Store buffer information in the descriptor
		out_buffer.m_allocation.m_descriptorBufferInfo.buffer = out_buffer.m_allocation.m_buffer;
		out_buffer.m_allocation.m_descriptorBufferInfo.offset = 0;
		out_buffer.m_allocation.m_descriptorBufferInfo.range = dataSize;
	}
}

bool VulkanBufferFactory::CreateBuffer(VkBufferUsageFlags in_usage,
	VkDeviceSize in_size, 
	void* in_data,
	VkBuffer& out_buffer,
	VkDeviceMemory& out_allocatedDeviceMemory) const
{
	if (m_memory == nullptr) return false;

	VkMemoryRequirements memoryRequirements;

	// Creation information structs
	static VkMemoryAllocateInfo memoryAllocationInfo = {};
	memoryAllocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocationInfo.pNext = nullptr;
	memoryAllocationInfo.allocationSize = 0; // is set later when we have the memory requirements
	memoryAllocationInfo.memoryTypeIndex = 0;

	VkBufferCreateInfo bufCreateInfo = {};
	bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufCreateInfo.pNext = nullptr;
	bufCreateInfo.usage = in_usage;
	bufCreateInfo.size = in_size;
	bufCreateInfo.flags = 0;

	// Create the buffer object
	VkResult err = vkCreateBuffer(m_device, &bufCreateInfo, nullptr, &out_buffer);
	ERROR_IF(err, "Create buffer");

	// Allocate memory on gpu
	vkGetBufferMemoryRequirements(m_device, out_buffer, &memoryRequirements);
	memoryAllocationInfo.allocationSize = memoryRequirements.size;
	// Get the appropriate memory type for allocation, where the memory is only visible to the host
	m_memory->GetMemoryType(memoryRequirements.memoryTypeBits, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 
		&memoryAllocationInfo.memoryTypeIndex);
	err = vkAllocateMemory(m_device, &memoryAllocationInfo, nullptr, &out_allocatedDeviceMemory);
	ERROR_IF(err, "Allocate memory on device for buffer");

	// If we have initialization data, then copy it to the gpu
	if (in_data != nullptr)
	{
		// TODO: Maybe move map/unmap operations to a helper class or make a generic buffer base class that has this sort of stuff
		void *mapped;
		err = vkMapMemory(m_device, out_allocatedDeviceMemory, 0, memoryAllocationInfo.allocationSize, 0, &mapped);
		ERROR_IF(err, "Map data for buffer");
		memcpy(mapped, in_data, in_size);
		vkUnmapMemory(m_device, out_allocatedDeviceMemory);
	}

	// Bind buffer
	err = vkBindBufferMemory(m_device, out_buffer, out_allocatedDeviceMemory, 0);
	ERROR_IF(err, "Bind buffer");

	return true;
}
