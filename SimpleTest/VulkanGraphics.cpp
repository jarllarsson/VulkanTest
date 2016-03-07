#include "VulkanGraphics.h"

#include <string>
#include <array>
#include <vector>
#include "ErrorReporting.h"
#include "vulkantools.h"
#include "vulkandebug.h"

#include "VulkanSwapChain.h"
#include "VulkanCommandBufferFactory.h"


#define ENABLE_VALIDATION false

VulkanGraphics::VulkanGraphics(HWND in_hWnd, HINSTANCE in_hInstance, uint32_t in_width, uint32_t in_height)
	: m_swapChain(nullptr)
	, m_commandBufferFactory(nullptr)
	, m_width(in_width)
	, m_height(in_height)
	, m_graphicsQueueIdx()
{
	Init(in_hWnd, in_hInstance);
}

VulkanGraphics::~VulkanGraphics()
{
	Destroy();
}

// Main initialization of Vulkan stuff
void VulkanGraphics::Init(HWND in_hWnd, HINSTANCE in_hInstance)
{
	VkResult err;

	// Create the Vulkan instance
	err = CreateInstance(&m_vulkanInstance);
 	if (err) throw ProgramError(std::string("Could not create Vulkan instance: ") + vkTools::errorString(err));

	// Create the physical device object
	// Just get the first physical device for now (otherwise, read into a vector instead of a single reference)
	uint32_t gpuCount;
	err = vkEnumeratePhysicalDevices(m_vulkanInstance, &gpuCount, &m_physicalDevice);
	if (err) throw ProgramError(std::string("Could not find GPUs: ") + vkTools::errorString(err));

	// Get memory properties for current physical device
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalDeviceMemProp);

	// Get graphics queue index on the current physical device
	m_graphicsQueueIdx = GetGraphicsQueueInternalIndex();

	// Create the logical device
	err = CreateLogicalDevice(m_graphicsQueueIdx, &m_device);
	if (err) throw ProgramError(std::string("Could not create logical device: ") + vkTools::errorString(err));

	// Get the graphics queue
	vkGetDeviceQueue(m_device, m_graphicsQueueIdx, 0, &m_queue);

	// Get depth format
	if (!GetDepthFormat(&m_depthFormat)) throw ProgramError(std::string("Could set up the depth format."));

	// Create a swap chain representation
	m_swapChain = std::make_shared<VulkanSwapChain>(m_vulkanInstance, m_physicalDevice, m_device,
	                                                &m_width, &m_height,
	                                                in_hInstance, in_hWnd);
	// Init factories
	m_commandBufferFactory = std::make_shared<VulkanCommandBufferFactory>(m_device);


	// TODO: Add Vulkan prepare stuff here:
	// Create command pool
	err = CreateCommandPool(&m_commandPool);
	if (err) throw ProgramError(std::string("Could not create command pool: ") + vkTools::errorString(err));


	// 2. create a setup-command buffer ????? Needed ????
	// 3. m_swapChain->SetImageLayoutsToSetupCommandBuffer(commandBuffer); ????? Needed ????
	// 4. Create command buffers for each frame image buffer in the swap chain, for rendering
	CreateCommandBuffers();
	// 5. setup depth stencil

	// Command buffer for initializing the depth stencil and swap chain 
	// images to the right format on the gpu
	VkCommandBuffer swapchainDepthStencilSetupCommandBuffer = VK_NULL_HANDLE;
	m_commandBufferFactory->ConstructSwapchainDepthStencilInitializationCommandBuffer(
		swapchainDepthStencilSetupCommandBuffer,
		m_swapChain,
		m_depthStencil);

	// 6. setup the render pass
	// 7. create a pipeline cache
	// 8. setup frame buffer
	// 9. flush setup-command buffer

	// Submit the setup command buffer to the queue, and then free it (we only need it once, here)
	SubmitCommandBufferAndAppendWaitToQueue(swapchainDepthStencilSetupCommandBuffer);
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &swapchainDepthStencilSetupCommandBuffer);
	swapchainDepthStencilSetupCommandBuffer = VK_NULL_HANDLE;

	// 10. other command buffers should then be created >here<

	// Then we need to implement these, from the derived class in the example:
	// prepareVertices();
	// prepareUniformBuffers();
	// setupDescriptorSetLayout();
	// preparePipelines();
	// setupDescriptorPool();
	// setupDescriptorSet();
	// buildCommandBuffers();

	// -------------------------------------------------------------------------------------------------
	// About the  "????? Needed ????"-tags:
	// The vulkan examples uses an initial command buffer to do some kind of setting
	// before actual rendering. It relates to changing of image layouts.
	// The setup-commandbuffer is begun created before the ordinary frame commandbuffers
	// it is written to with image layout commands for _swap chain_ and for the _depth stencil_.
	// It's creation is then ended in flush setup-commandbuffer and it is then submitted
	// to the allocated vulkan queue and then a wait for that queue is issued. The setup-commandbuffer
	// is then removed and not used again.

	// It seems like it is used to initialize the image layouts for the depth stencil image to VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	// and the buffer images to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR. Both from an initial state of 
	// VK_IMAGE_LAYOUT_UNDEFINED (default, can be used if we're not in need of initializing the image data on CPU, with an existing image for example).
	// Is this really necessary? Yes it is!
	// We need to use a command buffer to initialize the images to their correct image layout.
	// We'll actually have to do the same with images for textures.
	// -------------------------------------------------------------------------------------------------

	// When all the above is implemented we can create the render method that will be called each frame
}

