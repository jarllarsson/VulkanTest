#include "VulkanCommandBufferFactory.h"
#include "ErrorReporting.h"
#include "VulkanSwapChain.h"

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

// Set up swap chain and depth stencil on GPU
void VulkanCommandBufferFactory::ConstructSwapchainDepthStencilInitializationCommandBuffer(
	VkCommandBuffer& inout_buffer
	, std::shared_ptr<VulkanSwapChain> in_swapChain
	, VulkanDepthStencil& in_depthStencil)
{
	// Commandbuffer that initializes the swap chain and depth stencil to the correct image layouts
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	VkResult err = vkBeginCommandBuffer(inout_buffer, &cmdBufInfo);
	if (err) throw ProgramError(std::string("Could not begin the SwapchainDepthStencilInitialization Command buffer"));

	// Swap chain set up on GPU
	std::vector<VulkanSwapChain::SwapChainBuffer>& buffers = in_swapChain.GetBuffers();
	for (auto buf : buffers)
	{
		AddImageLayoutChangeToCommandBuffer(inout_buffer, buf.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR); // image layout: old, new
	}

	// Depth stencil set up on GPU
	AddImageLayoutChangeToCommandBuffer(inout_buffer, in_depthStencil.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR); // image layout: old, new

	err = vkEndCommandBuffer(inout_buffer);
	if (err) throw ProgramError(std::string("Could not end the SwapchainDepthStencilInitialization Command buffer"));
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


void VulkanCommandBufferFactory::AddImageLayoutChangeToCommandBuffer(VkCommandBuffer inout_cmdbuffer, VkImage in_image, VkImageAspectFlags in_aspectMask, VkImageLayout in_oldImageLayout, VkImageLayout in_newImageLayout)
{
	// This method is based on Sacha Willems Vulkan example code (vkTools::setImageLayout)
	// Copyright (C) 2015 by Sascha Willems - www.saschawillems.de
	// Create an image memory barrier for changing the layout of
	// an image and put it into an active command buffer
	// See chapter 11.4 "Image Layout" for details
	// Create an image barrier object
	VkImageMemoryBarrier imageMemoryBarrier = vkTools::initializers::imageMemoryBarrier();
	imageMemoryBarrier.oldLayout = in_oldImageLayout;
	imageMemoryBarrier.newLayout = in_newImageLayout;
	imageMemoryBarrier.image = in_image;
	imageMemoryBarrier.subresourceRange.aspectMask = in_aspectMask;
	imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
	imageMemoryBarrier.subresourceRange.levelCount = 1;
	imageMemoryBarrier.subresourceRange.layerCount = 1;

	// Source layouts (old)

	// Undefined layout
	// Only allowed as initial layout!
	// Make sure any writes to the image have been finished
	if (in_oldImageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
	{
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// Old layout is color attachment
	// Make sure any writes to the color buffer have been finished
	if (in_oldImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}

	// Old layout is transfer source
	// Make sure any reads from the image have been finished
	if (in_oldImageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}

	// Old layout is shader read (sampler, input attachment)
	// Make sure any shader reads from the image have been finished
	if (in_oldImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}

	// Target layouts (new)

	// New layout is transfer destination (copy, blit)
	// Make sure any copyies to the image have been finished
	if (in_newImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// New layout is transfer source (copy, blit)
	// Make sure any reads from and writes to the image have been finished
	if (in_newImageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		imageMemoryBarrier.srcAccessMask = imageMemoryBarrier.srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}

	// New layout is color attachment
	// Make sure any writes to the color buffer hav been finished
	if (in_newImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}

	// New layout is depth attachment
	// Make sure any writes to depth/stencil buffer have been finished
	if (in_newImageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	{
		imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	// New layout is shader read (sampler, input attachment)
	// Make sure any writes to the image have been finished
	if (in_newImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}


	// Put barrier on top
	VkPipelineStageFlags srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags destStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	// Put barrier inside setup command buffer
	vkCmdPipelineBarrier(
		inout_cmdbuffer,
		srcStageFlags,
		destStageFlags,
		0,
		0, nullptr,
		0, nullptr,
		1, &imageMemoryBarrier);
}