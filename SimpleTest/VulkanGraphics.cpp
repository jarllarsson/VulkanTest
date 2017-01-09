#include "VulkanGraphics.h"

// Standard libs and overall helpers
#include <string>
#include <array>
#include <vector>
#include "ErrorReporting.h"
#include "vulkantools.h" // error string help
#include "vulkandebug.h" // debug layer help (not implemented yet)

// General Vulkan setup helpers and factories
#include "VulkanSwapChain.h"
#include "VulkanCommandBufferFactory.h"
#include "VulkanMemoryHelper.h"
#include "VulkanRenderPassFactory.h"
#include "VulkanBufferFactory.h"
#include "VulkanShaderLoader.h"

// Geometry
#include "Vertex.h"
#include "VulkanVertexLayout.h"
#include "VulkanMesh.h"

// Uniform buffers
#include "VulkanUniformBufferPerFrame.h"


#define ENABLE_VALIDATION true // set to true to enable debug layer (requires LunarG SDK)
//#define USE_GLSL

// Binding IDs
#define VERTEX_BUFFER_BIND_ID 0

#ifdef _DEBUG
#define REGISTER_VKOBJ(x, d, func, dbg) x(d, func, std::string(dbg))
#else
#define REGISTER_VKOBJ(x, d, func, dbg) x(d, func)
#endif // _DEBUG




VulkanGraphics::VulkanGraphics(HWND in_hWnd, HINSTANCE in_hInstance, uint32_t in_width, uint32_t in_height)
	//////////////////////////////////////////////////////////////////////////
	// VkObjects needs to be created with pointers to their destruction functions.
	// Most also need a reference to the device wrapper for their destruction.
	// Wrapped data assigned later upon initialization.
	//////////////////////////////////////////////////////////////////////////
	: m_vulkanInstance(vkDestroyInstance)
	, m_device(vkDestroyDevice)
	, REGISTER_VKOBJ(m_commandPool, m_device, vkDestroyCommandPool, "CommandPool")
	, REGISTER_VKOBJ(m_pipelineCache, m_device, vkDestroyPipelineCache, "PipelineCache")
	, REGISTER_VKOBJ(m_renderPass, m_device, vkDestroyRenderPass, "RenderPass")
	, REGISTER_VKOBJ(m_descriptorPool, m_device, vkDestroyDescriptorPool, "DescriptorPool")
	, REGISTER_VKOBJ(m_presentComplete, m_device, vkDestroySemaphore, "PresentCompleteSemaphore")
	, REGISTER_VKOBJ(m_renderComplete, m_device, vkDestroySemaphore, "RenderCompleteSemaphore")
	, REGISTER_VKOBJ(m_descriptorSetLayoutPerFrame_TriangleProgram, m_device, vkDestroyDescriptorSetLayout, "DescriptorSetLayoutPerFrame_TriangleProgram")
	, REGISTER_VKOBJ(m_pipelineLayout_TriangleProgram, m_device, vkDestroyPipelineLayout, "PipelineLayout_TriangleProgram")
	, REGISTER_VKOBJ(m_pipeline_TriangleProgram, m_device, vkDestroyPipeline, "Pipeline_TriangleProgram")
	//////////////////////////////////////////////////////////////////////////
	, m_depthStencil(m_device)
	, m_graphicsQueueIdx()
	, m_postPresentCommandBuffers(VK_NULL_HANDLE)
	, m_currentFrameBufferIdx(0)
	, m_width(in_width)
	, m_height(in_height)
{
#ifdef _DEBUG
	m_vulkanInstance.SetDbgName(std::string("Instance"));
	m_device.SetDbgName(std::string("Device"));
#endif
	Init(in_hWnd, in_hInstance);
}

VulkanGraphics::~VulkanGraphics()
{
	Destroy();
}



