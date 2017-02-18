#include "VulkanGraphics.h"

// Standard libs and overall helpers
#include <string>
#include <array>
#include <vector>
#include "ErrorReporting.h"
#include "vulkantools.h" // error string help
#include "vulkandebug.h" // debug layer help (not implemented yet)

// General Vulkan setup helpers and factories
#include "VulkanHelper.h"
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

namespace
{
	// types
	typedef VkObj<VkShaderModule>             ShaderModuleType;
	typedef std::unique_ptr<ShaderModuleType> ShaderModulePtr;
}



VulkanGraphics::VulkanGraphics(HWND in_hWnd, HINSTANCE in_hInstance, uint32_t in_width, uint32_t in_height)
	//////////////////////////////////////////////////////////////////////////
	// VkObjects needs to be created with pointers to their destruction functions.
	// Most also need a reference to the device wrapper for their destruction.
	// Wrapped data assigned later upon initialization.
	//////////////////////////////////////////////////////////////////////////
	: m_vulkanInstance(vkDestroyInstance)
	, m_device(vkDestroyDevice)
	, REGISTER_VKOBJ(m_surface, m_vulkanInstance, vkDestroySurfaceKHR, "Present Surface")
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
	//, m_postPresentCommandBuffers(VK_NULL_HANDLE)
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
	Draw();
}

// Main initialization of Vulkan stuff
void VulkanGraphics::Init(HWND in_hWnd, HINSTANCE in_hInstance)
{
	LOG("Starting vulkan");
	// ===================================
	// 1. Set up Vulkan
	// ===================================
	VkResult err = VK_SUCCESS;

	// INSTANCE : Create the Vulkan instance
	// ---------------------------------------------------------------------------
	CreateInstance();
	// ---------------------------------------------------------------------------

	// DEBUG LAYER : Setup debug layer
	// ---------------------------------------------------------------------------
	// If requested, we enable the default validation layers for debugging
	SetupDebugLayer();
	// ---------------------------------------------------------------------------
	
	// PHYSICAL DEVICE : Create the physical device object
	// ---------------------------------------------------------------------------
	// Just get the first physical device for now (otherwise, read into a vector instead of a single reference)
	FindPhysicalDevice();
	// ---------------------------------------------------------------------------

	// SURFACE : Create presentation surface
	// ---------------------------------------------------------------------------
	CreatePresentSurface(reinterpret_cast<void*>(in_hInstance), reinterpret_cast<void*>(in_hWnd));
	// ---------------------------------------------------------------------------

	// LOGICAL DEVICE : Create the logical device and get the device queue for graphics
	// ---------------------------------------------------------------------------
	CreateLogicalDevice();
	// ---------------------------------------------------------------------------


	// FACTORIES : Init factories
	// ---------------------------------------------------------------------------
	m_memoryHelper = std::make_shared<VulkanMemoryHelper>(m_physicalDevice);
	m_commandBufferFactory = std::make_unique<VulkanCommandBufferFactory>(m_device);
	m_renderPassFactory = std::make_unique<VulkanRenderPassFactory>(m_device);
	m_depthStencilFactory = std::make_unique<VulkanDepthStencilFactory>(m_device, m_memoryHelper);
	m_bufferFactory = std::make_unique<VulkanBufferFactory>(m_device, m_memoryHelper);
	// ---------------------------------------------------------------------------


	// ================================================
	// 2. Prepare render usage of Vulkan
	// ================================================

	// TODO: Move GetDepthFormat into swapchain creation
	// Generalize creation of image and imageviews, create an "ImageViewFactory"
	// Make depth stencil creation use image+imageview creation helper + its own memory
	// create ImageViewFactory(device) it then has creation for image and imageviews and also a special depth stencil creation


	// DEPTH FORMAT : Get and set depth format
	// ---------------------------------------------------------------------------
	if (!GetDepthFormat(&m_depthFormat)) ERROR_ALWAYS("Set up the depth format.");
	// ---------------------------------------------------------------------------

	// SWAP CHAIN : Create a swap chain representation
	// ---------------------------------------------------------------------------
	m_swapChain = std::make_shared<VulkanSwapChain>(m_vulkanInstance, m_physicalDevice, m_device,
		m_surface, &m_width, &m_height);
	// ---------------------------------------------------------------------------


	// COMMAND POOL : Create command pool
	// ---------------------------------------------------------------------------
	err = CreateCommandPool(m_commandPool.Replace());
	ERROR_IF(err, "Create command pool: " << vkTools::errorString(err));
	// ---------------------------------------------------------------------------

	// COMMAND BUFFERS : Create command buffers for each frame image buffer in the swap chain, for rendering
	// ---------------------------------------------------------------------------
	AllocateRenderCommandBuffers();
	// ---------------------------------------------------------------------------

	// DEPTH STENCIL IMAGE VIEWS : Setup depth stencil
	// ---------------------------------------------------------------------------
	m_depthStencilFactory->CreateDepthStencil(m_depthFormat, m_width, m_height, m_depthStencil);
	// ---------------------------------------------------------------------------

	// RENDERPARSS : Create the render pass
	// ---------------------------------------------------------------------------
	err = m_renderPassFactory->CreateStandardRenderPass(m_swapChain->GetColorFormat(), m_depthFormat, *m_renderPass.Replace());
	ERROR_IF(err, "Create render pass: " << vkTools::errorString(err));
	// ---------------------------------------------------------------------------

	// PIPELINE : Create a pipeline cache
	// ---------------------------------------------------------------------------
	err = CreatePipelineCache();
	ERROR_IF(err, "Create pipeline cache: " << vkTools::errorString(err));
	// ---------------------------------------------------------------------------

	// FRAME BUFFER : Setup frame buffer
	// ---------------------------------------------------------------------------
	CreateFrameBuffers();
	// ---------------------------------------------------------------------------


	// SYNCHRONIZATION PRIMITIVES : Create semaphores and fences
	// ---------------------------------------------------------------------------
	CreateSemaphoresAndFences();
	// ---------------------------------------------------------------------------


	// ================================================
	// 3. Prepare application specific usage of Vulkan
	// ================================================

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



	// When all the above is implemented we can create the render method that will be called each frame
}

