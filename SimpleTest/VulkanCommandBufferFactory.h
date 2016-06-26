#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <memory>
#include "VulkanMesh.h"

class VulkanSwapChain;
struct VulkanDepthStencil;

class VulkanCommandBufferFactory
{
public:
	// Information used for drawing
	struct DrawCommandBufferDependencies
	{
	public:
		DrawCommandBufferDependencies(VkPipelineLayout& in_pipelineLayout, VkPipeline& in_pipeline, std::vector<VkDescriptorSet>& in_descriptorSets,
			int in_vertexBufferBindId, VulkanMesh& in_mesh, VulkanSwapChain& in_swapChain);

		// What pipeline layout and pipeline
		VkPipelineLayout&              m_pipelineLayout;
		VkPipeline&                    m_pipeline;
		// Descriptor sets
		std::vector<VkDescriptorSet>&  m_descriptorSets;

		// Mesh to draw
		int m_vertexBufferBindId;
		VulkanMesh& m_mesh;

		// Swap chain
		VulkanSwapChain& m_swapChain;
	};


	VulkanCommandBufferFactory(VkDevice in_device); 

	// Allocations
	VkResult AllocateCommandBuffer(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, VkCommandBuffer& out_buffer);
	VkResult AllocateCommandBuffers(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, std::vector<VkCommandBuffer>& out_buffers);



	// Constructs (needs allocation first)
	void ConstructSwapchainDepthStencilInitializationCommandBuffer(VkCommandBuffer& inout_buffer, 
		std::shared_ptr<VulkanSwapChain> in_swapChain, VulkanDepthStencil& in_depthStencil);

	void ConstructDrawCommandBuffer(std::vector<VkCommandBuffer>& inout_buffers, const std::vector<VkFramebuffer>& in_frameBuffers,
		DrawCommandBufferDependencies& in_dependencyObjects,
		const VkRenderPass& in_renderPass, const VkClearColorValue& in_clearColor,
		int in_width, int in_height);

	void ConstructPostPresentCommandBuffer(std::vector<VkCommandBuffer>& inout_buffers, DrawCommandBufferDependencies& in_dependencyObjects);


private:
	VkCommandBufferAllocateInfo MakeInfoStruct(VkCommandPool in_commandPool, VkCommandBufferLevel in_level, int in_bufferCount = 1);

	VkDevice m_device;

	// Image layout helper
	void AddImageLayoutChangeToCommandBuffer(VkCommandBuffer inout_cmdbuffer, VkImage in_image, VkImageAspectFlags in_aspectMask, VkImageLayout in_oldImageLayout, VkImageLayout in_newImageLayout);
};