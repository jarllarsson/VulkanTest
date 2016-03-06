#pragma once
#include "vulkan/vulkan.h"
#include <vector>

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

	// Index of the queue capable of graphics and presenting
	uint32_t m_queueNodeIndex = UINT32_MAX;

	VkFormat        m_colorFormat;
	VkColorSpaceKHR m_colorSpace;
};