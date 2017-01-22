#include "VulkanRenderPassFactory.h"

VulkanRenderPassFactory::VulkanRenderPassFactory(VkDevice in_device)
	: m_device(in_device)
{
}

VkResult VulkanRenderPassFactory::CreateStandardRenderPass(VkFormat in_colorFormat, VkFormat in_depthFormat, VkRenderPass& out_renderPass)
{
	const int colIdx = 0;
	const int dsIdx = 1;
	const int attachmentCount = 2;
	// Color and Depth stencil settings
	VkAttachmentDescription attachments[attachmentCount];
	attachments[colIdx].format = in_colorFormat;
	attachments[colIdx].samples = VK_SAMPLE_COUNT_1_BIT; // No multisampling (1 sample)
	attachments[colIdx].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Before rendering (clear existing)
	attachments[colIdx].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // After rendering (keep for presenting)
	attachments[colIdx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Before rendering (not doing anything with stencil, so don't care)
	attachments[colIdx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // After rendering (not doing anything with stencil, so don't care)
	attachments[colIdx].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[colIdx].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachments[dsIdx].format = in_depthFormat;
	attachments[dsIdx].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[dsIdx].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[dsIdx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[dsIdx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[dsIdx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[dsIdx].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[dsIdx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Reference to the previous attachment indices
	VkAttachmentReference colorReference = {};
	colorReference.attachment = colIdx; // Color attachment idx
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference = {};
	depthReference.attachment = dsIdx; // Depth attachment idx
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Single subpass
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = 1; // Number of color attachments
	subpass.pColorAttachments = &colorReference;
	subpass.pResolveAttachments = NULL;
	subpass.pDepthStencilAttachment = &depthReference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	// Wrap it all up
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.pNext = NULL;
	renderPassInfo.attachmentCount = attachmentCount;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1; // Subpass count
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 0;
	renderPassInfo.pDependencies = NULL;

	return vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &out_renderPass);
}
