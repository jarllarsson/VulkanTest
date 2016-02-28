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
	VkResult CreateInstance();
	uint32_t GetGraphicsQueueInternalIndex(const VkPhysicalDevice in_physicalDevice) const;
	VkResult CreateLogicalDevice(VkPhysicalDevice in_physicalDevice, uint32_t in_graphicsQueueIdx, VkDevice* out_device);
	bool     SetDepthFormat(VkPhysicalDevice in_physicalDevice, VkFormat* out_format);

	// The Vulkan instance
	VkInstance m_vulkanInstance;

	// Physical device object (ie. the real gpu)
	VkPhysicalDevice m_physicalDevice;
	// Available memory properties for the physical device
	VkPhysicalDeviceMemoryProperties m_physicalDeviceMemProp;

	// Logical device object (the app's view of the gpu)
	VkDevice m_device;
	// Handle to the device command buffer graphics queue
	VkQueue m_queue;
	// Depth buffer format
	VkFormat m_depthFormat;

	// Container for very basic swap chain functionality
	std::shared_ptr<VulkanSwapChain> m_swapChain;

	// Render size
	uint32_t m_width, m_height;
};