void VulkanGraphics::Render()
{
	if (!m_device)
		return;
	vkDeviceWaitIdle(m_device);
	Draw();
	vkDeviceWaitIdle(m_device);
}

// Main initialization of Vulkan stuff
void VulkanGraphics::Init(HWND in_hWnd, HINSTANCE in_hInstance)
{
	// ===================================
	// 1. Set up Vulkan
	// ===================================
	VkResult err = VK_SUCCESS;

	// Create the Vulkan instance
	err = CreateInstance(m_vulkanInstance.Replace());
	if (err) throw ProgramError(std::string("Create Vulkan instance: ") + vkTools::errorString(err));

	// If requested, we enable the default validation layers for debugging
	if (ENABLE_VALIDATION)
	{
		// Report flags for defining what levels to enable for the debug layer
		VkDebugReportFlagsEXT debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		vkDebug::setupDebugging(m_vulkanInstance, debugReportFlags, VK_NULL_HANDLE);
	}
	
	// Create the physical device object
	// Just get the first physical device for now (otherwise, read into a vector instead of a single reference)
	uint32_t gpuCount = 0;
	// Get number of available physical devices
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(m_vulkanInstance, &gpuCount, nullptr));
	assert(gpuCount > 0);
	err = vkEnumeratePhysicalDevices(m_vulkanInstance, &gpuCount, &m_physicalDevice);
	
	if (err != VK_SUCCESS)
	{
		if (err == VK_INCOMPLETE)
		{
			// Incomplete can be success if not 0 gpus found
			if (gpuCount <= 0 || !m_physicalDevice)
			{
				throw ProgramError(std::string("No GPUs found when enumerating GPUs: ") + vkTools::errorString(err));
			}
		}
		else if (err != VK_SUCCESS)
		{
			throw ProgramError(std::string("Enumerating GPUs: ") + vkTools::errorString(err));
		}
	}
	
	// Initialize memory helper class
	m_memory = std::make_shared<VulkanMemoryHelper>(m_physicalDevice);

	// Get graphics queue index on the current physical device
	m_graphicsQueueIdx = GetGraphicsQueueInternalIndex();

	// Create the logical device
	err = CreateLogicalDevice(m_graphicsQueueIdx, m_device.Replace());
	if (err) throw ProgramError(std::string("Create logical device: ") + vkTools::errorString(err));

	// Init factories
	m_commandBufferFactory = std::make_unique<VulkanCommandBufferFactory>(m_device);
	m_renderPassFactory = std::make_unique<VulkanRenderPassFactory>(m_device);
	m_depthStencilFactory = std::make_unique<VulkanDepthStencilFactory>(m_device, m_memory);
	m_bufferFactory = std::make_unique<VulkanBufferFactory>(m_device, m_memory);


	// ================================================
	// 2. Prepare render usage of Vulkan
	// ================================================

	// Get the graphics queue
	vkGetDeviceQueue(m_device, m_graphicsQueueIdx, 0, &m_queue);

	// Set color format
	m_colorformat = VK_FORMAT_B8G8R8A8_UNORM;
	// Get and set depth format
	if (!GetDepthFormat(&m_depthFormat)) throw ProgramError(std::string("Set up the depth format."));

	// Create a swap chain representation
	m_swapChain = std::make_shared<VulkanSwapChain>(m_vulkanInstance, m_physicalDevice, m_device,
		&m_width, &m_height,
		in_hInstance, in_hWnd);

	if (ENABLE_VALIDATION)
	{
		vkDebug::setupDebugging(m_vulkanInstance, VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT, NULL);
	}

	// Create command pool
	err = CreateCommandPool(m_commandPool.Replace());
	if (err) throw ProgramError(std::string("Create command pool: ") + vkTools::errorString(err));

	// Create command buffers for each frame image buffer in the swap chain, for rendering
	AllocateRenderCommandBuffers();

	// Setup depth stencil
	m_depthStencilFactory->CreateDepthStencil(m_depthFormat, m_width, m_height, m_depthStencil);

	// Command buffer for initializing the depth stencil and swap chain 
	// images to the right format on the gpu
	VkCommandBuffer swapchainDepthStencilSetupCommandBuffer = VK_NULL_HANDLE;
	err = m_commandBufferFactory->AllocateCommandBuffer(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, swapchainDepthStencilSetupCommandBuffer);
	if (err) throw ProgramError(std::string("Create a setup command buffer for the swapchain and depth stencil: ") + vkTools::errorString(err));
	m_commandBufferFactory->ConstructSwapchainDepthStencilInitializationCommandBuffer(
		swapchainDepthStencilSetupCommandBuffer,
		m_swapChain,
		m_depthStencil);


	// Create the render pass
	err = m_renderPassFactory->CreateStandardRenderPass(m_colorformat, m_depthFormat, *m_renderPass.Replace());
	if (err) throw ProgramError(std::string("Create render pass: ") + vkTools::errorString(err));

	// Create a pipeline cache
	err = CreatePipelineCache();
	if (err) throw ProgramError(std::string("Create pipeline cache: ") + vkTools::errorString(err));

	// Setup frame buffer
	CreateFrameBuffers();

	// Submit the setup command buffer to the queue, and then free it (we only need it once, here)
	SubmitCommandBufferAndAppendWaitToQueue(swapchainDepthStencilSetupCommandBuffer);
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &swapchainDepthStencilSetupCommandBuffer);
	swapchainDepthStencilSetupCommandBuffer = VK_NULL_HANDLE;

	// Note: Other command buffers if needed should then be allocated >here<

	// ================================================
	// 3. Prepare application specific usage of Vulkan
	// ================================================

	// Create semaphores
	CreateSemaphores();

	// Create triangle mesh
	m_triangleMesh = std::make_shared<VulkanMesh>(m_device);
	m_bufferFactory->CreateTriangle(*m_triangleMesh.get());

	// TODO: The following methods are currently specialized for a triangle example
	// but should probably be more generalized in the future:
	// -------------------------------------
	// Set up a simple vertex layout for our mesh
	CreateTriangleProgramVertexLayouts();

	// Set up the uniform buffers
	CreateTriangleProgramUniformBuffers();

	// Create descriptor set layout and with that then set up a 
	// corresponding pipeline layout and create the pipeline
	// A descriptor set is a collection of our constant buffers/uniforms and samplers (in Vulkan these are known as descriptors).
	// A descriptor set layout specifies what stages the descriptors are visible to.
	// Descriptor sets are useful groups as they can be grouped based on update frequency.
	CreateTriangleProgramDescriptorSetLayout(); // Describes the various bind stages of our descriptors
	// The pipeline then can be seen sorta like a function taking some structs as parameters, where then the parameter types are the descriptor sets layout(s) (1 layout used here atm)
	CreatePipelineLayout(m_descriptorSetLayoutPerFrame_TriangleProgram, *m_pipelineLayout_TriangleProgram.Replace()); // Create a pipeline which can handle the specified descriptor set layout
	CreateTriangleProgramPipelineAndLoadShaders();
	// Set up the descriptor set pool, this from where
	CreateTriangleProgramDescriptorPool();
	// With the pool we can now allocate the descriptors
	CreateTriangleProgramDescriptorSet();
	// -------------------------------------

	// Set up a command buffer for drawing the mesh
	std::vector<VkDescriptorSet> descriptors = { m_descriptorSetPerFrame };
	VulkanCommandBufferFactory::DrawCommandBufferDependencies drawInfo(
		&m_pipelineLayout_TriangleProgram,
		&m_pipeline_TriangleProgram,
		&descriptors,
		VERTEX_BUFFER_BIND_ID,
		m_triangleMesh.get(),
		m_swapChain.get()
		);
	VkClearColorValue clearCol = { { 0.0f, 0.0f, 1.0f, 1.0f } };
	m_commandBufferFactory->ConstructDrawCommandBuffer(m_drawCommandBuffers, m_frameBuffers, 
		drawInfo, m_renderPass, 
		clearCol, m_width, m_height);

	// Also set up commandbuffers for post-present behaviour (resetting image layouts for instance)
	m_commandBufferFactory->ConstructPostPresentCommandBuffer(m_postPresentCommandBuffers, drawInfo);


	// When all the above is implemented we can create the render method that will be called each frame

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

}

