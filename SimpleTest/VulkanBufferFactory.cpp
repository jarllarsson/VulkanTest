#include "VulkanBufferFactory.h"
#include "ErrorReporting.h"
#include "VulkanMemoryHelper.h"
#include "Vertex.h"
#include "VulkanMesh.h"

VulkanBufferFactory::VulkanBufferFactory(VkDevice in_device, std::shared_ptr<VulkanMemoryHelper> in_memory)
	: m_device(in_device)
	, m_memory(in_memory)
{

}

void VulkanBufferFactory::CreateTriangle(VulkanMesh& out_mesh)
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

	// Create vertex buffer
	if (CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		vertexBufferByteSize,
		reinterpret_cast<void*>(vertexData.data()),
		out_mesh.m_vertices.m_buffer,
		out_mesh.m_vertices.m_memory))
	{
		out_mesh.m_vertices.m_count = vertexCount;
	}

	// Create index buffer
	if (CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		indexBufferByteSize,
		reinterpret_cast<void*>(indexData.data()),
		out_mesh.m_indices.m_buffer,
		out_mesh.m_indices.m_memory))
	{
		out_mesh.m_indices.m_count = indexCount;
	}

	// Binding


}

bool VulkanBufferFactory::CreateBuffer(VkBufferUsageFlags in_usage,
	VkDeviceSize in_size, 
	void* in_data,
	VkBuffer& out_buffer,
	VkDeviceMemory& out_allocatedDeviceMemory)
{
	if (m_memory == nullptr) return false;

	VkMemoryRequirements memoryRequirements;

	// Creation information structs
	static VkMemoryAllocateInfo memoryAllocationInfo = {};
	memoryAllocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocationInfo.pNext = NULL;
	memoryAllocationInfo.allocationSize = 0; // is set later when we have the memory requirements
	memoryAllocationInfo.memoryTypeIndex = 0;

	VkBufferCreateInfo bufCreateInfo = {};
	bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufCreateInfo.pNext = NULL;
	bufCreateInfo.usage = in_usage;
	bufCreateInfo.size = in_size;
	bufCreateInfo.flags = 0;

	// Create the buffer object
	VkResult err = vkCreateBuffer(m_device, &bufCreateInfo, nullptr, &out_buffer);
	if (err) throw ProgramError(std::string("Could not create buffer"));

	// Allocate memory on gpu
	vkGetBufferMemoryRequirements(m_device, out_buffer, &memoryRequirements);
	memoryAllocationInfo.allocationSize = memoryRequirements.size;
	m_memory->GetMemoryType(memoryRequirements.memoryTypeBits, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 
		&memoryAllocationInfo.memoryTypeIndex);
	err = vkAllocateMemory(m_device, &memoryAllocationInfo, nullptr, &out_allocatedDeviceMemory);
	if (err) throw ProgramError(std::string("Could not allocate memory on device for buffer"));

	// If we have initialization data, then copy it to the gpu
	if (in_data != nullptr)
	{
		void *mapped;
		err = vkMapMemory(m_device, out_allocatedDeviceMemory, 0, in_size, 0, &mapped);
		if (err) throw ProgramError(std::string("Could not map data for buffer"));
		memcpy(mapped, in_data, in_size);
		vkUnmapMemory(m_device, out_allocatedDeviceMemory);
	}

	// Bind buffer
	err = vkBindBufferMemory(m_device, out_buffer, out_allocatedDeviceMemory, 0);
	if (err) throw ProgramError(std::string("Could not bind buffer"));

	return true;
}
