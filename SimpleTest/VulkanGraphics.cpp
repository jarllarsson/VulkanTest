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


#define ENABLE_VALIDATION false // set to true to enable debug layer (requires LunarG SDK)
//#define USE_GLSL

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
	// ===================================
	// 1. Set up Vulkan
	// ===================================
	VkResult err = VK_SUCCESS;

	// Create the Vulkan instance
	err = CreateInstance(&m_vulkanInstance);
	if (err) throw ProgramError(std::string("Create Vulkan instance: ") + vkTools::errorString(err));
	
	// Create the physical device object
	// Just get the first physical device for now (otherwise, read into a vector instead of a single reference)
	uint32_t gpuCount = 0;
	err = vkEnumeratePhysicalDevices(m_vulkanInstance, &gpuCount, &m_physicalDevice);
	if (err) throw ProgramError(std::string("Enumerating GPUs: ") + vkTools::errorString(err));
	
	// Initialize memory helper class
	m_memory = std::make_shared<VulkanMemoryHelper>(m_physicalDevice);

	// Get graphics queue index on the current physical device
	m_graphicsQueueIdx = GetGraphicsQueueInternalIndex();

	// Create the logical device
	err = CreateLogicalDevice(m_graphicsQueueIdx, &m_device);
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
	err = CreateCommandPool(&m_commandPool);
	if (err) throw ProgramError(std::string("Create command pool: ") + vkTools::errorString(err));

	// Create command buffers for each frame image buffer in the swap chain, for rendering
	CreateRenderCommandBuffers();

	// Setup depth stencil
	m_depthStencilFactory->CreateDepthStencil(m_depthFormat, m_width, m_height, m_depthStencil);

	// Command buffer for initializing the depth stencil and swap chain 
	// images to the right format on the gpu
	VkCommandBuffer swapchainDepthStencilSetupCommandBuffer = VK_NULL_HANDLE;
	err = m_commandBufferFactory->CreateCommandBuffer(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, swapchainDepthStencilSetupCommandBuffer);
	if (err) throw ProgramError(std::string("Create a setup command buffer for the swapchain and depth stencil: ") + vkTools::errorString(err));
	m_commandBufferFactory->ConstructSwapchainDepthStencilInitializationCommandBuffer(
		swapchainDepthStencilSetupCommandBuffer,
		m_swapChain,
		m_depthStencil);


	// Create the render pass
	err = m_renderPassFactory->CreateStandardRenderPass(m_colorformat, m_depthFormat, m_renderPass);
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

	// Note: Other command buffers if needed should then be created >here<

	// ================================================
	// 3. Prepare application specific usage of Vulkan
	// ================================================

	// Create triangle mesh
	m_triangleMesh = std::make_shared<VulkanMesh>();
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
	CreatePipelineLayout(m_descriptorSetLayoutPerFrame_TriangleProgram, m_pipelineLayout_TriangleProgram); // Create a pipeline which can handle the specified descriptor set layout
	CreateTriangleProgramPipelineAndLoadShaders();
	// Set up the descriptor set pool, this from where
	CreateTriangleProgramDescriptorPool();
	// With the pool we can now allocate the descriptors
	CreateTriangleProgramDescriptorSet();
	// -------------------------------------

	// Set up a command buffer for drawing the mesh
	std::vector<VkDescriptorSet> descriptors = { m_descriptorSetPerFrame };
	VulkanCommandBufferFactory::DrawCommandBufferDependencies drawInfo(
		m_pipelineLayout_TriangleProgram,
		m_pipeline_TriangleProgram,
		descriptors,
		VERTEX_BUFFER_BIND_ID,
		*m_triangleMesh.get(),
		*m_swapChain.get()
		);
	VkClearColorValue clearCol = { { 0.0f, 0.0f, 1.0f, 1.0f } };

	m_commandBufferFactory->ConstructDrawCommandBuffer(m_drawCommandBuffers, m_frameBuffers, 
		drawInfo, m_renderPass, 
		clearCol, m_width, m_height);

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

	return vkCreateInstance(&instanceCreateInfo, nullptr, out_instance);
}

