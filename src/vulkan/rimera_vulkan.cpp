#include "cwchar"
#include "rimera_vulkan.hpp"
#include "rimera_compiler.hpp"
#include "rimera_device.hpp"
#ifdef RIMERA_WINDOWS
#include "dxgi1_6.h"
#endif

const char* RM::Vulkan::ResultName(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VK_ERROR_UNKNOWN";
    }
}

std::string RM::Vulkan::ResultMessage(VkResult result)
{
    return std::string(ResultName(result)) + " (" + std::to_string(static_cast<int>(result)) +
           ")";
}

namespace RM::Vulkan
{
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*)
    {
        const char* level = "INFO";
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            level = "ERROR";
        else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            level = "WARNING";
        std::fprintf(stderr, "[Vulkan:%s] %s\n", level, data->pMessage);
        return VK_FALSE;
    }

    VkDebugUtilsMessengerCreateInfoEXT GetMessengerInfo()
    {
        return {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = DebugCallback,
        };
    }

    void CollectMissingFeatures(const FeatureChain& chain, std::vector<std::string>& missing)
    {
        const auto require = [&](VkBool32 supported, const char* name)
        {
            if (!supported) missing.emplace_back(name);
        };
        require(chain.features.features.shaderInt16, "shaderInt16");
        require(chain.features.features.samplerAnisotropy, "samplerAnisotropy");
        require(chain.features.features.fillModeNonSolid, "fillModeNonSolid");
        require(chain.features.features.depthClamp, "depthClamp");
        require(chain.features.features.depthBiasClamp, "depthBiasClamp");
        require(chain.features.features.logicOp, "logicOp");
        require(chain.v11.storageBuffer16BitAccess, "storageBuffer16BitAccess");
        require(chain.v12.timelineSemaphore, "timelineSemaphore");
        require(chain.v12.bufferDeviceAddress, "bufferDeviceAddress");
        require(chain.v12.runtimeDescriptorArray, "runtimeDescriptorArray");
        require(chain.v12.scalarBlockLayout, "scalarBlockLayout");
        require(chain.v12.samplerFilterMinmax, "samplerFilterMinmax");
        require(chain.v12.shaderSampledImageArrayNonUniformIndexing,
                "shaderSampledImageArrayNonUniformIndexing");
        require(chain.v12.shaderStorageImageArrayNonUniformIndexing,
                "shaderStorageImageArrayNonUniformIndexing");
        require(chain.v12.shaderStorageBufferArrayNonUniformIndexing,
                "shaderStorageBufferArrayNonUniformIndexing");
        require(chain.v13.dynamicRendering, "dynamicRendering");
        require(chain.v13.synchronization2, "synchronization2");
        require(chain.v14.maintenance5, "maintenance5");
        require(chain.v14.hostImageCopy, "hostImageCopy");
        require(chain.mesh.meshShader, "meshShader");
        require(chain.mesh.taskShader, "taskShader");
        require(chain.heap.descriptorHeap, "descriptorHeap");
    }

    void EnableFeatures(FeatureChain& chain, const FeatureChain& supported)
    {
        chain.features.features.shaderInt16 = VK_TRUE;
        chain.features.features.samplerAnisotropy = VK_TRUE;
        chain.features.features.fillModeNonSolid = VK_TRUE;
        chain.features.features.depthClamp = VK_TRUE;
        chain.features.features.depthBiasClamp = VK_TRUE;
        chain.features.features.logicOp = VK_TRUE;
        chain.v11.storageBuffer16BitAccess = VK_TRUE;
        chain.v12.timelineSemaphore = VK_TRUE;
        chain.v12.bufferDeviceAddress = VK_TRUE;
        chain.v12.runtimeDescriptorArray = VK_TRUE;
        chain.v12.scalarBlockLayout = VK_TRUE;
        chain.v12.samplerFilterMinmax = VK_TRUE;
        chain.v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        chain.v12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
        chain.v12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        chain.v13.dynamicRendering = VK_TRUE;
        chain.v13.synchronization2 = VK_TRUE;
        chain.v14.maintenance5 = VK_TRUE;
        chain.v14.hostImageCopy = VK_TRUE;
        chain.mesh.meshShader = VK_TRUE;
        chain.mesh.taskShader = VK_TRUE;
        chain.heap.descriptorHeap = VK_TRUE;
        chain.unified.unifiedImageLayouts = supported.unified.unifiedImageLayouts;
        chain.priority.memoryPriority = supported.priority.memoryPriority;
    }

    std::string Join(const std::vector<std::string>& items)
    {
        std::string joined;
        for (const auto& item : items)
        {
            if (!joined.empty()) joined += ", ";
            joined += item;
        }
        return joined;
    }

    bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
    {
        return std::ranges::any_of(extensions,
                                   [&](const VkExtensionProperties& extension)
                                   { return std::strcmp(extension.extensionName, name) == 0; });
    }

    PhysicalDeviceCandidate EvaluatePhysicalDevice(VkPhysicalDevice device)
    {
        PhysicalDeviceCandidate candidate{.device = device};
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        candidate.name = properties.deviceName;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) candidate.score += 1000;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) candidate.score += 100;

        if (properties.apiVersion < VK_API_VERSION_1_4) candidate.missing.emplace_back("Vulkan 1.4");

        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions.data());
        for (const char* name : kRequiredDeviceExtensions)
        {
            if (!HasExtension(extensions, name)) candidate.missing.emplace_back(name);
        }

        if (candidate.missing.empty())
        {
            const bool has_unified =
                HasExtension(extensions, VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME);
            const bool has_priority = HasExtension(extensions, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
            FeatureChain supported;
            supported.Link(has_unified, has_priority);
            vkGetPhysicalDeviceFeatures2(device, &supported.features);
            CollectMissingFeatures(supported, candidate.missing);
            candidate.unified_image_layouts = has_unified && supported.unified.unifiedImageLayouts;
            candidate.memory_priority = has_priority && supported.priority.memoryPriority;
            candidate.hdr_metadata = HasExtension(extensions, VK_EXT_HDR_METADATA_EXTENSION_NAME);
        }
        return candidate;
    }
}