void VulkanGraphics::CreateInstance()
{
	std::string name = "vulkanTestApp";
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; // Mandatory
	appInfo.pNext = nullptr;                               // Mandatory
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
	instanceCreateInfo.pNext = nullptr;                                   // Mandatory
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

	VkResult err = vkCreateInstance(&instanceCreateInfo, nullptr, m_vulkanInstance.Replace());
	ERROR_IF(err, "Create Vulkan instance: " << vkTools::errorString(err));
	// Set up function pointers that requires Vulkan instance
	GET_INSTANCE_PROC_ADDR(m_vulkanInstance, GetPhysicalDeviceSurfaceSupportKHR);
}

void VulkanGraphics::SetupDebugLayer()
{
	if (ENABLE_VALIDATION)
	{
		// Report flags for defining what levels to enable for the debug layer
		VkDebugReportFlagsEXT debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		vkDebug::setupDebugging(m_vulkanInstance, debugReportFlags, VK_NULL_HANDLE);
	}
}

void VulkanGraphics::FindPhysicalDevice()
{
	VkResult err = VK_SUCCESS;
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
			ERROR_IF(gpuCount <= 0 || !m_physicalDevice, "No GPUs found when enumerating GPUs: " << vkTools::errorString(err));
		}
		else if (err != VK_SUCCESS)
		{
			ERROR_ALWAYS("Enumerating GPUs: " << vkTools::errorString(err));
		}
	}
}

void VulkanGraphics::CreatePresentSurface(void* in_platformHandle, void* in_platformWindow)
{
	assert(in_platformHandle);
	assert(in_platformWindow);
	VkResult err;
	// Create surface
#ifdef _WIN32
	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.hinstance = reinterpret_cast<HINSTANCE>(in_platformHandle);
	surfaceCreateInfo.hwnd = reinterpret_cast<HWND>(in_platformWindow);
	err = vkCreateWin32SurfaceKHR(m_vulkanInstance, &surfaceCreateInfo, nullptr, m_surface.Replace());
#else
#ifdef __ANDROID__
	VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.window = window;
	err = vkCreateAndroidSurfaceKHR(m_vulkanInstance, &surfaceCreateInfo, nullptr, out_surface);
#else
	VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.connection = connection;
	surfaceCreateInfo.window = window;
	err = vkCreateXcbSurfaceKHR(m_vulkanInstance, &surfaceCreateInfo, nullptr, out_surface);
#endif
#endif
	if (err)
	{
		LOG(vkTools::errorString(err));
	}
}

