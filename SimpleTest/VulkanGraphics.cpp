#include "VulkanGraphics.h"

#include <string>
#include <array>
#include <vector>
#include "ErrorReporting.h"
#include "vulkantools.h"
#include "vulkandebug.h"


#define ENABLE_VALIDATION false

VulkanGraphics::VulkanGraphics(HWND in_hWnd, HINSTANCE in_hInstance, uint32_t in_width, uint32_t in_height)
	: m_swapChain(nullptr)
	, m_width(in_width)
	, m_height(in_height)
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
	err = CreateInstance();
 	if (err)
 	{
		throw ProgramError(std::string("Could not create Vulkan instance: ") + vkTools::errorString(err));
 	}	// Create the physical device object	// Just get the first physical device for now (otherwise, read into a vector instead of a single reference)
	uint32_t gpuCount;
	err = vkEnumeratePhysicalDevices(m_vulkanInstance, &gpuCount, &m_physicalDevice);
	if (err)
	{
		throw ProgramError(std::string("Could not find GPUs: ") + vkTools::errorString(err));
	}	// Get memory properties for current physical device
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalDeviceMemProp);
	// Get graphics queue index on the current physical device
	const uint32_t graphicsQueueIdx = GetGraphicsQueueInternalIndex(m_physicalDevice);

	// Create the logical device
	err = CreateLogicalDevice(m_physicalDevice, graphicsQueueIdx, &m_device);
	if (err)
	{
		throw ProgramError(std::string("Could not create logical device: ") + vkTools::errorString(err));
	}

	// Get the graphics queue
	vkGetDeviceQueue(m_device, graphicsQueueIdx, 0, &m_queue);

	// Get depth format
	if (!SetDepthFormat(m_physicalDevice, &m_depthFormat))
	{
		throw ProgramError(std::string("Could set up the depth format."));
	}

	// Create a swap chain representation
	m_swapChain = std::make_shared<VulkanSwapChain>(m_vulkanInstance, m_physicalDevice, m_device,
	                                                &m_width, &m_height,
	                                                in_hInstance, in_hWnd);

	// TODO: Add Vulkan prepare stuff here:
	// create command pool
	// create a setup-command buffer ????? Needed ????
	// m_swapChain->SetImageLayoutsToSetupCommandBuffer(commandBuffer); ????? Needed ????
	// Create command buffers for each frame image buffer in the swap chain, for rendering
	// setup depth stencil
	// setup the render pass
	// create a pipeline cache
	// setup frame buffer
	// flush setup-command buffer ????? Needed ????
	// other command buffers should then be created >here<

	// -------------------------------------------------------------------------------------------------
	// About the  "????? Needed ????"-tags:
	// The vulkan examples uses an initial command buffer to do some kind of setting
	// before actual rendering. It relates to changing of image layouts.
	// The setup-commandbuffer is begun created before the ordinary frame commandbuffers
	// it is written to with image layout commands for _swap chain_ and for the _depth stencil_.
	// It's creation is then ended in flush setup-commandbuffer and it is then submitted
	// to the allocated vulkan queue and then a wait for that queue is issued. The setup-commandbuffer
	// is then removed and not used again.
	// -------------------------------------------------------------------------------------------------

	// When all the above is implemented we can create the render method that will be called each frame
}

void VulkanGraphics::Destroy()
{
	m_swapChain.reset();
	vkDestroyInstance(m_vulkanInstance, NULL);
}



VkResult VulkanGraphics::CreateInstance()
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

 	return vkCreateInstance(&instanceCreateInfo, nullptr, &m_vulkanInstance);
}



uint32_t VulkanGraphics::GetGraphicsQueueInternalIndex(const VkPhysicalDevice in_physicalDevice) const
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

VkResult VulkanGraphics::CreateLogicalDevice(VkPhysicalDevice in_physicalDevice, uint32_t in_graphicsQueueIdx, VkDevice* out_device)
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

	return vkCreateDevice(in_physicalDevice, &deviceCreateInfo, nullptr, out_device); // no allocation callbacks for now
}

bool VulkanGraphics::SetDepthFormat(VkPhysicalDevice in_physicalDevice, VkFormat* out_format)
{
	// Find supported depth format
	// Prefer 24 bits of depth and 8 bits of stencil
	std::vector<VkFormat> depthFormats = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };
	bool depthFormatFound = false;
	for (auto& format : depthFormats)
	{
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(in_physicalDevice, format, &formatProps);
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