namespace RM::Vulkan
{
    VkSamplerAddressMode GetAddressMode(Wrap wrap)
    {
        switch (wrap)
        {
            case Wrap::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case Wrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case Wrap::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case Wrap::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        }
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    VkCompareOp GetCompareOp(ComparisonFunc func)
    {
        switch (func)
        {
            case ComparisonFunc::Always: return VK_COMPARE_OP_ALWAYS;
            case ComparisonFunc::Never: return VK_COMPARE_OP_NEVER;
            case ComparisonFunc::Less: return VK_COMPARE_OP_LESS;
            case ComparisonFunc::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case ComparisonFunc::Greater: return VK_COMPARE_OP_GREATER;
            case ComparisonFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case ComparisonFunc::Equal: return VK_COMPARE_OP_EQUAL;
            case ComparisonFunc::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        }
        return VK_COMPARE_OP_ALWAYS;
    }

    VkStencilOp GetStencilOp(StencilOp op)
    {
        switch (op)
        {
            case StencilOp::Keep: return VK_STENCIL_OP_KEEP;
            case StencilOp::Zero: return VK_STENCIL_OP_ZERO;
            case StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
            case StencilOp::IncreaseSat: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case StencilOp::DecreaseSat: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case StencilOp::Invert: return VK_STENCIL_OP_INVERT;
            case StencilOp::Increment: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case StencilOp::Decrement: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        }
        return VK_STENCIL_OP_KEEP;
    }

    VkStencilOpState GetStencilOpState(const StencilOpState& state,
                                       uint32_t compare_mask,
                                       uint32_t write_mask)
    {
        return {
            .failOp = GetStencilOp(state.StencilFailOp),
            .passOp = GetStencilOp(state.StencilPassOp),
            .depthFailOp = GetStencilOp(state.StencilDepthFailOp),
            .compareOp = GetCompareOp(state.StencilFunc),
            .compareMask = compare_mask,
            .writeMask = write_mask,
            .reference = 0,
        };
    }

    VkBlendFactor GetBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
            case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::InvSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::InvSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DestColor: return VK_BLEND_FACTOR_DST_COLOR;
            case BlendFactor::InvDestColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::DestAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::InvDestAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BlendFactor::SrcAlphaSat: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            case BlendFactor::ColorFactor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case BlendFactor::InvColorFactor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            case BlendFactor::Src1Color: return VK_BLEND_FACTOR_SRC1_COLOR;
            case BlendFactor::InvSrc1Color: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            case BlendFactor::Src1Alpha: return VK_BLEND_FACTOR_SRC1_ALPHA;
            case BlendFactor::InvSrc1Alpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            case BlendFactor::AlphaFactor: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
            case BlendFactor::InvAlphaFactor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        }
        return VK_BLEND_FACTOR_ZERO;
    }

    VkBlendOp GetBlendOp(BlendOp op)
    {
        switch (op)
        {
            case BlendOp::Add: return VK_BLEND_OP_ADD;
            case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
            case BlendOp::RevSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case BlendOp::Min: return VK_BLEND_OP_MIN;
            case BlendOp::Max: return VK_BLEND_OP_MAX;
        }
        return VK_BLEND_OP_ADD;
    }

    VkLogicOp GetLogicOp(LogicOp op)
    {
        switch (op)
        {
            case LogicOp::Clear: return VK_LOGIC_OP_CLEAR;
            case LogicOp::And: return VK_LOGIC_OP_AND;
            case LogicOp::AndReverse: return VK_LOGIC_OP_AND_REVERSE;
            case LogicOp::Copy: return VK_LOGIC_OP_COPY;
            case LogicOp::AndInverted: return VK_LOGIC_OP_AND_INVERTED;
            case LogicOp::NoOp: return VK_LOGIC_OP_NO_OP;
            case LogicOp::Xor: return VK_LOGIC_OP_XOR;
            case LogicOp::Or: return VK_LOGIC_OP_OR;
            case LogicOp::Nor: return VK_LOGIC_OP_NOR;
            case LogicOp::Equivalent: return VK_LOGIC_OP_EQUIVALENT;
            case LogicOp::Invert: return VK_LOGIC_OP_INVERT;
            case LogicOp::OrReverse: return VK_LOGIC_OP_OR_REVERSE;
            case LogicOp::CopyInverted: return VK_LOGIC_OP_COPY_INVERTED;
            case LogicOp::OrInverted: return VK_LOGIC_OP_OR_INVERTED;
            case LogicOp::Nand: return VK_LOGIC_OP_NAND;
            case LogicOp::Set: return VK_LOGIC_OP_SET;
        }
        return VK_LOGIC_OP_COPY;
    }

    bool HasConflictingLogicOps(std::span<const BlendInfo> blends)
    {
        const BlendInfo* first = nullptr;
        for (const auto& blend : blends)
        {
            if (!blend.logic_op_enable) continue;
            if (!first) first = &blend;
            else if (blend.logic_op != first->logic_op) return true;
        }
        return false;
    }

    VkCullModeFlags GetCullMode(CullMode mode)
    {
        switch (mode)
        {
            case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::None: return VK_CULL_MODE_NONE;
        }
        return VK_CULL_MODE_NONE;
    }

    VkPrimitiveTopology GetPrimitiveTopology(PrimitiveTopology topology)
    {
        switch (topology)
        {
            case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    VkPrimitiveTopology GetPrimitiveTopology(PolygonMode mode)
    {
        switch (mode)
        {
            case PolygonMode::Point: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case PolygonMode::Line: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PolygonMode::Triangle: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    VkVertexInputRate GetInputRate(VertexInputRate rate)
    {
        return rate == VertexInputRate::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                 : VK_VERTEX_INPUT_RATE_VERTEX;
    }

    VkPipelineStageFlags2 GetPipelineStage(BarrierStage stage)
    {
        const auto bits = static_cast<uint32_t>(stage);
        VkPipelineStageFlags2 result = VK_PIPELINE_STAGE_2_NONE;
        const auto add = [&](BarrierStage flag, VkPipelineStageFlags2 native)
        {
            if (bits & static_cast<uint32_t>(flag)) result |= native;
        };
        add(BarrierStage::VertexShading, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
        add(BarrierStage::PixelShading, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        add(BarrierStage::ComputeShading, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        add(BarrierStage::Copy, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        add(BarrierStage::AmplificationShading, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT);
        add(BarrierStage::MeshShading, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        add(BarrierStage::AccelerationStructureBuild,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
        add(BarrierStage::AccelerationStructureCopy,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR);
        add(BarrierStage::RenderTarget, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        add(BarrierStage::DepthStencil,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
        add(BarrierStage::IndexInput, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
        return result;
    }

    VkAccessFlags2 GetAccess(BarrierAccess access)
    {
        const auto bits = static_cast<uint32_t>(access);
        if (bits & static_cast<uint32_t>(BarrierAccess::NoAccess)) return VK_ACCESS_2_NONE;
        VkAccessFlags2 result = VK_ACCESS_2_NONE;
        const auto add = [&](BarrierAccess flag, VkAccessFlags2 native)
        {
            if (bits & static_cast<uint32_t>(flag)) result |= native;
        };
        add(BarrierAccess::VertexBuffer, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
        add(BarrierAccess::ConstantBuffer, VK_ACCESS_2_UNIFORM_READ_BIT);
        add(BarrierAccess::IndexBuffer, VK_ACCESS_2_INDEX_READ_BIT);
        add(BarrierAccess::RenderTarget,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        add(BarrierAccess::UnorderedAccess,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        add(BarrierAccess::DepthStencilWrite,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        add(BarrierAccess::DepthStencilRead, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        add(BarrierAccess::ShaderResource,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        add(BarrierAccess::IndirectArgument, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        add(BarrierAccess::CopyDestination, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        add(BarrierAccess::CopySource, VK_ACCESS_2_TRANSFER_READ_BIT);
        add(BarrierAccess::ResolveDestination, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        add(BarrierAccess::ResolveSource, VK_ACCESS_2_TRANSFER_READ_BIT);
        add(BarrierAccess::AccelerationStructureRead, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        add(BarrierAccess::AccelerationStructureWrite,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
        add(BarrierAccess::ShadingRateSource,
            VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR);
        return result;
    }

    VkImageLayout GetImageLayout(BarrierLayout layout, bool source)
    {
        switch (layout)
        {
            case BarrierLayout::Undefined:
                return VK_IMAGE_LAYOUT_UNDEFINED;
            case BarrierLayout::Present:
                return source ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            default:
                return VK_IMAGE_LAYOUT_GENERAL;
        }
    }

    VkAttachmentLoadOp GetLoadOp(LoadOp op)
    {
        switch (op)
        {
            case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    VkAttachmentStoreOp GetStoreOp(StoreOp op)
    {
        return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                    : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
}

namespace RM::Vulkan
{
    void StageSet::Add(const Device* device, VkShaderStageFlagBits stage, const Shader* shader)
    {
        modules.push_back({
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shader->GetBytecodeSize(),
            .pCode = static_cast<const uint32_t*>(shader->GetBytecode()),
        });
        mappings.push_back(
            *static_cast<const VkShaderDescriptorSetAndBindingMappingInfoEXT*>(
                device->GetNativeRootSignature()));
        mappings.back().pNext = &modules.back();
        stages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = &mappings.back(),
            .stage = stage,
            .module = VK_NULL_HANDLE,
            .pName = shader->GetEntryPoint().data(),
        });
    }

    RasterState::RasterState(const RasterizerState& rasterizer,
                             const DepthStencilState& depth_stencil_state,
                             size_t color_count,
                             std::span<const BlendInfo> blends,
                             bool dynamic_topology)
    {
        viewport = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };
        rasterization = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = rasterizer.depth_clip_enable ? VK_FALSE : VK_TRUE,
            .polygonMode = rasterizer.fill_mode == FillMode::Wireframe ? VK_POLYGON_MODE_LINE
                                                                       : VK_POLYGON_MODE_FILL,
            .cullMode = GetCullMode(rasterizer.cull_mode),
            .frontFace = rasterizer.front_face == FrontFace::CounterClockwise
                             ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                             : VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = rasterizer.depth_bias != 0 || rasterizer.slope_scaled_depth_bias != 0.f
                                   ? VK_TRUE
                                   : VK_FALSE,
            .depthBiasConstantFactor = static_cast<float>(rasterizer.depth_bias),
            .depthBiasClamp = rasterizer.depth_bias_clamp,
            .depthBiasSlopeFactor = rasterizer.slope_scaled_depth_bias,
            .lineWidth = 1.f,
        };
        multisample = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        depth_stencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = depth_stencil_state.depth_enable ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = depth_stencil_state.depth_write_enable ? VK_TRUE : VK_FALSE,
            .depthCompareOp = GetCompareOp(depth_stencil_state.depth_test),
            .stencilTestEnable = depth_stencil_state.stencil_enable ? VK_TRUE : VK_FALSE,
            .front = GetStencilOpState(depth_stencil_state.front_stencil_op,
                                       depth_stencil_state.stencil_read_mask,
                                       depth_stencil_state.stencil_write_mask),
            .back = GetStencilOpState(depth_stencil_state.back_stencil_op,
                                      depth_stencil_state.stencil_read_mask,
                                      depth_stencil_state.stencil_write_mask),
        };
        for (size_t i = 0; i < color_count; ++i)
        {
            const auto blend = i < blends.size() ? blends[i] : BlendInfo{};
            attachments.push_back({
                .blendEnable = blend.blend_enable ? VK_TRUE : VK_FALSE,
                .srcColorBlendFactor = GetBlendFactor(blend.src_color_factor),
                .dstColorBlendFactor = GetBlendFactor(blend.dst_color_factor),
                .colorBlendOp = GetBlendOp(blend.color_blend_op),
                .srcAlphaBlendFactor = GetBlendFactor(blend.src_alpha_factor),
                .dstAlphaBlendFactor = GetBlendFactor(blend.dst_alpha_factor),
                .alphaBlendOp = GetBlendOp(blend.alpha_blend_op),
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            });
        }
        const auto logic = std::ranges::find_if(blends, &BlendInfo::logic_op_enable);
        color_blend = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = logic != blends.end() ? VK_TRUE : VK_FALSE,
            .logicOp = logic != blends.end() ? GetLogicOp(logic->logic_op) : VK_LOGIC_OP_COPY,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .blendConstants = {1.f, 1.f, 1.f, 1.f},
        };
        dynamic = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()) - (dynamic_topology ? 0u : 1u),
            .pDynamicStates = dynamic_states.data(),
        };
    }

    VkPipelineRenderingCreateInfo RasterState::GetRenderingInfo(
        std::span<const Format> color_formats,
        std::optional<Format> depth_format)
    {
        for (const auto format : color_formats) formats.push_back(GetFormat(format));
        VkPipelineRenderingCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = static_cast<uint32_t>(formats.size()),
            .pColorAttachmentFormats = formats.data(),
        };
        if (depth_format)
        {
            info.depthAttachmentFormat = GetFormat(*depth_format);
            if (GetAspectMask(*depth_format) & VK_IMAGE_ASPECT_STENCIL_BIT)
                info.stencilAttachmentFormat = info.depthAttachmentFormat;
        }
        return info;
    }
}

namespace RM::Vulkan
{
    VkClearColorValue GetClearColor(Format format, const ClearColor& color)
    {
        switch (format)
        {
            case Format::R8_UInt:
            case Format::R16_UInt:
            case Format::R32_UInt:
            case Format::RG8_UInt:
            case Format::RG16_UInt:
            case Format::RG32_UInt:
            case Format::RGBA8_UInt:
            case Format::RGBA16_UInt:
            case Format::RGBA32_UInt:
            case Format::RGB10A2_UInt:
                return {.uint32 = {static_cast<uint32_t>(color.r),
                                   static_cast<uint32_t>(color.g),
                                   static_cast<uint32_t>(color.b),
                                   static_cast<uint32_t>(color.a)}};
            case Format::R8_SInt:
            case Format::R16_SInt:
            case Format::R32_SInt:
            case Format::RG8_SInt:
            case Format::RG16_SInt:
            case Format::RG32_SInt:
            case Format::RGBA8_SInt:
            case Format::RGBA16_SInt:
            case Format::RGBA32_SInt:
                return {.int32 = {static_cast<int32_t>(color.r),
                                  static_cast<int32_t>(color.g),
                                  static_cast<int32_t>(color.b),
                                  static_cast<int32_t>(color.a)}};
            default:
                return {.float32 = {color.r, color.g, color.b, color.a}};
        }
    }
}

namespace RM::Vulkan
{
    VkFormat GetFormat(Format format)
    {
        switch (format)
        {
            case Format::A8_UNorm: return VK_FORMAT_A8_UNORM;
            case Format::R8_UNorm: return VK_FORMAT_R8_UNORM;
            case Format::R8_UNorm_sRGB: return VK_FORMAT_R8_SRGB;
            case Format::R8_SNorm: return VK_FORMAT_R8_SNORM;
            case Format::R8_UInt: return VK_FORMAT_R8_UINT;
            case Format::R8_SInt: return VK_FORMAT_R8_SINT;
            case Format::R16_UNorm: return VK_FORMAT_R16_UNORM;
            case Format::R16_SNorm: return VK_FORMAT_R16_SNORM;
            case Format::R16_UInt: return VK_FORMAT_R16_UINT;
            case Format::R16_SInt: return VK_FORMAT_R16_SINT;
            case Format::R16_Float: return VK_FORMAT_R16_SFLOAT;
            case Format::RG8_UNorm: return VK_FORMAT_R8G8_UNORM;
            case Format::RG8_UNorm_sRGB: return VK_FORMAT_R8G8_SRGB;
            case Format::RG8_SNorm: return VK_FORMAT_R8G8_SNORM;
            case Format::RG8_UInt: return VK_FORMAT_R8G8_UINT;
            case Format::RG8_SInt: return VK_FORMAT_R8G8_SINT;
            case Format::B5G6R5_UNorm: return VK_FORMAT_R5G6B5_UNORM_PACK16;
            case Format::BGR5A1_UNorm: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
            case Format::R32_UInt: return VK_FORMAT_R32_UINT;
            case Format::R32_SInt: return VK_FORMAT_R32_SINT;
            case Format::R32_Float: return VK_FORMAT_R32_SFLOAT;
            case Format::RG16_UNorm: return VK_FORMAT_R16G16_UNORM;
            case Format::RG16_SNorm: return VK_FORMAT_R16G16_SNORM;
            case Format::RG16_UInt: return VK_FORMAT_R16G16_UINT;
            case Format::RG16_SInt: return VK_FORMAT_R16G16_SINT;
            case Format::RG16_Float: return VK_FORMAT_R16G16_SFLOAT;
            case Format::RGBA8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
            case Format::RGBA8_UNorm_sRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case Format::RGBA8_SNorm: return VK_FORMAT_R8G8B8A8_SNORM;
            case Format::RGBA8_UInt: return VK_FORMAT_R8G8B8A8_UINT;
            case Format::RGBA8_SInt: return VK_FORMAT_R8G8B8A8_SINT;
            case Format::BGRA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::BGRA8_UNorm_sRGB: return VK_FORMAT_B8G8R8A8_SRGB;
            case Format::RGB10A2_UNorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            case Format::RGB10A2_UInt: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
            case Format::RG11B10_Float: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            case Format::RGB9E5_Float: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
            case Format::RG32_UInt: return VK_FORMAT_R32G32_UINT;
            case Format::RG32_SInt: return VK_FORMAT_R32G32_SINT;
            case Format::RG32_Float: return VK_FORMAT_R32G32_SFLOAT;
            case Format::RGBA16_UNorm: return VK_FORMAT_R16G16B16A16_UNORM;
            case Format::RGBA16_SNorm: return VK_FORMAT_R16G16B16A16_SNORM;
            case Format::RGBA16_UInt: return VK_FORMAT_R16G16B16A16_UINT;
            case Format::RGBA16_SInt: return VK_FORMAT_R16G16B16A16_SINT;
            case Format::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Format::RGBA32_UInt: return VK_FORMAT_R32G32B32A32_UINT;
            case Format::RGBA32_SInt: return VK_FORMAT_R32G32B32A32_SINT;
            case Format::RGBA32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case Format::BC1_RGBA_UNorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            case Format::BC1_RGBA_UNorm_sRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
            case Format::BC2_RGBA_UNorm: return VK_FORMAT_BC2_UNORM_BLOCK;
            case Format::BC2_RGBA_UNorm_sRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
            case Format::BC3_RGBA_UNorm: return VK_FORMAT_BC3_UNORM_BLOCK;
            case Format::BC3_RGBA_UNorm_sRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
            case Format::BC4_R_UNorm: return VK_FORMAT_BC4_UNORM_BLOCK;
            case Format::BC4_R_SNorm: return VK_FORMAT_BC4_SNORM_BLOCK;
            case Format::BC5_RG_UNorm: return VK_FORMAT_BC5_UNORM_BLOCK;
            case Format::BC5_RG_SNorm: return VK_FORMAT_BC5_SNORM_BLOCK;
            case Format::BC6H_RGB_Float: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
            case Format::BC6H_RGB_UFloat: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
            case Format::BC7_RGBA_UNorm: return VK_FORMAT_BC7_UNORM_BLOCK;
            case Format::BC7_RGBA_UNorm_sRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
            case Format::Depth16_UNorm: return VK_FORMAT_D16_UNORM;
            case Format::Depth32_Float: return VK_FORMAT_D32_SFLOAT;
            case Format::Stencil8: return VK_FORMAT_S8_UINT;
            case Format::Depth24_UNorm_Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
            case Format::Depth32_Float_Stencil8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
            default: return VK_FORMAT_UNDEFINED;
        }
    }

    bool IsDepthStencil(Format format)
    {
        return format == Format::Depth16_UNorm || format == Format::Depth32_Float ||
               format == Format::Stencil8 || format == Format::Depth24_UNorm_Stencil8 ||
               format == Format::Depth32_Float_Stencil8;
    }

    VkImageAspectFlags GetAspectMask(Format format)
    {
        switch (format)
        {
            case Format::Depth16_UNorm:
            case Format::Depth32_Float:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case Format::Stencil8:
                return VK_IMAGE_ASPECT_STENCIL_BIT;
            case Format::Depth24_UNorm_Stencil8:
            case Format::Depth32_Float_Stencil8:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    FormatBlock GetFormatBlock(Format format)
    {
        switch (format)
        {
            case Format::A8_UNorm:
            case Format::R8_UNorm:
            case Format::R8_UNorm_sRGB:
            case Format::R8_SNorm:
            case Format::R8_UInt:
            case Format::R8_SInt:
                return {1, 1, 1};
            case Format::R16_UNorm:
            case Format::R16_SNorm:
            case Format::R16_UInt:
            case Format::R16_SInt:
            case Format::R16_Float:
            case Format::RG8_UNorm:
            case Format::RG8_UNorm_sRGB:
            case Format::RG8_SNorm:
            case Format::RG8_UInt:
            case Format::RG8_SInt:
            case Format::B5G6R5_UNorm:
            case Format::BGR5A1_UNorm:
                return {1, 1, 2};
            case Format::R32_UInt:
            case Format::R32_SInt:
            case Format::R32_Float:
            case Format::RG16_UNorm:
            case Format::RG16_SNorm:
            case Format::RG16_UInt:
            case Format::RG16_SInt:
            case Format::RG16_Float:
            case Format::RGBA8_UNorm:
            case Format::RGBA8_UNorm_sRGB:
            case Format::RGBA8_SNorm:
            case Format::RGBA8_UInt:
            case Format::RGBA8_SInt:
            case Format::BGRA8_UNorm:
            case Format::BGRA8_UNorm_sRGB:
            case Format::RGB10A2_UNorm:
            case Format::RGB10A2_UInt:
            case Format::RG11B10_Float:
            case Format::RGB9E5_Float:
                return {1, 1, 4};
            case Format::RG32_UInt:
            case Format::RG32_SInt:
            case Format::RG32_Float:
            case Format::RGBA16_UNorm:
            case Format::RGBA16_SNorm:
            case Format::RGBA16_UInt:
            case Format::RGBA16_SInt:
            case Format::RGBA16_Float:
                return {1, 1, 8};
            case Format::RGBA32_UInt:
            case Format::RGBA32_SInt:
            case Format::RGBA32_Float:
                return {1, 1, 16};
            case Format::BC1_RGBA_UNorm:
            case Format::BC1_RGBA_UNorm_sRGB:
            case Format::BC4_R_UNorm:
            case Format::BC4_R_SNorm:
                return {4, 4, 8};
            case Format::BC2_RGBA_UNorm:
            case Format::BC2_RGBA_UNorm_sRGB:
            case Format::BC3_RGBA_UNorm:
            case Format::BC3_RGBA_UNorm_sRGB:
            case Format::BC5_RG_UNorm:
            case Format::BC5_RG_SNorm:
            case Format::BC6H_RGB_Float:
            case Format::BC6H_RGB_UFloat:
            case Format::BC7_RGBA_UNorm:
            case Format::BC7_RGBA_UNorm_sRGB:
                return {4, 4, 16};
            default:
                return {};
        }
    }

    VkImageCreateInfo GetImageCreateInfo(const TextureCreateInfo& info,
                                         std::span<const uint32_t> families,
                                         bool host_transfer)
    {
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (info.usage.contains(TextureUsage::ShaderRead)) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (info.usage.contains(TextureUsage::ShaderWrite)) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (info.usage.contains(TextureUsage::RenderTarget))
            usage |= IsDepthStencil(info.format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                 : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (host_transfer) usage |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
        const bool concurrent = families.size() > 1;
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = GetFormat(info.format),
            .extent = {info.width, info.height, 1},
            .mipLevels = info.mip_levels,
            .arrayLayers = info.array_layers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = concurrent ? static_cast<uint32_t>(families.size()) : 0,
            .pQueueFamilyIndices = concurrent ? families.data() : nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
    }

    VkBufferCreateInfo GetBufferCreateInfo(const BufferCreateInfo& info,
                                           std::span<const uint32_t> families)
    {
        const bool concurrent = families.size() > 1;
        return {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = info.size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = concurrent ? static_cast<uint32_t>(families.size()) : 0,
            .pQueueFamilyIndices = concurrent ? families.data() : nullptr,
        };
    }
}

#ifdef RIMERA_WINDOWS
float RM::Vulkan::GetSdrWhiteLevel(const wchar_t* device_name)
{
    constexpr float kDefaultWhite = 80.f;
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
        ERROR_SUCCESS)
        return kDefaultWhite;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                           &path_count,
                           paths.data(),
                           &mode_count,
                           modes.data(),
                           nullptr) != ERROR_SUCCESS)
        return kDefaultWhite;

    for (UINT32 i = 0; i < path_count; ++i)
    {
        const auto& path = paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                         .size = sizeof(source),
                         .adapterId = path.sourceInfo.adapterId,
                         .id = path.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (std::wcscmp(source.viewGdiDeviceName, device_name) != 0) continue;

        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
                        .size = sizeof(white),
                        .adapterId = path.targetInfo.adapterId,
                        .id = path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS) return kDefaultWhite;
        return static_cast<float>(white.SDRWhiteLevel) / 1000.f * kDefaultWhite;
    }
    return kDefaultWhite;
}

RM::Vulkan::DisplayHdr RM::Vulkan::QueryDisplayHdr(void* window)
{
    const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(window), MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return {};

    ComPtr<IDXGIOutput6> output6;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapter_index = 0;
         !output6 && factory->EnumAdapters1(adapter_index, adapter.ReleaseAndGetAddressOf()) !=
                         DXGI_ERROR_NOT_FOUND;
         ++adapter_index)
    {
        ComPtr<IDXGIOutput> output;
        for (UINT output_index = 0;
             adapter->EnumOutputs(output_index, output.ReleaseAndGetAddressOf()) !=
             DXGI_ERROR_NOT_FOUND;
             ++output_index)
        {
            DXGI_OUTPUT_DESC output_desc{};
            if (FAILED(output->GetDesc(&output_desc)) || output_desc.Monitor != monitor) continue;
            if (FAILED(output.As(&output6))) return {};
            break;
        }
    }
    if (!output6) return {};

    DXGI_OUTPUT_DESC1 desc{};
    if (FAILED(output6->GetDesc1(&desc))) return {};
    if (desc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) return {};

    const float sdr_white = GetSdrWhiteLevel(desc.DeviceName);
    DisplayHdr result;
    result.info = {.hdr = true,
                   .headroom = std::max(1.f, desc.MaxLuminance / sdr_white),
                   .sdr_white_scale = sdr_white / 80.f};
    result.metadata.displayPrimaryRed = {desc.RedPrimary[0], desc.RedPrimary[1]};
    result.metadata.displayPrimaryGreen = {desc.GreenPrimary[0], desc.GreenPrimary[1]};
    result.metadata.displayPrimaryBlue = {desc.BluePrimary[0], desc.BluePrimary[1]};
    result.metadata.whitePoint = {desc.WhitePoint[0], desc.WhitePoint[1]};
    result.metadata.maxLuminance = desc.MaxLuminance;
    result.metadata.minLuminance = desc.MinLuminance;
    result.metadata.maxContentLightLevel = desc.MaxLuminance;
    result.metadata.maxFrameAverageLightLevel = desc.MaxFullFrameLuminance;
    return result;
}
#else
RM::Vulkan::DisplayHdr RM::Vulkan::QueryDisplayHdr(void*)
{
    return {};
}
#endif
