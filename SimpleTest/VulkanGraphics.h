#pragma once
#include "vulkan/vulkan.h"
#include <memory>

#include "VulkanSwapChain.h"

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
	// Depth buffer format
	VkFormat m_depthFormat;

	// Command buffer pool, command buffers are allocated from this
	VkCommandPool m_commandPool;
	// Command buffers for rendering
	std::vector<VkCommandBuffer> m_drawCommandBuffers;
	// Command buffer for resetting after presenting
	VkCommandBuffer m_postPresentCommandBuffer = VK_NULL_HANDLE;


	// Container for very basic swap chain functionality
	std::shared_ptr<VulkanSwapChain> m_swapChain;

	// Render size
	uint32_t m_width, m_height;
};