VkResult VulkanGraphics::CreateInstance(VkInstance* out_instance)
{
	std::string name = "vulkanTestApp";
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; // Mandatory
	appInfo.pNext = NULL;                               // Mandatory
	appInfo.pApplicationName = name.c_str();
	appInfo.pEngineName = name.c_str();
	appInfo.applicationVersion = 1;
	appInfo.engineVersion = 1;
	appInfo.apiVersion = VK_API_VERSION_1_0;
	std::vector<const char*> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

	// Windows specific
	enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

	// Set up and create the Vulkan main instance
	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; // Mandatory
	instanceCreateInfo.pNext = NULL;                                   // Mandatory
	//instanceCreateInfo.flags = 0;                                      // Mandatory
	instanceCreateInfo.pApplicationInfo = &appInfo;
	// Next, set up what extensions to enable
	if (enabledExtensions.size() > 0)
	{
		if (ENABLE_VALIDATION)
		{
			enabledExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		}
		instanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
		instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
	}

	// Set up what debug layers to enable
	if (ENABLE_VALIDATION)
	{
		instanceCreateInfo.enabledLayerCount = vkDebug::validationLayerCount;
		instanceCreateInfo.ppEnabledLayerNames = vkDebug::validationLayerNames;
	}

	VkResult err = vkCreateInstance(&instanceCreateInfo, nullptr, out_instance);
	if (err)
	{
		return err;
	}

	return err;
}

