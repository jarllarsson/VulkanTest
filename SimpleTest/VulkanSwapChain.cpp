#include "VulkanSwapChain.h"
#include "DebugPrint.h"
#include "vulkantools.h"
#include "ErrorReporting.h"

VulkanSwapChain::VulkanSwapChain(VkInstance in_vulkanInstance, VkPhysicalDevice in_physicalDevice, VkDevice in_device, 
	                             uint32_t* in_width, uint32_t* in_height,
	                             void* in_platformHandle, void* in_platformWindow, VkSwapchainKHR in_oldSwapChain/* = VK_NULL_HANDLE*/)
	: m_vulkanInstance(in_vulkanInstance)
	, m_device(in_device)
	, m_surface(VK_NULL_HANDLE)
	, m_swapChain(VK_NULL_HANDLE)
	, m_buffers()
	, m_colorFormat()
	, m_colorSpace()
{
	VkResult err;

	GET_INSTANCE_PROC_ADDR(m_vulkanInstance, GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR(m_vulkanInstance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR(m_vulkanInstance, GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR(m_vulkanInstance, GetPhysicalDeviceSurfacePresentModesKHR);
	GET_DEVICE_PROC_ADDR(m_device, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(m_device, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(m_device, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(m_device, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(m_device, QueuePresentKHR);

	// Create surface
#ifdef _WIN32
	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.hinstance = reinterpret_cast<HINSTANCE>(in_platformHandle);
	surfaceCreateInfo.hwnd = reinterpret_cast<HWND>(in_platformWindow);
	err = vkCreateWin32SurfaceKHR(in_vulkanInstance, &surfaceCreateInfo, nullptr, &m_surface);
#else
#ifdef __ANDROID__
	VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.window = window;
	err = vkCreateAndroidSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface);
#else
	VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.connection = connection;
	surfaceCreateInfo.window = window;
	err = vkCreateXcbSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#endif
#endif
	if (err)
	{
		DEBUGPRINT(vkTools::errorString(err));
	}

	// Get list of supported surface formats
	uint32_t formatCount = 0;
	err = fpGetPhysicalDeviceSurfaceFormatsKHR(in_physicalDevice, m_surface, &formatCount, NULL);
	if (err) throw ProgramError(std::string("Error when querying surface format count: ") + vkTools::errorString(err));
	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	err = fpGetPhysicalDeviceSurfaceFormatsKHR(in_physicalDevice, m_surface, &formatCount, surfaceFormats.data());
	if (err) throw ProgramError(std::string("Error when querying surface formats: ") + vkTools::errorString(err));

	if (formatCount == 1 && surfaceFormats[0].format == VK_FORMAT_UNDEFINED)
	{
		// If the format list has just one entry and that entry is VK_FORMAT_UNDEFINED,
		// then the surface does not have any preferred format, so just pick one.
		m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else 
	{
		// Otherwise, return first preferred format
		if (formatCount < 1) throw ProgramError(std::string("Error, no surface formats available"));
		m_colorFormat = surfaceFormats[0].format;
	}
	m_colorSpace = surfaceFormats[0].colorSpace;

	// Create the swap chain object and surface
	SetupSurfaceAndSwapChain(in_physicalDevice, in_oldSwapChain, in_width, in_height);

	// Create the buffers we will draw to
	CreateBuffers();
}

VulkanSwapChain::~VulkanSwapChain()
{
	for (auto buffer : m_buffers)
	{
		OutputDebugString("Vulkan: Removing swap chain image view\n");
		vkDestroyImageView(m_device, buffer.m_imageView, nullptr);
	}
	OutputDebugString("Vulkan: Removing swap chain object's SwapchainKHR\n");
	fpDestroySwapchainKHR(m_device, m_swapChain, nullptr);
	OutputDebugString("Vulkan: Removing swap chain object's SurfaceKHR\n");
	vkDestroySurfaceKHR(m_vulkanInstance, m_surface, nullptr);
}

VkResult VulkanSwapChain::NextImage(VkSemaphore in_semPresentIsComplete, uint32_t* inout_currentBufferIdx)
{
	return fpAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, in_semPresentIsComplete, (VkFence)nullptr, inout_currentBufferIdx);
}

VkResult VulkanSwapChain::Present(VkQueue in_queue, uint32_t in_currentBufferIdx, VkSemaphore in_waitSemaphore /*=VK_NULL_HANDLE*/)
{
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = NULL;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_swapChain;
	presentInfo.pImageIndices = &in_currentBufferIdx;
	if (in_waitSemaphore != VK_NULL_HANDLE)
	{
		presentInfo.pWaitSemaphores = &in_waitSemaphore;
		presentInfo.waitSemaphoreCount = 1;
	}
	return fpQueuePresentKHR(in_queue, &presentInfo);
}

int VulkanSwapChain::GetBuffersCount() const
{
	return static_cast<int>(m_buffers.size());
}

void VulkanSwapChain::SetupSurfaceAndSwapChain(VkPhysicalDevice in_physicalDevice, VkSwapchainKHR in_oldSwapChain,
                                               uint32_t *in_width, uint32_t *in_height)
{
	VkResult err;

	// Get physical device surface properties and formats
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	err = fpGetPhysicalDeviceSurfaceCapabilitiesKHR(in_physicalDevice, m_surface, &surfaceCapabilities);
	if (err) throw ProgramError(std::string("Error when querying surface capabilities: ") + vkTools::errorString(err));

	// Get the available present modes on the GPU
	uint32_t presentModeCount;
	err = fpGetPhysicalDeviceSurfacePresentModesKHR(in_physicalDevice, m_surface, &presentModeCount, NULL);
	if (err) throw ProgramError(std::string("Error when querying surface present mode count: ") + vkTools::errorString(err));
	std::vector<VkPresentModeKHR> supportedPresentModes(presentModeCount);
	err = fpGetPhysicalDeviceSurfacePresentModesKHR(in_physicalDevice, m_surface, &presentModeCount, supportedPresentModes.data());
	if (err) throw ProgramError(std::string("Error when querying surface present modes: ") + vkTools::errorString(err));

	// Setup surface extents
	VkExtent2D swapchainExtent = {};
	// if surface size is undefined
	if (surfaceCapabilities.currentExtent.width == -1) // if size is undefined, both axes will be -1
	{
		// Set to requested size
		swapchainExtent.width = *in_width;
		swapchainExtent.height = *in_height;
	}
	else
	{
		// if defined, the swap chain must match, and update our external dimensions. TODO! alert outside of change?
		swapchainExtent = surfaceCapabilities.currentExtent;
		*in_width = surfaceCapabilities.currentExtent.width;
		*in_height = surfaceCapabilities.currentExtent.height;
	}

	// Try to use mailbox mode if possible.
	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync, supported by all vulkan implementations, and our default
	for (size_t i = 0; i < presentModeCount; i++)
	{
		if (supportedPresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR; // Doesn't vsync or tear, more latency than tearing
			break;
		}
		if (swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR && supportedPresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
		{
			swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; // Standard tearing mode.
		}
	}

	// Determine the number of images for swap chain, ie. (2)double- or (3)triple buffering for example
	uint32_t minImageCount = surfaceCapabilities.minImageCount + 1;
	// Cap if more than max allowed amount of images (if defined)
	if ((surfaceCapabilities.maxImageCount > 0) && (minImageCount > surfaceCapabilities.maxImageCount))
	{
		minImageCount = surfaceCapabilities.maxImageCount;
	}

	// Not sure what this is (justs resets transform to identity if identity bit is set?)
	VkSurfaceTransformFlagsKHR preTransform;
	if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else
		preTransform = surfaceCapabilities.currentTransform;

	// Set up the construction struct
	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.pNext = NULL;
	createInfo.surface = m_surface;
	createInfo.minImageCount = minImageCount; // buffering size
	createInfo.imageFormat = m_colorFormat;
	createInfo.imageColorSpace = m_colorSpace;
	createInfo.imageExtent = { swapchainExtent.width, swapchainExtent.height };
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // color buffer
	createInfo.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
	createInfo.imageArrayLayers = 1; // not stereoscopic
	createInfo.queueFamilyIndexCount = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.queueFamilyIndexCount = 0;
	createInfo.pQueueFamilyIndices = NULL;
	createInfo.presentMode = swapchainPresentMode;
	createInfo.oldSwapchain = in_oldSwapChain;
	createInfo.clipped = true; // allow clip pixels
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // opaque, no alpha

	err = fpCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain);
	if (err)
	{
		throw ProgramError(std::string("Error trying to construct swap chain object: ") + vkTools::errorString(err));
	}

	// Destroy old swapchain if we have one
	if (in_oldSwapChain != VK_NULL_HANDLE)
	{
		OutputDebugString("Vulkan: Old swapchain exist, removing old swap chain image views\n");
		for (auto buffer : m_buffers)
		{
			vkDestroyImageView(m_device, buffer.m_imageView, nullptr);
		}
		fpDestroySwapchainKHR(m_device, in_oldSwapChain, nullptr);
	}
}


void VulkanSwapChain::CreateBuffers()
{
	VkResult err;

	uint32_t imageCount;
	err = fpGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, NULL);
	if (err) throw ProgramError(std::string("Error when querying swap chain image count: ") + vkTools::errorString(err));
	if (imageCount < 1) throw ProgramError(std::string("Swap chain image count less than 1: ") + vkTools::errorString(err));

	std::vector<VkImage> bufferImages(imageCount);
	err = fpGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, bufferImages.data());
	if (err) throw ProgramError(std::string("Error when querying swap chain images: ") + vkTools::errorString(err));

	m_buffers.resize(imageCount);
	for (uint32_t i = 0; i < m_buffers.size(); i++)
	{
		VkImageViewCreateInfo colorAttachmentView = {};
		colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorAttachmentView.pNext = NULL;
		colorAttachmentView.format = m_colorFormat;
		colorAttachmentView.components = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A
		};
		colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorAttachmentView.subresourceRange.baseMipLevel = 0;
		colorAttachmentView.subresourceRange.levelCount = 1;
		colorAttachmentView.subresourceRange.baseArrayLayer = 0;
		colorAttachmentView.subresourceRange.layerCount = 1;
		colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorAttachmentView.flags = 0;

		// Assign VkImage object to our buffer struct
		m_buffers[i].m_image = bufferImages[i];

		// Link image to creation struct for the image view
		colorAttachmentView.image = m_buffers[i].m_image;

		err = vkCreateImageView(m_device, &colorAttachmentView, nullptr, &m_buffers[i].m_imageView);
		if (err)
		{
			throw ProgramError(std::string("Error trying to construct an image view(")+std::to_string(i)+std::string(") for the image buffers: ") + vkTools::errorString(err));
		}
	}
}