void VulkanGraphics::Destroy()
{
	m_swapChain.reset();
	vkDestroyInstance(m_vulkanInstance, NULL);
}



void VulkanGraphics::DestroyCommandBuffers()
{
	vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_drawCommandBuffers.size()), m_drawCommandBuffers.data());
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_postPresentCommandBuffer);
}

VkResult VulkanGraphics::CreateInstance(VkInstance* out_instance)
{
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; // Mandatory
	appInfo.pNext = NULL;                               // Mandatory
	appInfo.pApplicationName = "vulkanTestApp";
	appInfo.pEngineName = "vulkanTestApp";
	appInfo.apiVersion = VK_API_VERSION;
	std::vector<const char*> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };
 
#ifdef _WIN32
 	enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
 	enabledExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
 
 	// Set up and create the Vulkan main instance
	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; // Mandatory
	instanceCreateInfo.pNext = NULL;                                   // Mandatory
	instanceCreateInfo.flags = 0;                                      // Mandatory
 	instanceCreateInfo.pApplicationInfo = &appInfo;
	// Next, set up what extensions to enable
 	if (enabledExtensions.size() > 0)
 	{
//  		if (ENABLE_VALIDATION)
//  		{
//  			enabledExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
//  		}
 		instanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
 		instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
 	}

	// Finally, set up what layers to enable
//  	if (ENABLE_VALIDATION)
//  	{
//  		instanceCreateInfo.enabledLayerCount = vkDebug::validationLayerCount;
//  		instanceCreateInfo.ppEnabledLayerNames = vkDebug::validationLayerNames;
//  	}

 	return vkCreateInstance(&instanceCreateInfo, nullptr, out_instance);
}



uint32_t VulkanGraphics::GetGraphicsQueueInternalIndex() const
{
	// Find a valid queue that will support graphics operations
	uint32_t graphicsQueueIdx = 0;
	uint32_t queueCount = 0;
	// Report properties of the queues of the specified physical device
	// Note that we first must retrieve the size of the queue!
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, NULL);
	if (queueCount <= 0)
	{
		throw ProgramError(std::string("Could not find any queues on selected GPU"));
	}

	// When we have the count of the available queues, we can create a vector of that size and
	// call vkGetPhysicalDeviceQueueFamilyProperties again, and fill the vector with the queues' properties.
	std::vector<VkQueueFamilyProperties> queueProps(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, queueProps.data());
	for (uint32_t i = 0; i < queueCount; i++)
	{
		/*
		-- graphics       VK_QUEUE_GRAPHICS_BIT
		-- compute        VK_QUEUE_COMPUTE_BIT
		-- transfer       VK_QUEUE_TRANSFER_BIT
		-- sparse memory  VK_QUEUE_SPARSE_BINDING_BIT
		*/
		graphicsQueueIdx = i;
		if (queueProps[graphicsQueueIdx].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			break;
	}
	if (graphicsQueueIdx >= queueCount)
	{
		throw ProgramError(std::string("None of the queues on the selected GPU are graphics queues"));
	}

	return graphicsQueueIdx;
}

