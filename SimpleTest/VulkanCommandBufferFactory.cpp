#include "VulkanCommandBufferFactory.h"
#include "ErrorReporting.h"
#include "VulkanDepthStencil.h"
#include "VulkanSwapChain.h"
#include "vulkantools.h"


VulkanCommandBufferFactory::DrawCommandBufferDependencies::DrawCommandBufferDependencies(VkPipelineLayout& in_pipelineLayout, VkPipeline& in_pipeline, 
	std::vector<VkDescriptorSet>& in_descriptorSets, int in_vertexBufferBindId, VulkanMesh& in_mesh, VulkanSwapChain& in_swapChain)
	: m_pipelineLayout(in_pipelineLayout)
	, m_pipeline(in_pipeline)
	, m_descriptorSets(in_descriptorSets)
	, m_vertexBufferBindId(in_vertexBufferBindId)
	, m_mesh(in_mesh)
	, m_swapChain(in_swapChain)
{
}


VulkanCommandBufferFactory::VulkanCommandBufferFactory(VkDevice in_device)
	: m_device(in_device)
{
}

VkResult VulkanCommandBufferFactory::CreateCommandBuffer(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, VkCommandBuffer& out_buffer)
{
	VkCommandBufferAllocateInfo createInfo = MakeInfoStruct(in_commandPool, in_level);
	return vkAllocateCommandBuffers(m_device, &createInfo, &out_buffer);
}

VkResult VulkanCommandBufferFactory::CreateCommandBuffers(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, std::vector<VkCommandBuffer>& out_buffers)
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
	if (err) throw ProgramError(std::string("Begin swapchain's depthstencil-initialization command buffer: ") + vkTools::errorString(err));

	// Swap chain set up on GPU
	std::vector<VulkanSwapChain::SwapChainBuffer>& buffers = in_swapChain->GetBuffers();
	for (auto buf : buffers)
	{
		AddImageLayoutChangeToCommandBuffer(inout_buffer, buf.m_image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR); // image layout: old, new
	}

	// Depth stencil set up on GPU
	AddImageLayoutChangeToCommandBuffer(inout_buffer, in_depthStencil.m_image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL); // image layout: old, new

	err = vkEndCommandBuffer(inout_buffer);
	if (err) throw ProgramError(std::string("End swapchain's depthstencil-initialization command buffer: ") + vkTools::errorString(err));
}

// Constructs command buffers for drawing to target frame buffers
void VulkanCommandBufferFactory::ConstructDrawCommandBuffer(std::vector<VkCommandBuffer>& inout_buffers, const std::vector<VkFramebuffer>& in_targetFrameBuffers, 
	DrawCommandBufferDependencies& in_dependencyObjects,
	const VkRenderPass& in_renderPass, const VkClearColorValue& in_clearColor,
	int in_width, int in_height)
{
	// The following buffer lists should all be of the same size, as they represent the size of the buffers in the swap chain
	if (inout_buffers.size() != in_targetFrameBuffers.size()) throw ProgramError(std::string("ConstructDrawCommandBuffer: Frame buffer size not equal to command buffer size."));
	
	std::vector<VulkanSwapChain::SwapChainBuffer>& swapchainBuffers = in_dependencyObjects.m_swapChain.GetBuffers();
	if (inout_buffers.size() != in_dependencyObjects.m_swapChain.GetBuffersCount()) throw ProgramError(std::string("ConstructDrawCommandBuffer: Swap chain buffer size not equal to command buffer size."));

	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	VkClearValue clearValues[2];
	clearValues[0].color = in_clearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = in_renderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = in_width;
	renderPassBeginInfo.renderArea.extent.height = in_height;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;

	VkResult err;

	for (int32_t i = 0; i < inout_buffers.size(); ++i)
	{
		// Set target frame buffer
		renderPassBeginInfo.framebuffer = in_targetFrameBuffers[i];
		// Begin new command buffer for the target frame buffer
		err = vkBeginCommandBuffer(inout_buffers[i], &cmdBufInfo);
		if (err) throw ProgramError(std::string("Begin command buffer for drawing to frame buffer") + std::to_string(i) + ": " + vkTools::errorString(err));

		vkCmdBeginRenderPass(inout_buffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Update dynamic viewport state
		VkViewport viewport = {};
		viewport.width = static_cast<float>(in_width);
		viewport.height = static_cast<float>(in_height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(inout_buffers[i], 0, 1, &viewport);

		// Update dynamic scissor state
		VkRect2D scissor = {};
		scissor.extent.width = in_width;
		scissor.extent.height = in_height;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		vkCmdSetScissor(inout_buffers[i], 0, 1, &scissor);

		// Bind descriptor sets describing shader binding points
		vkCmdBindDescriptorSets(inout_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, 
			in_dependencyObjects.m_pipelineLayout, 
			0, static_cast<uint32_t>(in_dependencyObjects.m_descriptorSets.size()), in_dependencyObjects.m_descriptorSets.data(), 0, nullptr);

		// Bind the rendering pipeline (including the shaders)
		vkCmdBindPipeline(inout_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, in_dependencyObjects.m_pipeline);

		// Draw mesh!
		// -----------------------------------------------------------
		VulkanMesh& mesh = in_dependencyObjects.m_mesh;
		// Bind triangle vertices
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(inout_buffers[i], in_dependencyObjects.m_vertexBufferBindId, 
			mesh.m_vertices.m_count, &mesh.m_vertices.m_buffer, offsets);

		// Bind triangle indices
		vkCmdBindIndexBuffer(inout_buffers[i], mesh.m_indices.m_buffer, 0, VK_INDEX_TYPE_UINT32);

		// Draw indexed triangle
		vkCmdDrawIndexed(inout_buffers[i], mesh.m_indices.m_count, 
			1, // Instance count
			0, // Index offset
			0, // Vertex offset (added to value from index buffer)
			1); // Not sure about this one.. "Specifies the starting value of the internally generated instance count."
		// -----------------------------------------------------------

		vkCmdEndRenderPass(inout_buffers[i]);

		// Add a present memory barrier to the end of the command buffer
		// This will transform the frame buffer color attachment to a
		// new layout for presenting it to the windowing system integration 
		VkImageMemoryBarrier prePresentBarrier = {};
		prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		prePresentBarrier.pNext = NULL;
		prePresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		prePresentBarrier.dstAccessMask = 0;
		prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prePresentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		prePresentBarrier.image = swapchainBuffers[i].m_image;

		VkImageMemoryBarrier *pMemoryBarrier = &prePresentBarrier;
		vkCmdPipelineBarrier(
			inout_buffers[i],
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_FLAGS_NONE,
			0, nullptr,
			0, nullptr,
			1, &prePresentBarrier);

		err = vkEndCommandBuffer(inout_buffers[i]);
		if (err) throw ProgramError(std::string("End command buffer for drawing to frame buffer") + std::to_string(i) + ": " + vkTools::errorString(err));
	}
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
