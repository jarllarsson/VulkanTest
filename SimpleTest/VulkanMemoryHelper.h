#pragma once

#include "vulkan/vulkan.h"

class VulkanMemoryHelper
{
public:
	VulkanMemoryHelper(VkPhysicalDevice in_physicalDevice);
	~VulkanMemoryHelper();

	VkBool32 GetMemoryType(uint32_t typeBits, VkFlags properties, uint32_t * typeIndex) const;
	VkPhysicalDeviceMemoryProperties GetAvailableMemoryProperties() { return m_physicalDeviceMemProp; }
private:
	// Available memory properties for the physical device
	VkPhysicalDeviceMemoryProperties m_physicalDeviceMemProp;
};