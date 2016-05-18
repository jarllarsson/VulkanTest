#pragma once

#include "vulkan/vulkan.h"
#include "vulkantools.h" // for file loading of shader
#include "ErrorReporting.h"

namespace VulkanShaderLoader
{

	VkPipelineShaderStageCreateInfo LoadShaderSPIRV(const std::string& in_fileName, const char* in_methodName, const VkDevice& in_device, VkShaderStageFlagBits in_stage)
	{
		VkPipelineShaderStageCreateInfo shaderStage = {};
		shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStage.stage = in_stage;
		shaderStage.module = vkTools::loadShader(in_fileName.c_str(), in_device, in_stage);
		shaderStage.pName = in_methodName;
		if (shaderStage.module == NULL) throw ProgramError(std::string("Load SPIR-V shader: ") + in_fileName);
		return shaderStage;
	}

	VkPipelineShaderStageCreateInfo LoadShaderGLSL(const std::string& in_fileName, const char* in_methodName, const VkDevice& in_device, VkShaderStageFlagBits in_stage)
	{
		VkPipelineShaderStageCreateInfo shaderStage = {};
		shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStage.stage = in_stage;
		shaderStage.module = vkTools::loadShaderGLSL(in_fileName.c_str(), in_device, in_stage);
		shaderStage.pName = in_methodName;
		if (shaderStage.module == NULL) throw ProgramError(std::string("Load GLSL shader: ") + in_fileName);
		return shaderStage;
	}
};
