#pragma once
#include <memory>
#include <vector>
#include "MathTypes.h"
#include "vulkan/vulkan.h"
#include "VulkanDepthStencil.h"
#include "VkObj.h"


class VulkanSwapChain;
class VulkanCommandBufferFactory;
class VulkanRenderPassFactory;
class VulkanBufferFactory;
class VulkanMemoryHelper;

struct VulkanVertexLayout;
class VulkanMesh;

struct VulkanUniformBufferPerFrame;

/*!
 * \class VulkanGraphics
 *
 * \brief 
 * 
 * Test class for learning Vulkan.
 * Some short descriptions of various terms (filling these out while I go):
 * 
 * Descriptor - A vulkan base binding type. (Ie. a constant buffer or sampler) Are bound in 
 *              blocks called descriptor sets (Which are described by descriptor set layouts: sorta like structs)
 * 
 *
 * \author Jarl
 * \date 2016
 */


class VulkanGraphics
{
public:
	VulkanGraphics(HWND in_hWnd, HINSTANCE in_hInstance, uint32_t in_width, uint32_t in_height);
	~VulkanGraphics();

	void Render();
private:
	// General
	void     Init(HWND in_hWnd, HINSTANCE in_hInstance);
	VkResult CreateInstance(VkInstance* out_instance);

	void     Destroy();
	void     DestroyCommandBuffers();

	uint32_t GetGraphicsQueueInternalIndex() const;
	VkResult CreateLogicalDevice(uint32_t in_graphicsQueueIdx, VkDevice* out_device);
	bool     GetDepthFormat(VkFormat* out_format) const;
	VkResult CreateCommandPool(VkCommandPool* out_commandPool);
	void     AllocateRenderCommandBuffers();
	void     SubmitCommandBufferAndAppendWaitToQueue(VkCommandBuffer in_commandBuffer);
	VkResult CreatePipelineCache();
	void     CreateFrameBuffers();
	void     CreateSemaphores();

	VkDescriptorSetLayoutBinding    CreateDescriptorSetLayoutBinding(uint32_t in_descriptorBindingId, VkDescriptorType in_type, VkShaderStageFlags in_shaderStageFlags);
	VkDescriptorSetLayoutCreateInfo CreateDescriptorSetLayoutCreateInfo(const std::vector<VkDescriptorSetLayoutBinding>& in_bindings);


	// Rendering
	void CreateTriangleProgramVertexLayouts();
	void CreateTriangleProgramUniformBuffers();
	void CreateTriangleProgramDescriptorSetLayout();
	void CreateTriangleProgramDescriptorPool();
	void CreateTriangleProgramDescriptorSet();
	void Draw();

	// TODO: Maybe move out to factory?:
	void CreatePipelineLayout(const VkDescriptorSetLayout& in_descriptorSetLayout, VkPipelineLayout& out_pipelineLayout);
	void CreateTriangleProgramPipelineAndLoadShaders();




	// Data
	// Here VkObj wrappers are used for destroy-required Vulkan objects, 
	// it ensures their destruction when this instance is destroyed.
	// Some lists still uses explicit removal until I've decided the best way to handle them

	// The Vulkan instance
	VkObj<VkInstance> m_vulkanInstance;
	// Physical device object (ie. the real gpu)
	VkPhysicalDevice m_physicalDevice; // Destroyed when instance is destroyed

	// Vulkan memory handler
	std::shared_ptr<VulkanMemoryHelper> m_memory;
	// Logical device object (the app's view of the gpu)
	VkObj<VkDevice> m_device;

	// Queue supporting graphics
	uint32_t m_graphicsQueueIdx;
	// Handle to the device command buffer graphics queue
	VkQueue m_queue;
	// Color and depth buffer format
	VkFormat m_colorformat;
	VkFormat m_depthFormat;
	// Depth stencil object
	VulkanDepthStencil m_depthStencil;
	// Render pass for frame buffer writing
	VkObj<VkRenderPass> m_renderPass;

	// Command buffer pool, command buffers are allocated from this
	VkObj<VkCommandPool> m_commandPool;


	// Command buffers for presenting, one for each frame buffer (for double buffered draw for example)
	// They each store separate references to frame buffer id's
	// - Command buffers for rendering
	std::vector<VkCommandBuffer> m_drawCommandBuffers;
	// - Command buffers for resetting image formats after presenting
	std::vector<VkCommandBuffer> m_postPresentCommandBuffers;

	// Container for very basic swap chain functionality
	std::shared_ptr<VulkanSwapChain> m_swapChain;

	// Frame buffer for the swap chain images
	std::vector<VkFramebuffer> m_frameBuffers;
	uint32_t m_currentFrameBufferIdx;

	// Factories
	std::unique_ptr<VulkanCommandBufferFactory> m_commandBufferFactory;
	std::unique_ptr<VulkanRenderPassFactory>    m_renderPassFactory;
	std::unique_ptr<VulkanDepthStencilFactory>  m_depthStencilFactory;
	std::unique_ptr<VulkanBufferFactory>        m_bufferFactory;

	// Geometry
	std::shared_ptr<VulkanVertexLayout> m_simpleVertexLayout;
	std::shared_ptr<VulkanMesh> m_triangleMesh;

	// Uniform buffers (think sorta like constant buffers in DX)
	std::shared_ptr<VulkanUniformBufferPerFrame> m_ubufPerFrame;
	glm::vec3 m_rotation; // temp rotation vector of view 

	// Pipeline layout
	VkObj<VkPipelineLayout> m_pipelineLayout_TriangleProgram;
	// Pipeline cache
	VkObj<VkPipelineCache> m_pipelineCache;
	// Pipeline
	VkObj<VkPipeline> m_pipeline_TriangleProgram;

	// Semaphores
	VkObj<VkSemaphore> m_presentComplete;
	VkObj<VkSemaphore> m_renderComplete;

	// Descriptor sets
	VkDescriptorSet                 m_descriptorSetPerFrame; // All descriptors to be used per frame
	VkObj<VkDescriptorSetLayout>    m_descriptorSetLayoutPerFrame_TriangleProgram;
	// Descriptor set pool
	VkObj<VkDescriptorPool>  m_descriptorPool;

	// Shaders created (we need to store these for proper cleanup)
	typedef VkObj<VkShaderModule>             ShaderModuleType;
	typedef std::unique_ptr<ShaderModuleType> ShaderModulePtr;
	std::vector<ShaderModulePtr> m_shaderModules;


	// Render size
	uint32_t m_width, m_height;
};