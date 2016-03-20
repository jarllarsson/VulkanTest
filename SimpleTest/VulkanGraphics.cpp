#include "VulkanGraphics.h"

#include <string>
#include <array>
#include <vector>
#include "ErrorReporting.h"
#include "vulkantools.h"
#include "vulkandebug.h"

#include "VulkanSwapChain.h"
#include "VulkanCommandBufferFactory.h"
#include "VulkanMemoryHelper.h"
#include "VulkanRenderPassFactory.h"
#include "VulkanBufferFactory.h"

#include "Vertex.h"
#include "VulkanVertexLayout.h"
#include "VulkanMesh.h"


#define ENABLE_VALIDATION false

// Binding IDs
#define VERTEX_BUFFER_BIND_ID 0

VulkanGraphics::VulkanGraphics(HWND in_hWnd, HINSTANCE in_hInstance, uint32_t in_width, uint32_t in_height)
	: m_graphicsQueueIdx()
	, m_postPresentCommandBuffer(VK_NULL_HANDLE)
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
	VkResult err = VK_SUCCESS;

	// Create the Vulkan instance
	err = CreateInstance(&m_vulkanInstance);
	if (err) throw ProgramError(std::string("Could not create Vulkan instance: ") + vkTools::errorString(err));
	
	// Create the physical device object
	// Just get the first physical device for now (otherwise, read into a vector instead of a single reference)
	uint32_t gpuCount = 0;
	err = vkEnumeratePhysicalDevices(m_vulkanInstance, &gpuCount, &m_physicalDevice);
	if (err) throw ProgramError(std::string("Could not find GPUs: ") + vkTools::errorString(err));
	
	// Initialize memory helper class
	m_memory = std::make_shared<VulkanMemoryHelper>(m_physicalDevice);

	// Get graphics queue index on the current physical device
	m_graphicsQueueIdx = GetGraphicsQueueInternalIndex();

	// Create the logical device
	err = CreateLogicalDevice(m_graphicsQueueIdx, &m_device);
	if (err) throw ProgramError(std::string("Could not create logical device: ") + vkTools::errorString(err));

	// Get the graphics queue
	vkGetDeviceQueue(m_device, m_graphicsQueueIdx, 0, &m_queue);

	// Set color format
	m_colorformat = VK_FORMAT_B8G8R8A8_UNORM;
	// Get and set depth format
	if (!GetDepthFormat(&m_depthFormat)) throw ProgramError(std::string("Could set up the depth format."));

	// Create a swap chain representation
	m_swapChain = std::make_shared<VulkanSwapChain>(m_vulkanInstance, m_physicalDevice, m_device,
		&m_width, &m_height,
		in_hInstance, in_hWnd);

	// Init factories
	m_commandBufferFactory = std::make_unique<VulkanCommandBufferFactory>(m_device);
	m_renderPassFactory = std::make_unique<VulkanRenderPassFactory>(m_device);
	m_depthStencilFactory = std::make_unique<VulkanDepthStencilFactory>(m_device, m_memory);
	m_bufferFactory = std::make_unique<VulkanBufferFactory>(m_device, m_memory);

	// TODO: Add Vulkan prepare stuff here:
	// Create command pool
	err = CreateCommandPool(&m_commandPool);
	if (err) throw ProgramError(std::string("Could not create command pool: ") + vkTools::errorString(err));

	// Create command buffers for each frame image buffer in the swap chain, for rendering __DONE__
	CreateCommandBuffers();

	// Setup depth stencil
	m_depthStencilFactory->CreateDepthStencil(m_depthFormat, m_width, m_height, m_depthStencil);

	// Command buffer for initializing the depth stencil and swap chain 
	// images to the right format on the gpu
	VkCommandBuffer swapchainDepthStencilSetupCommandBuffer = VK_NULL_HANDLE;
	err = m_commandBufferFactory->CreateCommandBuffer(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, swapchainDepthStencilSetupCommandBuffer);
	if (err) throw ProgramError(std::string("Could not create a setup command buffer for the swapchain and depth stencil: ") + vkTools::errorString(err));
	m_commandBufferFactory->ConstructSwapchainDepthStencilInitializationCommandBuffer(
		swapchainDepthStencilSetupCommandBuffer,
		m_swapChain,
		m_depthStencil);

	// Create the render pass
	err = m_renderPassFactory->CreateStandardRenderPass(m_colorformat, m_depthFormat, m_renderPass);
	if (err) throw ProgramError(std::string("Could not create render pass: ") + vkTools::errorString(err));

	// Create a pipeline cache
	err = CreatePipelineCache();
	if (err) throw ProgramError(std::string("Could not create pipeline cache: ") + vkTools::errorString(err));

	// Setup frame buffer
	CreateFrameBuffers();

	// Submit the setup command buffer to the queue, and then free it (we only need it once, here)
	SubmitCommandBufferAndAppendWaitToQueue(swapchainDepthStencilSetupCommandBuffer);
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &swapchainDepthStencilSetupCommandBuffer);
	swapchainDepthStencilSetupCommandBuffer = VK_NULL_HANDLE;



	// 10. other command buffers should then be created >here<

	// Then we need to implement these, from the derived class in the example:
	// prepareVertices(); DONE

	// Create triangle mesh
	VulkanMesh triangle;
	m_bufferFactory->CreateTriangle(triangle);
	m_triangleMesh = std::make_shared<VulkanMesh>(triangle);

	// Set up a simple vertex layout for our mesh
	CreateVertexLayouts();

	// prepareUniformBuffers();
	// setupDescriptorSetLayout();
	// preparePipelines(); // here we will need to create a temporary VkPipelineVertexInputStateCreateInfo for pipelineCreateInfo.pVertexInputState from our vertexlayout, as we're not storing the creation struct unlike the examples
	// setupDescriptorPool();
	// setupDescriptorSet();
	// buildCommandBuffers();

	// -------------------------------------------------------------------------------------------------
	// About ConstructSwapchainDepthStencilInitializationCommandBuffer:
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