void VulkanGraphics::Destroy()
{
	// General
	OutputDebugString("Vulkan: Removing swap chain\n");
	m_swapChain.reset();

	OutputDebugString("Vulkan: Removing command buffers\n");
	DestroyCommandBuffers();

	OutputDebugString("Vulkan: Removing frame buffers\n");
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_frameBuffers.size()); i++)
	{
		vkDestroyFramebuffer(m_device, m_frameBuffers[i], nullptr);
	}

	if (ENABLE_VALIDATION)
	{
		vkDebug::freeDebugCallback(m_vulkanInstance);
	}
}

void VulkanGraphics::DestroyCommandBuffers()
{
	OutputDebugString("Vulkan: Removing draw command buffers\n");
	if (m_drawCommandBuffers[0]!=VK_NULL_HANDLE) // TODO, remove this when we have the draw buffer initialization in place
		vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_drawCommandBuffers.size()), m_drawCommandBuffers.data());
	else
		OutputDebugString("Vulkan: Warning, can't remove draw buffer as it has not been created\n");
	OutputDebugString("Vulkan: Removing draw post present command buffers\n");
	vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_postPresentCommandBuffers.size()), m_postPresentCommandBuffers.data());
}


uint32_t VulkanGraphics::GetGraphicsQueueInternalIndex() const
{
	// TODO: Move this to swapchain (need to split up its current constructor)

	// Find a valid queue that will support graphics operations
	uint32_t graphicsQueueIdx = 0;
	uint32_t queueCount = 0;
	// Report properties of the queues of the specified physical device
	// Note that we first must retrieve the size of the queue!
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, NULL);
	if (queueCount <= 0)
	{
		throw ProgramError(std::string("Get queues on selected GPU"));
	}

	// Find a queue that support presenting
	/*d::vector<VkBool32> supportsPresent(queueCount);
	for (uint32_t i = 0; i < queueCount; i++)
	{
		fpGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, surface, &supportsPresent[i]);
	}*/

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
	if (ENABLE_VALIDATION)
	{
		deviceCreateInfo.enabledLayerCount = vkDebug::validationLayerCount; // todo : validation layer names
		deviceCreateInfo.ppEnabledLayerNames = vkDebug::validationLayerNames;
	}

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

