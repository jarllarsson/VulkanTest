#include "VulkanMemoryHelper.h"


VulkanMemoryHelper::VulkanMemoryHelper(VkPhysicalDevice in_physicalDevice)
{
	vkGetPhysicalDeviceMemoryProperties(in_physicalDevice, &m_physicalDeviceMemProp);
}

VulkanMemoryHelper::~VulkanMemoryHelper()
{
}

VkBool32 VulkanMemoryHelper::GetMemoryType(uint32_t typeBits, VkFlags properties, uint32_t * typeIndex) const
{
	for (uint32_t i = 0; i < 32; i++)
	{
		if ((typeBits & 1) == 1)
		{
			if ((m_physicalDeviceMemProp.memoryTypes[i].propertyFlags & properties) == properties)
			{
				*typeIndex = i;
				return true;
			}
		}
		typeBits >>= 1;
	}
	return false;
}
