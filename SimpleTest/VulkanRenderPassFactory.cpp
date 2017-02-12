#include "VulkanRenderPassFactory.h"

VulkanRenderPassFactory::VulkanRenderPassFactory(VkDevice in_device)
	: m_device(in_device)
{
}

VkResult VulkanRenderPassFactory::CreateStandardRenderPass(VkFormat in_colorFormat, VkFormat in_depthFormat, VkRenderPass& out_renderPass)
{
	const int colIdx = 0; // color attachment
	const int dsIdx = 1;  // depth attachment
	const int attachmentCount = 2;
	// Color and Depth stencil settings
	VkAttachmentDescription attachments[attachmentCount];
	attachments[colIdx].flags = 0;
	attachments[colIdx].format = in_colorFormat;
	attachments[colIdx].samples = VK_SAMPLE_COUNT_1_BIT; // No multisampling (1 sample)
	attachments[colIdx].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Before rendering (clear existing)
	attachments[colIdx].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // After rendering (keep for presenting)
	attachments[colIdx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Before rendering (not doing anything with stencil, so don't care)
	attachments[colIdx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // After rendering (not doing anything with stencil, so don't care)
	attachments[colIdx].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Layout at render pass start. Initial doesn't matter, so we use undefined
	attachments[colIdx].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Layout to which the attachment is transitioned when the render pass is finished. As we want to present the color buffer to the swapchain, we transition to PRESENT_KHR

	attachments[dsIdx].flags = 0;
	attachments[dsIdx].format = in_depthFormat;
	attachments[dsIdx].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[dsIdx].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[dsIdx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[dsIdx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[dsIdx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[dsIdx].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[dsIdx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Transition to depth/stencil attachment

	// Reference to the previous attachment indices
	VkAttachmentReference colorReference = {};
	colorReference.attachment = colIdx; // Color attachment idx
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Attachment layout used as color during the subpass

	VkAttachmentReference depthReference = {};
	depthReference.attachment = dsIdx; // Depth attachment idx
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Single subpass
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.colorAttachmentCount = 1; // Number of color attachments
	subpass.pColorAttachments = &colorReference; // Reference to the color attachment in slot 0
	subpass.pDepthStencilAttachment = &depthReference; // Reference to the depth attachment in slot 1

	subpass.pResolveAttachments = nullptr; // Resolve attachments are resolved at the end of a sub pass and can be used for e.g. multi sampling
	subpass.inputAttachmentCount = 0; // Input attachments can be used to sample from contents of a previous subpass
	subpass.pInputAttachments = nullptr; // Input attachments not used
	subpass.preserveAttachmentCount = 0; // Preserved attachments can be used to loop (and preserve) attachments through subpasses
	subpass.pPreserveAttachments = nullptr; // (Preserve attachments not used by this example)

	// Setup subpass dependencies
	// These will add the implicit attachment layout transitions specified by the attachment descriptions
	// The actual usage layout is preserved through the layout specified in the attachment reference		
	// Each subpass dependency will introduce a memory and execution dependency between the source and dest subpass described by
	// srcStageMask, dstStageMask, srcAccessMask, dstAccessMask (and dependencyFlags is set)
	// Note: VK_SUBPASS_EXTERNAL is a special constant that refers to all commands executed outside of the actual renderpass)
	const uint32_t dependencyCount = 2; // 2 = start of renderpass and end of renderpass (not same as attachmentCount)
	VkSubpassDependency dependencies[dependencyCount];

	// First dependency at the start of the renderpass
	// Does the transition from final to initial layout 
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;								// Producer of the dependency 
	dependencies[0].dstSubpass = 0;													// Consumer is our single subpass that will wait for the execution depdendency
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Second dependency at the end the renderpass
	// Does the transition from the initial to the final layout
	dependencies[1].srcSubpass = 0;													// Producer of the dependency is our single subpass
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;								// Consumer are all commands outside of the renderpass
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;


	// Wrap it all up with the renderpass
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.pNext = nullptr;
	renderPassInfo.attachmentCount = attachmentCount;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1; // Subpass count
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = dependencyCount; // Number of subpass dependencies
	renderPassInfo.pDependencies = dependencies; // Subpass dependencies used by the render pass

	return vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &out_renderPass);
}
