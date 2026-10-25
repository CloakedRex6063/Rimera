#include "rimera_shader.hpp"
#include "rimera_compiler.hpp"
#include "rimera_device.hpp"
#include "rimera_vulkan.hpp"

RM::Pipeline::~Pipeline()
{
    if (m_device && m_pipeline) vkDestroyPipeline(m_device->m_device, m_pipeline, nullptr);
}

void* RM::Pipeline::GetNativeHandle() const
{
    return Vulkan::ToNative(m_pipeline);
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const ComputePipelineCreateInfo& info)
{
    m_device = device;
    m_type = PipelineType::Compute;
    if (!info.shader || info.shader->GetType() != ShaderType::Compute)
        return std::unexpected("Compute pipeline requires a compute shader");

    Vulkan::StageSet stages;
    stages.Add(device, VK_SHADER_STAGE_COMPUTE_BIT, info.shader);
    const VkPipelineCreateFlags2CreateInfo flags_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
        .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
    };
    const VkComputePipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = &flags_info,
        .stage = stages.stages[0],
        .layout = VK_NULL_HANDLE,
    };
    if (const auto result = vkCreateComputePipelines(device->m_device,
                                                     VK_NULL_HANDLE,
                                                     1,
                                                     &pipeline_info,
                                                     nullptr,
                                                     &m_pipeline);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create compute pipeline, " +
                               Vulkan::ResultMessage(result));
    return {};
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const GraphicsPipelineCreateInfo& info)
{
    m_device = device;
    m_type = PipelineType::Graphics;
    if (info.rtv_formats.size() > 8 || info.blend_infos.size() > info.rtv_formats.size())
        return std::unexpected("Invalid graphics attachment count");
    if (Vulkan::HasConflictingLogicOps(info.blend_infos))
        return std::unexpected("Vulkan requires one logic operation for all attachments");
    if (!info.vertex_shader || !info.fragment_shader)
        return std::unexpected("Graphics pipeline requires vertex and fragment shaders");

    Vulkan::StageSet stages;
    stages.Add(device, VK_SHADER_STAGE_VERTEX_BIT, info.vertex_shader);
    stages.Add(device, VK_SHADER_STAGE_FRAGMENT_BIT, info.fragment_shader);

    std::vector<VkVertexInputBindingDescription> bindings;
    for (const auto& binding : info.input_state.bindings)
    {
        bindings.push_back({.binding = binding.binding,
                            .stride = binding.stride,
                            .inputRate = Vulkan::GetInputRate(binding.input_rate)});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const auto& attribute : info.input_state.attributes)
    {
        if (!std::ranges::contains(info.input_state.bindings, attribute.binding, &VertexBinding::binding))
            return std::unexpected("Invalid vertex binding");
        attributes.push_back({.location = attribute.location,
                              .binding = attribute.binding,
                              .format = Vulkan::GetFormat(attribute.format),
                              .offset = attribute.offset});
    }
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size()),
        .pVertexBindingDescriptions = bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = Vulkan::GetPrimitiveTopology(info.polygon_mode),
    };

    Vulkan::RasterState raster(info.rasterizer_state,
                               info.depth_stencil_state,
                               info.rtv_formats.size(),
                               info.blend_infos,
                               true);
    const VkPipelineRenderingCreateInfo rendering = raster.GetRenderingInfo(info.rtv_formats,
                                                                            info.dsv_format);
    const VkPipelineCreateFlags2CreateInfo flags_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
        .pNext = &rendering,
        .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
    };
    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &flags_info,
        .stageCount = static_cast<uint32_t>(stages.stages.size()),
        .pStages = stages.stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &raster.viewport,
        .pRasterizationState = &raster.rasterization,
        .pMultisampleState = &raster.multisample,
        .pDepthStencilState = &raster.depth_stencil,
        .pColorBlendState = &raster.color_blend,
        .pDynamicState = &raster.dynamic,
        .layout = VK_NULL_HANDLE,
    };
    if (const auto result = vkCreateGraphicsPipelines(device->m_device,
                                                      VK_NULL_HANDLE,
                                                      1,
                                                      &pipeline_info,
                                                      nullptr,
                                                      &m_pipeline);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create graphics pipeline, " +
                               Vulkan::ResultMessage(result));
    return {};
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const MeshGraphicsPipelineCreateInfo& info)
{
    if (!device) return std::unexpected("Mesh graphics pipeline requires a valid device");
    if (info.rtv_formats.size() > 8 || info.blend_infos.size() > info.rtv_formats.size())
        return std::unexpected("Invalid mesh graphics attachment count");
    if (Vulkan::HasConflictingLogicOps(info.blend_infos))
        return std::unexpected("Vulkan requires one logic operation for all attachments");
    if (!info.mesh_shader || !info.fragment_shader)
        return std::unexpected("Mesh graphics pipeline requires mesh and fragment shaders");
    m_device = device;
    m_type = PipelineType::MeshGraphics;

    Vulkan::StageSet stages;
    if (info.ampl_shader) stages.Add(device, VK_SHADER_STAGE_TASK_BIT_EXT, info.ampl_shader);
    stages.Add(device, VK_SHADER_STAGE_MESH_BIT_EXT, info.mesh_shader);
    stages.Add(device, VK_SHADER_STAGE_FRAGMENT_BIT, info.fragment_shader);

    Vulkan::RasterState raster(info.rasterizer_state,
                               info.depth_stencil_state,
                               info.rtv_formats.size(),
                               info.blend_infos,
                               false);
    const VkPipelineRenderingCreateInfo rendering = raster.GetRenderingInfo(info.rtv_formats,
                                                                            info.dsv_format);
    const VkPipelineCreateFlags2CreateInfo flags_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
        .pNext = &rendering,
        .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
    };
    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &flags_info,
        .stageCount = static_cast<uint32_t>(stages.stages.size()),
        .pStages = stages.stages.data(),
        .pViewportState = &raster.viewport,
        .pRasterizationState = &raster.rasterization,
        .pMultisampleState = &raster.multisample,
        .pDepthStencilState = &raster.depth_stencil,
        .pColorBlendState = &raster.color_blend,
        .pDynamicState = &raster.dynamic,
        .layout = VK_NULL_HANDLE,
    };
    if (const auto result = vkCreateGraphicsPipelines(device->m_device,
                                                      VK_NULL_HANDLE,
                                                      1,
                                                      &pipeline_info,
                                                      nullptr,
                                                      &m_pipeline);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create mesh graphics pipeline, " +
                               Vulkan::ResultMessage(result));
    return {};
}