void VulkanGraphics::Destroy()
{
	// General

	// TODO: Replace remaining with VkObjs

	// Flush device to make sure all resources can be freed 
	vkDeviceWaitIdle(m_device);

	OutputDebugString("Vulkan: Removing swap chain\n");
	m_swapChain.reset();

	OutputDebugString("Vulkan: Removing command buffers\n");
	DestroyCommandBuffers();

	OutputDebugString("Vulkan: Removing renderpass\n");
	m_renderPass.Reset(nullptr);

	OutputDebugString("Vulkan: Removing frame buffers\n");
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_frameBuffers.size()); i++)
	{
		vkDestroyFramebuffer(m_device, m_frameBuffers[i], nullptr);
	}

	// Probably needs to be vkobj as well:
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
}


uint32_t VulkanGraphics::GetGraphicsQueueInternalIndex() const
{
	// Find a valid queue that will support graphics operations
	uint32_t graphicsQueueIdx = 0;
	uint32_t queueCount = 0;
	// Report properties of the queues of the specified physical device
	// Note that we first must retrieve the size of the queue!
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, nullptr);
	ERROR_IF(queueCount <= 0, "Get queues on selected GPU");

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
		VkBool32 supportsPresent;
		fpGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &supportsPresent);
		if (supportsPresent && queueProps[graphicsQueueIdx].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			break;
	}


	ERROR_IF(graphicsQueueIdx >= queueCount, "None of the queues on the selected GPU are graphics queues");


	return graphicsQueueIdx;
}


void VulkanGraphics::CreateLogicalDevice()
{
	// Vulkan device

	// First, set up queue creation info
	m_graphicsQueueIdx = GetGraphicsQueueInternalIndex();
	std::array<float, 1> queuePriorities = { 0.0f };
	VkDeviceQueueCreateInfo queueCreateInfo = {};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.pNext = nullptr;
	queueCreateInfo.queueFamilyIndex = m_graphicsQueueIdx;
	queueCreateInfo.queueCount = 1; // one queue for now
	queueCreateInfo.pQueuePriorities = queuePriorities.data();

	// Set up device
	std::vector<const char*> enabledExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = nullptr;
	deviceCreateInfo.pEnabledFeatures = nullptr;
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
	VkResult err = VK_SUCCESS;
	err = vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, m_device.Replace()); // no allocation callbacks for now
	ERROR_IF(err, "Create logical device: " << vkTools::errorString(err));

	// Get the graphics queue for the device
	vkGetDeviceQueue(m_device, m_graphicsQueueIdx, 0, &m_queue);
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

	// This index is tested to VK_QUEUE_GRAPHICS_BIT and whether it supports present (see GetGraphicsQueueInternalIndex):
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
	ERROR_IF(err, "Allocate command buffers from pool: " << vkTools::errorString(err));
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
		frameBufferCreateInfo.pNext = nullptr;
		frameBufferCreateInfo.renderPass = m_renderPass;
		frameBufferCreateInfo.attachmentCount = 2;
		frameBufferCreateInfo.pAttachments = attachments; // attach the image view and the depthstencil view
		frameBufferCreateInfo.width = m_width;
		frameBufferCreateInfo.height = m_height;
		frameBufferCreateInfo.layers = 1;

		VkResult err = vkCreateFramebuffer(m_device, &frameBufferCreateInfo, nullptr, &m_frameBuffers[i]);
		ERROR_IF(err, "Create frame buffer[" << std::to_string(i) << "] :" << vkTools::errorString(err));
	}
}

