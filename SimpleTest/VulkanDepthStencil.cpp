#include "VulkanDepthStencil.h"
#include "ErrorReporting.h"
#include "VulkanMemoryHelper.h"
#include "vulkantools.h"


VulkanDepthStencilFactory::VulkanDepthStencilFactory(VkDevice in_device, const std::shared_ptr<VulkanMemoryHelper> in_memory)
	: m_device(in_device)
	, m_memory(in_memory)
{

}

void VulkanDepthStencilFactory::CreateDepthStencil(VkFormat in_format, uint32_t in_width, uint32_t in_height,
	VulkanDepthStencil& out_depthStencil)
{
	if (m_memory == nullptr) return;

	// Creation information for the depth stencil image
	VkImageCreateInfo imageCreationInfo = {};
	imageCreationInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreationInfo.pNext = NULL;
	imageCreationInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreationInfo.format = in_format;
	imageCreationInfo.extent = { in_width, in_height, 1 };
	imageCreationInfo.mipLevels = 1;
	imageCreationInfo.arrayLayers = 1;
	imageCreationInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreationInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreationInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageCreationInfo.flags = 0;
	
	VkMemoryAllocateInfo memoryAllocInfo = {};
	memoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocInfo.pNext = NULL;
	memoryAllocInfo.allocationSize = 0;
	memoryAllocInfo.memoryTypeIndex = 0;

	VkImageViewCreateInfo depthStencilViewCreationInfo = {};
	depthStencilViewCreationInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthStencilViewCreationInfo.pNext = NULL;
	depthStencilViewCreationInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthStencilViewCreationInfo.format = in_format;
	depthStencilViewCreationInfo.flags = 0;
	depthStencilViewCreationInfo.subresourceRange = {};
	depthStencilViewCreationInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	depthStencilViewCreationInfo.subresourceRange.baseMipLevel = 0;
	depthStencilViewCreationInfo.subresourceRange.levelCount = 1;
	depthStencilViewCreationInfo.subresourceRange.baseArrayLayer = 0;
	depthStencilViewCreationInfo.subresourceRange.layerCount = 1;


	VkMemoryRequirements memoryRequirements;
	VkResult err;

	// Create the image
	err = vkCreateImage(m_device, &imageCreationInfo, nullptr, &out_depthStencil.m_image);
	if (err) throw ProgramError(std::string("Could not create depth stencil image: ") + vkTools::errorString(err));

	// Allocate memory for the image on the gpu
	vkGetImageMemoryRequirements(m_device, out_depthStencil.m_image, &memoryRequirements);
	memoryAllocInfo.allocationSize = memoryRequirements.size;
	m_memory->GetMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memoryAllocInfo.memoryTypeIndex);
	err = vkAllocateMemory(m_device, &memoryAllocInfo, nullptr, &out_depthStencil.m_gpuMem);
	if (err) throw ProgramError(std::string("Could not allocate depth stencil memory on GPU: ") + vkTools::errorString(err));

	// Bind the image to the allocated memory
	err = vkBindImageMemory(m_device, out_depthStencil.m_image, out_depthStencil.m_gpuMem, 0);
	if (err) throw ProgramError(std::string("Could not bind depth stencil image to GPU memory: ") + vkTools::errorString(err));

	// Set up our view to the image
	depthStencilViewCreationInfo.image = out_depthStencil.m_image;
	err = vkCreateImageView(m_device, &depthStencilViewCreationInfo, nullptr, &out_depthStencil.m_imageView);
	if (err) throw ProgramError(std::string("Could not create depth stencil image view: ") + vkTools::errorString(err));
}