VkResult VulkanGraphics::CreateInstance(VkInstance* out_instance)
{
	std::string name = "vulkanTestApp";
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; // Mandatory
	appInfo.pNext = NULL;                               // Mandatory
	appInfo.pApplicationName = name.c_str();
	appInfo.pEngineName = name.c_str();
	appInfo.apiVersion = VK_API_VERSION;
	std::vector<const char*> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

	// Windows specific
	enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

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

void VulkanGraphics::Destroy()
{
	OutputDebugString("Vulkan: Removing swap chain\n");
	m_swapChain.reset();

	//TODO vkDestroyDescriptorPool(m_device, descriptorPool, nullptr);
	OutputDebugString("Vulkan: Removing command buffers\n");
	DestroyCommandBuffers();
	OutputDebugString("Vulkan: Removing render pass\n");
	vkDestroyRenderPass(m_device, m_renderPass, nullptr);

	OutputDebugString("Vulkan: Removing frame buffers\n");
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_frameBuffers.size()); i++)
	{
		vkDestroyFramebuffer(m_device, m_frameBuffers[i], nullptr);
	}

	// Todo
	//for (auto& shaderModule : shaderModules)
	//{
	//	vkDestroyShaderModule(device, shaderModule, nullptr);
	//}
	OutputDebugString("Vulkan: Removing depth stencil view\n");
	vkDestroyImageView(m_device, m_depthStencil.m_imageView, nullptr);
	OutputDebugString("Vulkan: Removing depth stencil image\n");
	vkDestroyImage(m_device, m_depthStencil.m_image, nullptr);
	OutputDebugString("Vulkan: Freeing memory of depth stencil on the GPU\n");
	vkFreeMemory(m_device, m_depthStencil.m_gpuMem, nullptr);

	OutputDebugString("Vulkan: Removing pipeline cache\n");
	vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);

	OutputDebugString("Vulkan: Removing command pool\n");
	vkDestroyCommandPool(m_device, m_commandPool, nullptr);
	OutputDebugString("Vulkan: Removing device object\n");
	vkDestroyDevice(m_device, nullptr);
	OutputDebugString("Vulkan: Removing instance object\n");
	vkDestroyInstance(m_vulkanInstance, nullptr);
}

