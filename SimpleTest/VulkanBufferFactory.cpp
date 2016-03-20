#include "VulkanBufferFactory.h"
#include "ErrorReporting.h"
#include "VulkanMemoryHelper.h"

VulkanBufferFactory::VulkanBufferFactory(VkDevice in_device)
	: m_device(in_device)
{

}

bool VulkanBufferFactory::CreateBuffer(VkBufferUsageFlags in_usage, 
	VkDeviceSize in_size, 
	void* in_data,
	const std::shared_ptr<VulkanMemoryHelper> in_memory,
	VkBuffer& out_buffer,
	VkDeviceMemory& out_allocatedDeviceMemory)
{
	if (in_memory == nullptr) return false;

	VkMemoryRequirements memoryRequirements;

	// Creation information structs
	static VkMemoryAllocateInfo memoryAllocationInfo = {};
	memoryAllocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocationInfo.pNext = NULL;
	memoryAllocationInfo.allocationSize = 0; // set later
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
	in_memory->GetMemoryType(memoryRequirements.memoryTypeBits, 
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