void VulkanGraphics::AllocateRenderCommandBuffers()
{
	// Create one command buffer per image buffer 
	// in the swap chain
	// Command buffers store a reference to the 
	// frame buffer inside their render pass info
	// so for static usage without having to rebuild 
	// them each frame, we use one per frame buffer

	uint32_t count = static_cast<uint32_t>(m_swapChain->GetBuffersCount());

	m_drawCommandBuffers.resize(count);
	VkResult err = m_commandBufferFactory->AllocateCommandBuffers(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_drawCommandBuffers);
	if (err) throw ProgramError(std::string("Allocate command buffers from pool: ") + vkTools::errorString(err));

	// Create post-present command buffers, they are used to restore the image layout after presenting
	m_postPresentCommandBuffers.resize(count);
	err = m_commandBufferFactory->AllocateCommandBuffers(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_postPresentCommandBuffers);
	if (err) throw ProgramError(std::string("Allocate post-present command buffer from pool: ") + vkTools::errorString(err));
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
	if (err) throw  ProgramError(std::string("Submit command buffer: ") + vkTools::errorString(err));

	err = vkQueueWaitIdle(m_queue);
	if (err) throw  ProgramError(std::string("Submit wait to queue: ") + vkTools::errorString(err));
}

VkResult VulkanGraphics::CreatePipelineCache()
{
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	return vkCreatePipelineCache(m_device, &pipelineCacheCreateInfo, nullptr, m_pipelineCache.Replace());
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
		if (err) throw ProgramError(std::string("Create frame buffer[") + std::to_string(i) + "] :" + vkTools::errorString(err));
	}
}

void VulkanGraphics::CreateSemaphores()
{
	VkResult err;
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = NULL;

	err = vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, m_presentComplete.Replace());
	if (err) throw ProgramError(std::string("Creating wait semaphore for present-complete: ") + vkTools::errorString(err));
	err = vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, m_renderComplete.Replace());
	if (err) throw ProgramError(std::string("Creating signal semaphore for render-complete: ") + vkTools::errorString(err));
}

VkDescriptorSetLayoutBinding VulkanGraphics::CreateDescriptorSetLayoutBinding(uint32_t in_descriptorBindingId,
	VkDescriptorType in_type,
	VkShaderStageFlags in_shaderStageFlags)
{
	VkDescriptorSetLayoutBinding setLayoutBinding = {};
	setLayoutBinding.descriptorType = in_type;
	setLayoutBinding.stageFlags = in_shaderStageFlags;
	setLayoutBinding.binding = in_descriptorBindingId;
	setLayoutBinding.descriptorCount = 1; // not sure when this is not 1
	return setLayoutBinding;
}

VkDescriptorSetLayoutCreateInfo VulkanGraphics::CreateDescriptorSetLayoutCreateInfo(const std::vector<VkDescriptorSetLayoutBinding>& in_bindings)
{
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
	descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutCreateInfo.pNext = NULL;
	descriptorSetLayoutCreateInfo.pBindings = in_bindings.data();
	descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(in_bindings.size());
	return descriptorSetLayoutCreateInfo;
}

void VulkanGraphics::CreateTriangleProgramVertexLayouts()
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