void VulkanGraphics::CreateSemaphoresAndFences()
{
	// Semaphores are GPU-GPU syncs and are used to order queue submits. They are reset automatically after a completed wait.
	// Fences are GCPU-CPU syncs and can only be waited on and reset on the CPU

	VkResult err;
	// Semaphores (Used for correct command ordering)
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;

	// Semaphore used to ensures that image presentation is complete before starting to submit again
	err = vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, m_presentComplete.Replace());
	ERROR_IF(err, "Creating wait semaphore for present-complete: " << vkTools::errorString(err));
	// Semaphore used to ensures that all commands submitted have been finished before submitting the image to the queue
	err = vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, m_renderComplete.Replace());
	ERROR_IF(err, "Creating signal semaphore for render-complete: " << vkTools::errorString(err));

	// Fences (Used to check draw command buffer completion)
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	// Create in signaled state so we don't wait on first render of each command buffer
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	m_waitFences.resize(m_drawCommandBuffers.size());
	for (auto& pFence : m_waitFences)
	{
		pFence = std::make_unique<FenceType>(m_device, vkDestroyFence
#ifdef _DEBUG
			, std::string("Fence")
#endif
			);
		err = vkCreateFence(m_device, &fenceCreateInfo, nullptr, pFence->Replace());
		ERROR_IF(err, "Creating wait fence for waiting for draw buffer completion: " << vkTools::errorString(err));
	}
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
	descriptorSetLayoutCreateInfo.pNext = nullptr;
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

	VkResult err = vkCreateDescriptorSetLayout(m_device, &descriptorLayout, nullptr, m_descriptorSetLayoutPerFrame_TriangleProgram.Replace());
	ERROR_IF(err, "Create descriptor set layout: " << vkTools::errorString(err));
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
	descriptorPoolCreateInfo.pNext = nullptr;
	descriptorPoolCreateInfo.poolSizeCount = 1;
	descriptorPoolCreateInfo.pPoolSizes = typeCounts;
	descriptorPoolCreateInfo.maxSets = 1; // The max number of descriptor sets that can be created. (Requesting more results in an error)

	VkResult err = vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, m_descriptorPool.Replace());
	ERROR_IF(err, "Create descriptor pool: " << vkTools::errorString(err));
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
	ERROR_IF(err, "Allocate descriptor set: " << vkTools::errorString(err));

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
	ERROR_IF(err, "Swap chain get next image");

	// Use a fence to wait until the command buffer has finished execution before using it again
	err = vkWaitForFences(m_device, 1, &(*m_waitFences[m_currentFrameBufferIdx].get()), VK_TRUE, UINT64_MAX);
	ERROR_IF(err, "Fence wait");
	err = vkResetFences(m_device, 1, &(*m_waitFences[m_currentFrameBufferIdx].get()));
	ERROR_IF(err, "Reset fence");

	// Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
	VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	// The submit info structure specifies a command buffer queue submission batch
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pWaitDstStageMask = &waitStageMask;									// Pointer to the list of pipeline stages that the semaphore waits will occur at
	submitInfo.pWaitSemaphores = &m_presentComplete;							    // Semaphore(s) to wait upon before the submitted command buffer starts executing
	submitInfo.waitSemaphoreCount = 1;												// One wait semaphore																				
	submitInfo.pSignalSemaphores = &m_renderComplete;						        // Semaphore(s) to be signaled when command buffers have completed
	submitInfo.signalSemaphoreCount = 1;											// One signal semaphore
	submitInfo.pCommandBuffers = &m_drawCommandBuffers[m_currentFrameBufferIdx];	// Command buffers(s) to execute in this batch (submission)
	submitInfo.commandBufferCount = 1;												// One command buffer

	// Submit to the graphics queue passing a wait fence
	err = vkQueueSubmit(m_queue, 1, &submitInfo, *m_waitFences[m_currentFrameBufferIdx]);
	ERROR_IF(err, "Draw queue submit");

	// Present the current buffer to the swap chain
	// Pass the semaphore signaled by the command buffer submission from the submit info as the wait semaphore for swap chain presentation
	// This ensures that the image is not presented to the windowing system until all commands have been submitted
	// Present the queue (draws image)
	err = m_swapChain->Present(m_queue, m_currentFrameBufferIdx, m_renderComplete);
	ERROR_IF(err, "Swapchain present");
}

void VulkanGraphics::CreatePipelineLayout(const VkDescriptorSetLayout& in_descriptorSetLayout, VkPipelineLayout& out_pipelineLayout)
{
	// Create a pipeline layout which is to be used to create the pipeline which 
	// uses the given descriptor set layout

	// This method can be used to set up different pipeline layouts for different descriptor sets.

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.pNext = nullptr;
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &in_descriptorSetLayout;

	VkResult err = vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &out_pipelineLayout);
	ERROR_IF(err, "Create pipeline layout: " << vkTools::errorString(err));
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
	rasterizationStateCreateInfo.lineWidth = 1.0f;

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
	multisampleStateCreateInfo.pSampleMask = nullptr;
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
	// Store shader modules until after pipeline creation for proper cleanup
	std::vector<ShaderModulePtr> shaderModules;
	for (auto& shader : shaderStagesCreateInfo)
	{
		LOG("Storing: ShaderModule");
		shaderModules.push_back(std::make_unique<ShaderModuleType>(m_device, vkDestroyShaderModule, 
#ifdef _DEBUG
			std::string("ShaderModule"), 
#endif
			shader.module));
	}

	// Vertex input state (use our simple vertex layout with position and color for this pipeline)
	VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = {};
	vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateCreateInfo.pNext = nullptr;
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
	ERROR_IF(err, "Create graphics pipeline: " << vkTools::errorString(err));

	// Shader modules can be destroyed after pipeline has been set up
	shaderModules.clear();
}