void VulkanGraphics::DestroyCommandBuffers()
{
	OutputDebugString("Vulkan: Removing draw command buffers\n");
	if (m_drawCommandBuffers[0]!=VK_NULL_HANDLE) // TODO, remove this when we have the draw buffer initialization in place
		vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_drawCommandBuffers.size()), m_drawCommandBuffers.data());
	else
		OutputDebugString("Vulkan: Warning, can't remove draw buffer as it has not been created\n");
	OutputDebugString("Vulkan: Removing draw post present command buffers\n");
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_postPresentCommandBuffer);
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

bool VulkanGraphics::GetDepthFormat(VkFormat* out_format) const
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
	// Create one command buffer per image buffer 
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

VkResult VulkanGraphics::CreatePipelineCache()
{
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	return vkCreatePipelineCache(m_device, &pipelineCacheCreateInfo, nullptr, &m_pipelineCache);
}

void VulkanGraphics::CreateFrameBuffers()
{
	// Create frame buffers which use the buffers in the swap chain to
	// render to and the render pass to be compatible with.
	VkImageView attachments[2];

	// Depthstencil attachment is the same for all frame buffers
	attachments[1] = m_depthStencil.m_imageView;


	// Create frame buffers for every swap chain image
	const uint32_t sz = m_swapChain->GetBuffersCount();
	m_frameBuffers.resize(sz);
	const std::vector<VulkanSwapChain::SwapChainBuffer>& swapchainBuffers = m_swapChain->GetBuffers();
	for (uint32_t i = 0; i < sz; i++)
	{
		// Update first creation attachment struct with the associated image view
		// The second struct in the attachment array remains the depthstencil view
		attachments[0] = swapchainBuffers[i].m_imageView;

		VkFramebufferCreateInfo frameBufferCreateInfo = {};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.pNext = NULL;
		frameBufferCreateInfo.renderPass = m_renderPass;
		frameBufferCreateInfo.attachmentCount = 2;
		frameBufferCreateInfo.pAttachments = attachments; // attach the image view and the depthstencil view
		frameBufferCreateInfo.width = m_width;
		frameBufferCreateInfo.height = m_height;
		frameBufferCreateInfo.layers = 1;

		VkResult err = vkCreateFramebuffer(m_device, &frameBufferCreateInfo, nullptr, &m_frameBuffers[i]);
		if (err) throw ProgramError(std::string("Could not create frame buffer[") + std::to_string(i) + "] :" + vkTools::errorString(err));
	}
}

void VulkanGraphics::CreateVertexLayouts()
{
	m_simpleVertexLayout = std::make_shared<VulkanVertexLayout>();
	VulkanVertexLayout& vertices = *m_simpleVertexLayout.get();
	// Binding description
	vertices.m_bindingDescriptions.resize(1);
	vertices.m_bindingDescriptions[0].binding = VERTEX_BUFFER_BIND_ID;
	vertices.m_bindingDescriptions[0].stride = sizeof(Vertex);
	vertices.m_bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	// An entry for each data type in the vertex, for example: [0]:pos, [1]:col
	vertices.m_attributeDescriptions.resize(2);
	// Position
	vertices.m_attributeDescriptions[0].binding = VERTEX_BUFFER_BIND_ID;
	vertices.m_attributeDescriptions[0].location = 0;
	vertices.m_attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertices.m_attributeDescriptions[0].offset = 0;
	// Color
	vertices.m_attributeDescriptions[1].binding = VERTEX_BUFFER_BIND_ID;
	vertices.m_attributeDescriptions[1].location = 1;
	vertices.m_attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertices.m_attributeDescriptions[1].offset = sizeof(float) * 3; // size of position is offset

}

void VulkanGraphics::SetupVertices()
{

}