void VulkanGraphics::CreateTriangleProgramUniformBuffers()
{
	// Per frame buffer
	// --------------------------------------------------------------------------------------------------------

	// Create buffer for projection-, view- and world matrices.
	glm::mat4 projectionMatrix = glm::perspective(deg_to_rad(60.0f), (float)m_width / (float)m_height, 
		0.1f, // near
		1000.0f); // far
	glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, -3.0f)); // camera start location
	m_rotation = glm::vec3();
	glm::mat4 worldMatrix = glm::mat4();
	worldMatrix = glm::rotate(worldMatrix, deg_to_rad(m_rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	worldMatrix = glm::rotate(worldMatrix, deg_to_rad(m_rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	worldMatrix = glm::rotate(worldMatrix, deg_to_rad(m_rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

	m_ubufPerFrame = std::make_shared<VulkanUniformBufferPerFrame>(m_device);
	m_bufferFactory->CreateUniformBufferPerFrame(*m_ubufPerFrame.get(), projectionMatrix, worldMatrix, viewMatrix);
	// --------------------------------------------------------------------------------------------------------

	// TODO: other buffers based on how often they're updated
}

void VulkanGraphics::CreateTriangleProgramDescriptorSetLayout()
{
	// Setup the descriptor's layout (TODO: Create factory for this if we need more?)
	// A description of what shader stages the uniform buffers (and image sampler) are bound to.

	// Every shader binding should map to one descriptor layout

	// The following is set ups a descriptor layout for accessing our per-frame uniform buffer from the vertex shader:
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
	{
		// Binding 0 : binding of a uniform buffer for vertex shader stage access
		CreateDescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
	};
	VkDescriptorSetLayoutCreateInfo descriptorLayout = CreateDescriptorSetLayoutCreateInfo(setLayoutBindings);

	VkResult err = vkCreateDescriptorSetLayout(m_device, &descriptorLayout, NULL, m_descriptorSetLayoutPerFrame_TriangleProgram.Replace());
	if(err) throw ProgramError(std::string("Create descriptor set layout: ") + vkTools::errorString(err));
}

void VulkanGraphics::CreateTriangleProgramDescriptorPool()
{
	// Max requested descriptors per type:
	VkDescriptorPoolSize typeCounts[1];
	// We're currently only using 1 descriptor type (a uniform buffer)
	// We will only request this descriptor once
	typeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	typeCounts[0].descriptorCount = 1;
	// If we add more possible types, we need to increase the size of typeCounts
	// and specify the additional types.
	// E.g. for two combined image samplers :
	// typeCounts[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	// typeCounts[1].descriptorCount = 2;

	// Create global descriptor pool
	// We could have one pool per thread to allow for per thread allocation of descriptors
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
	descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCreateInfo.pNext = NULL;
	descriptorPoolCreateInfo.poolSizeCount = 1;
	descriptorPoolCreateInfo.pPoolSizes = typeCounts;
	descriptorPoolCreateInfo.maxSets = 1; // The max number of descriptor sets that can be created. (Requesting more results in an error)

	VkResult err = vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, m_descriptorPool.Replace());
	if (err) throw ProgramError(std::string("Create descriptor pool: ") + vkTools::errorString(err));
}

void VulkanGraphics::CreateTriangleProgramDescriptorSet()
{
	// Update descriptor sets determining the shader binding points
	// For every binding point used in a shader there needs to be one
	// descriptor set matching that binding point
	VkWriteDescriptorSet writeDescriptorSet = {};

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_descriptorSetLayoutPerFrame_TriangleProgram;

	VkResult err = vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSetPerFrame);
	if (err) throw ProgramError(std::string("Allocate descriptor set: ") + vkTools::errorString(err));

	// Binding 0 : Uniform buffer
	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.dstSet = m_descriptorSetPerFrame; // TODO: Take parameter
	writeDescriptorSet.descriptorCount = 1; // TODO: Get number of descriptors from set (wrap in in struct containing count?)
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // TODO: Possible to retrieve this from layout to avoid having to state this twice? Wrap layout in struct with type info?
	writeDescriptorSet.pBufferInfo = &m_ubufPerFrame->m_allocation.m_descriptorBufferInfo; // TODO: Take buffer object as input param?
	// Binds this uniform buffer to binding point 0
	writeDescriptorSet.dstBinding = 0;

	vkUpdateDescriptorSets(m_device, 1, &writeDescriptorSet, 0, nullptr);
}

void VulkanGraphics::Draw()
{
	VkResult err;

	// Get next swap chain image (backbuffer flip)
	err = m_swapChain->NextImage(m_presentComplete, &m_currentFrameBufferIdx);
	assert(!err);


	// Submit the post-present command buffer
	// Submit to the queue
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_postPresentCommandBuffers[m_currentFrameBufferIdx]; // use post present commandbuffer for current frame buffer

	err = vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
	assert(!err);

	err = vkQueueWaitIdle(m_queue); // wait until done
	assert(!err);

	// Submit info for draw
	// Contains list of command buffers and semaphores
	// TODO: Maybe only need to do this once??
	submitInfo = {};
	VkPipelineStageFlags pipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pWaitDstStageMask = &pipelineStages;
	// The wait semaphore ensures that image has been presented before new commandbuffer is submitted
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_presentComplete;
	submitInfo.commandBufferCount = 1; // only have one commandbuffer atm (send along vector::data, below, if we have more than one commandbuffer)
	submitInfo.pCommandBuffers = &m_drawCommandBuffers[m_currentFrameBufferIdx]; // use draw commandbuffer for current frame buffer
	// The signal semaphore is used during presentation of the queue to make sure that image is rendered after
	// all commandbuffers are submitted
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_renderComplete;

	// Submit to Vulkan graphics queue (executes command buffers)
	err = vkQueueSubmit(m_queue, 1, &submitInfo, 
		VK_NULL_HANDLE); // optional fence that is signaled when all commandbuffers in this submit is done. Not needed here atm
	assert(!err);

	// Present the queue (draws image)
	err = m_swapChain->Present(m_queue, m_currentFrameBufferIdx, m_renderComplete);
	assert(!err);
}

void VulkanGraphics::CreatePipelineLayout(const VkDescriptorSetLayout& in_descriptorSetLayout, VkPipelineLayout& out_pipelineLayout)
{
	// Create a pipeline layout which is to be used to create the pipeline which 
	// uses the given descriptor set layout

	// This method can be used to set up different pipeline layouts for different descriptor sets.

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.pNext = NULL;
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &in_descriptorSetLayout;

	VkResult err = vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &out_pipelineLayout);
	if (err) throw ProgramError(std::string("Create pipeline layout: ") + vkTools::errorString(err));
}

void VulkanGraphics::CreateTriangleProgramPipelineAndLoadShaders()
{
	// Create the pipeline for rendering, we create a pipeline containing all the states
	// that defines it, instead of using a state machine and change during run-time.
	// We can define for example topology type and rasterization- and blend states.
	// So in an application with lots of stuff to render in different ways there will be pipelines for
	// each rendering "mode".

	// TODO: Make a separate pipeline for rendering in "wireframe mode"
	// TODO: Probably want to separate this, create a factory for making it handy to generate several types of pipelines

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
	VkResult err;

	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCreateInfo.layout = m_pipelineLayout_TriangleProgram; // layout used for pipeline
	pipelineCreateInfo.renderPass = m_renderPass; // renderpass we created, attach to this pipeline

	// Vertex topology setting (triangle lists)
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = {};
	inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Rasterization state setting (filled, no culling)
	VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = {};
	rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_NONE;
	rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
	rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;

	// Blend state setting (no blending)
	VkPipelineColorBlendStateCreateInfo blendStateCreateInfo = {};
	blendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
	blendAttachmentState[0].colorWriteMask = 0xf;
	blendAttachmentState[0].blendEnable = VK_FALSE;
	blendStateCreateInfo.attachmentCount = 1;
	blendStateCreateInfo.pAttachments = blendAttachmentState;

	// Viewport state
	VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
	viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCreateInfo.viewportCount = 1;
	viewportStateCreateInfo.scissorCount = 1;

	// Dynamic states
	// These allow us to control, for instance, the viewport size.
	// As it would be crazy to create new pipelines each time we did stuff like that.
	VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
	// The dynamic state properties themselves are stored in the command buffer
	std::vector<VkDynamicState> dynamicStates;
	dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
	dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
	dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();
	dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());

	// Depth and stencil states (depth write, depth test, depth compare <=, no stencil)
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = {};
	depthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCreateInfo.depthTestEnable = VK_TRUE;
	depthStencilStateCreateInfo.depthWriteEnable = VK_TRUE;
	depthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateCreateInfo.depthBoundsTestEnable = VK_FALSE;
	depthStencilStateCreateInfo.back.failOp = VK_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.back.passOp = VK_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;
	depthStencilStateCreateInfo.stencilTestEnable = VK_FALSE;
	depthStencilStateCreateInfo.front = depthStencilStateCreateInfo.back;

	// Multi sampling state (disabled)
	VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = {};
	multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateCreateInfo.pSampleMask = NULL;
	multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // disabled

	// Load shaders
	VkPipelineShaderStageCreateInfo shaderStagesCreateInfo[2] = { {},{} };

#ifdef USE_GLSL
	shaderStagesCreateInfo[0] = VulkanShaderLoader::LoadShaderGLSL("./../shaders/triangle.vert", "main", m_device, VK_SHADER_STAGE_VERTEX_BIT);
	shaderStagesCreateInfo[1] = VulkanShaderLoader::LoadShaderGLSL("./../shaders/triangle.frag", "main", m_device, VK_SHADER_STAGE_FRAGMENT_BIT);
#else
	shaderStagesCreateInfo[0] = VulkanShaderLoader::LoadShaderSPIRV("./../shaders/triangle.vert.spv", "main", m_device, VK_SHADER_STAGE_VERTEX_BIT);
	shaderStagesCreateInfo[1] = VulkanShaderLoader::LoadShaderSPIRV("./../shaders/triangle.frag.spv", "main", m_device, VK_SHADER_STAGE_FRAGMENT_BIT);
#endif
	// Store shader modules
	for (auto& shader : shaderStagesCreateInfo)
	{

		m_shaderModules.push_back(std::make_unique<ShaderModuleType>(m_device, vkDestroyShaderModule, 
#ifdef _DEBUG
			std::string("ShaderModule"), 
#endif
			shader.module));
	}

	// Vertex input state (use our simple vertex layout with position and color for this pipeline)
	VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = {};
	vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateCreateInfo.pNext = NULL;
	vertexInputStateCreateInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(m_simpleVertexLayout->m_bindingDescriptions.size());
	vertexInputStateCreateInfo.pVertexBindingDescriptions = m_simpleVertexLayout->m_bindingDescriptions.data();
	vertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_simpleVertexLayout->m_attributeDescriptions.size());
	vertexInputStateCreateInfo.pVertexAttributeDescriptions = m_simpleVertexLayout->m_attributeDescriptions.data();

	// Assign all the states create infos to the main pipeline create info
	pipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
	pipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
	pipelineCreateInfo.pColorBlendState = &blendStateCreateInfo;
	pipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
	pipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
	pipelineCreateInfo.pDepthStencilState = &depthStencilStateCreateInfo;
	pipelineCreateInfo.pStages = shaderStagesCreateInfo;
	pipelineCreateInfo.stageCount = 2; // vertex and fragment shader stages
	pipelineCreateInfo.renderPass = m_renderPass;
	pipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;

	// Create the pipeline
	err = vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &pipelineCreateInfo, nullptr, m_pipeline_TriangleProgram.Replace());
	if (err)
	{
		throw ProgramError(std::string("Create graphics pipeline: ") + vkTools::errorString(err));
	}
}
