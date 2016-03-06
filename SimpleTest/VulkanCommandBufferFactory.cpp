#include "VulkanCommandBufferFactory.h"

VulkanCommandBufferFactory::VulkanCommandBufferFactory(VkDevice in_device)
	: m_device(in_device)
{

}

VkResult VulkanCommandBufferFactory::CreateCommandBuffer(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, VkCommandBuffer& out_buffer)
{
	VkCommandBufferAllocateInfo createInfo = MakeInfoStruct(in_commandPool, in_level);
	return vkAllocateCommandBuffers(m_device, &createInfo, &out_buffer);
}

VkResult VulkanCommandBufferFactory::CreateCommandBuffers(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, std::vector<VkCommandBuffer> out_buffers)
{
	VkCommandBufferAllocateInfo createInfo = MakeInfoStruct(in_commandPool, in_level, static_cast<int>(out_buffers.size()));
	return vkAllocateCommandBuffers(m_device, &createInfo, out_buffers.data());
}

VkCommandBufferAllocateInfo VulkanCommandBufferFactory::MakeInfoStruct(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, int in_bufferCount/* = 1*/)
{
	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.commandPool = in_commandPool;
	commandBufferAllocateInfo.level = in_level;
	commandBufferAllocateInfo.commandBufferCount = in_bufferCount;
	return commandBufferAllocateInfo;
}