void VulkanGraphics::Destroy()
{
	// Triangle program
	OutputDebugString("Vulkan: Removing pipeline\n");
	vkDestroyPipeline(m_device, m_pipeline_TriangleProgram, nullptr);
	OutputDebugString("Vulkan: Removing pipeline layout\n");
	vkDestroyPipelineLayout(m_device, m_pipelineLayout_TriangleProgram, nullptr);
	OutputDebugString("Vulkan: Removing per-frame descriptor set\n");
	vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayoutPerFrame_TriangleProgram, nullptr);

	OutputDebugString("Vulkan: Freeing mesh data\n");
	vkDestroyBuffer(m_device, m_triangleMesh->m_vertices.m_buffer, nullptr);
	vkFreeMemory(m_device, m_triangleMesh->m_vertices.m_gpuMem, nullptr);
	vkDestroyBuffer(m_device, m_triangleMesh->m_indices.m_buffer, nullptr);
	vkFreeMemory(m_device, m_triangleMesh->m_indices.m_gpuMem, nullptr);

	OutputDebugString("Vulkan: Freeing uniform data\n");
	vkDestroyBuffer(m_device, m_ubufPerFrame->m_allocation.m_buffer, nullptr);
	vkFreeMemory(m_device, m_ubufPerFrame->m_allocation.m_gpuMem, nullptr);

	// General
	OutputDebugString("Vulkan: Removing swap chain\n");
	m_swapChain.reset();

	OutputDebugString("Vulkan: Removing descriptor pool\n");
	vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
	OutputDebugString("Vulkan: Removing command buffers\n");
	DestroyCommandBuffers();
	OutputDebugString("Vulkan: Removing render pass\n");
	vkDestroyRenderPass(m_device, m_renderPass, nullptr);

	OutputDebugString("Vulkan: Removing frame buffers\n");
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_frameBuffers.size()); i++)
	{
		vkDestroyFramebuffer(m_device, m_frameBuffers[i], nullptr);
	}

	
	for (auto& shaderModule : m_shaderModules)
	{
		vkDestroyShaderModule(m_device, shaderModule, nullptr);
	}
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
	if (ENABLE_VALIDATION)
	{
		vkDebug::freeDebugCallback(m_vulkanInstance);
	}
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
		throw ProgramError(std::string("Get queues on selected GPU"));
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

void VulkanGraphics::CreateRenderCommandBuffers()
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
	if (err) throw ProgramError(std::string("Allocate command buffers from pool: ") + vkTools::errorString(err));

	// Create a post-present command buffer, it is used to restore the image layout after presenting
	err = m_commandBufferFactory->CreateCommandBuffer(m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_postPresentCommandBuffer);
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
		if (err) throw ProgramError(std::string("Create frame buffer[") + std::to_string(i) + "] :" + vkTools::errorString(err));
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

	m_ubufPerFrame = std::make_shared<VulkanUniformBufferPerFrame>();
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

	VkResult err = vkCreateDescriptorSetLayout(m_device, &descriptorLayout, NULL, &m_descriptorSetLayoutPerFrame_TriangleProgram);
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

	VkResult err = vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, &m_descriptorPool);
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
		m_shaderModules.push_back(shader.module);
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
	err = vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &pipelineCreateInfo, nullptr, &m_pipeline_TriangleProgram);
	if (err)
	{
		if (err == -1000012000)
		{
			// Hardcoded for now:
			// see http://developer.download.nvidia.com/assets/gameworks/downloads/secure/Vulkan_Beta_Drivers/VK_NV_glsl_shader.txt?autho=1463334899_30ddfca4ebd31a43eeefc10957d34f6f&file=VK_NV_glsl_shader.txt
			throw ProgramError(std::string("Create graphics pipeline: VK_ERROR_INVALID_SHADER_NV\nOne or more shaders failed to compile or link. More details are\nreported back to the application via VK_EXT_debug_report\nif enabled."));
			// If any shader stage fails to compile VK_ERROR_INVALID_SHADER_NV
			// will be generated and the compile log will be reported back to the
			// application by VK_EXT_debug_report if enabled.
		}
		else
		{
			throw ProgramError(std::string("Create graphics pipeline: ") + vkTools::errorString(err));
		}
	}
}
