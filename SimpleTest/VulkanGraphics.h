#pragma once
#include "vulkan/vulkan.h"
#include "VulkanDepthStencil.h"
#include <memory>

class VulkanSwapChain;
class VulkanCommandBufferFactory;
class VulkanRenderPassFactory;

class VulkanGraphics
{
public:
	VulkanGraphics(HWND in_hWnd, HINSTANCE in_hInstance, uint32_t in_width, uint32_t in_height);
	~VulkanGraphics();
private:
	void Init(HWND in_hWnd, HINSTANCE in_hInstance);
	void Destroy();
	VkResult CreateInstance(VkInstance* out_instance);
	uint32_t GetGraphicsQueueInternalIndex() const;
	VkResult CreateLogicalDevice(uint32_t in_graphicsQueueIdx, VkDevice* out_device);
	bool     GetDepthFormat(VkFormat* out_format);
	VkResult CreateCommandPool(VkCommandPool* out_commandPool);
	void     CreateCommandBuffers();

	// The Vulkan instance
	VkInstance m_vulkanInstance;

	// Physical device object (ie. the real gpu)
	VkPhysicalDevice m_physicalDevice;
	// Available memory properties for the physical device
	VkPhysicalDeviceMemoryProperties m_physicalDeviceMemProp;
	// Logical device object (the app's view of the gpu)
	VkDevice m_device;

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
	VkRenderPass m_renderPass;

	// Command buffer pool, command buffers are allocated from this
	VkCommandPool m_commandPool;
	// Command buffers for rendering
	std::vector<VkCommandBuffer> m_drawCommandBuffers;
	// Command buffer for initializing the depth stencil and swap chain images to the right format on the gpu
	VkCommandBuffer m_swapchainDepthStencilInitializationCommandBuffer = VK_NULL_HANDLE;
	// Command buffer for resetting image formats after presenting
	VkCommandBuffer m_postPresentCommandBuffer = VK_NULL_HANDLE;


	// Container for very basic swap chain functionality
	std::shared_ptr<VulkanSwapChain> m_swapChain;

	// Factories
	std::unique_ptr<VulkanCommandBufferFactory> m_commandBufferFactory;
	std::unique_ptr<VulkanRenderPassFactory> m_renderPassFactory;


	// Render size
	uint32_t m_width, m_height;
};