#pragma once
#include "vulkan/vulkan.h"
#include <vector>


// Macro to get a procedure address based on a vulkan instance
#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                        \
{                                                                       \
    fp##entrypoint = (PFN_vk##entrypoint) vkGetInstanceProcAddr(inst, "vk"#entrypoint); \
    if (fp##entrypoint == NULL)                                         \
	{																    \
        exit(1);                                                        \
    }                                                                   \
}

// Macro to get a procedure address based on a vulkan device
#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                           \
{                                                                       \
    fp##entrypoint = (PFN_vk##entrypoint) vkGetDeviceProcAddr(dev, "vk"#entrypoint);   \
    if (fp##entrypoint == NULL)                                         \
	{																    \
        exit(1);                                                        \
    }                                                                   \
}


class VulkanSwapChain
{
public:
	struct SwapChainBuffer
	{
		VkImage m_image;
		VkImageView m_imageView;
	};

	VulkanSwapChain(VkInstance in_vulkanInstance, VkPhysicalDevice in_physicalDevice, VkDevice in_device,
		            uint32_t* in_width, uint32_t* in_height,
	                void* in_platformHandle, void* in_platformWindow, 
	                VkSwapchainKHR in_oldSwapChain = VK_NULL_HANDLE);

	~VulkanSwapChain();

	// For initializing or re-initializing the surface and swap chain (will auto clear if already exists)
	void SetupSurfaceAndSwapChain(VkPhysicalDevice in_physicalDevice, VkSwapchainKHR in_oldSwapChain,
	                              uint32_t* in_width, uint32_t* in_height);

	// Get buffer
	std::vector<SwapChainBuffer>& GetBuffers() { return m_buffers; }

	// Acquire the next image in the swapchain for rendering
	VkResult NextImage(VkSemaphore in_semPresentIsComplete, uint32_t* inout_currentBufferIdx);

	// Present current image to specified queue
	VkResult Present(VkQueue in_queue, uint32_t in_currentBufferIdx);

	int      GetBuffersCount() const;

private:
	void CreateBuffers();

	VkInstance m_vulkanInstance;
	VkDevice   m_device;

	VkSurfaceKHR    m_surface;
	VkSwapchainKHR  m_swapChain;
	std::vector<SwapChainBuffer> m_buffers; // The buffers we render to and flip between

	VkFormat        m_colorFormat;
	VkColorSpaceKHR m_colorSpace;


	// Function pointers
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
	PFN_vkQueuePresentKHR fpQueuePresentKHR;
};