VkResult VulkanGraphics::CreateLogicalDevice(uint32_t in_graphicsQueueIdx, VkDevice* out_device)
{
	// Vulkan device

	// First, set up queue creation info
	std::array<float, 1> queuePriorities = { 0.0f };
	VkDeviceQueueCreateInfo queueCreateInfo = {};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.pNext = NULL;
	queueCreateInfo.queueFamilyIndex = in_graphicsQueueIdx;
	queueCreateInfo.queueCount = 1; // one queue for now
	queueCreateInfo.pQueuePriorities = queuePriorities.data();

	// Set up device
	std::vector<const char*> enabledExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = NULL;
	deviceCreateInfo.pEnabledFeatures = NULL;
	// Set queue(s) to device
	deviceCreateInfo.queueCreateInfoCount = 1; // one queue for now
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;


	if (enabledExtensions.size() > 0)
	{
		deviceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
		deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
	}
// 	if (ENABLE_VALIDATION)
// 	{
// 		deviceCreateInfo.enabledLayerCount = vkDebug::validationLayerCount; // todo : validation layer names
// 		deviceCreateInfo.ppEnabledLayerNames = vkDebug::validationLayerNames;
// 	}

	/*
	In Vulkan you can set several queues into the VkDeviceCreateInfo. With correct queuecount.
	You can control the priority of each queue with an array of normalized floats, where 1 is highest priority.
	*/

	return vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, out_device); // no allocation callbacks for now
}

bool VulkanGraphics::GetDepthFormat(VkFormat* out_format)
{
	// Find supported depth format
	// Prefer 24 bits of depth and 8 bits of stencil
	std::vector<VkFormat> depthFormats = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };
	bool depthFormatFound = false;
	for (auto& format : depthFormats)
	{
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &formatProps);
		// Format must support depth stencil attachment for optimal tiling
		if (formatProps.optimalTilingFeatures && VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			*out_format = format;
			depthFormatFound = true;
			break;
		}
	}

	return depthFormatFound;
}

VkResult VulkanGraphics::CreateCommandPool(VkCommandPool* out_commandPool)
{
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

	// TODO: This index is only tested to VK_QUEUE_GRAPHICS_BIT,
	// but I guess it should also be tested on whether it supports presenting?
	// If so, should be done when surface has been created in the swap chain object,
	// using vkGetPhysicalDeviceSurfaceSupportKHR:
	cmdPoolInfo.queueFamilyIndex = m_graphicsQueueIdx;
	
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	return vkCreateCommandPool(m_device, &cmdPoolInfo, nullptr, out_commandPool);
}

void VulkanGraphics::CreateCommandBuffers()
{
	// Create one command buffer per frame buffer 
	// in the swap chain
	// Command buffers store a reference to the 
	// frame buffer inside their render pass info
	// so for static usage without having to rebuild 
	// them each frame, we use one per frame buffer

	uint32_t count = static_cast<uint32_t>(m_swapChain->GetBuffersCount());

	m_drawCommandBuffers.resize(count);
	VkResult err = m_commandBufferFactory->CreateCommandBuffers(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_drawCommandBuffers);
	if (err) throw ProgramError(std::string("Could not allocate command buffers from pool: ") + vkTools::errorString(err));

	// Create a post-present command buffer, it is used to restore the image layout after presenting
	err = m_commandBufferFactory->CreateCommandBuffer(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_postPresentCommandBuffer);
	if (err) throw ProgramError(std::string("Could not allocate post-present command buffer from pool: ") + vkTools::errorString(err));
}

void VulkanGraphics::SubmitCommandBufferAndAppendWaitToQueue(VkCommandBuffer in_commandBuffer)
{
	// Submit a command buffer to the main queue, and also append a wait
	VkResult err;

	if (in_commandBuffer == VK_NULL_HANDLE) return;

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &in_commandBuffer;

	err = vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
	if (err) throw  ProgramError(std::string("Could not submit command buffer: ") + vkTools::errorString(err));

	err = vkQueueWaitIdle(m_queue);
	if (err) throw  ProgramError(std::string("Could not submit wait to queue: ") + vkTools::errorString(err));
}
