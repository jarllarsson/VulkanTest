#pragma once
#include "vulkan/vulkan.h"


class VulkanGraphics
{
public:
	VulkanGraphics();
	~VulkanGraphics();
private:
	void Init();
	void Destroy();
	VkResult CreateInstance();
	uint32_t GetGraphicsQueueInternalIndex(const VkPhysicalDevice in_physicalDevice) const;
	VkResult CreateLogicalDevice(VkPhysicalDevice in_physicalDevice, uint32_t in_graphicsQueueIdx, VkDevice* out_device);

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
};