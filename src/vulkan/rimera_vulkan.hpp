#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class Shader;
}

namespace RM::Vulkan
{
    inline constexpr std::array kRequiredDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
    };

    inline constexpr uint32_t kMaxQueuesPerFamily = 4;

    struct FeatureChain
    {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan14Features v14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
        VkPhysicalDeviceMeshShaderFeaturesEXT mesh{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
        VkPhysicalDeviceDescriptorHeapFeaturesEXT heap{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT};
        VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR unified{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR};
        VkPhysicalDeviceMemoryPriorityFeaturesEXT priority{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT};

        FeatureChain()
        {
            features.pNext = &v11;
            v11.pNext = &v12;
            v12.pNext = &v13;
            v13.pNext = &v14;
            v14.pNext = &mesh;
            mesh.pNext = &heap;
        }

        void Link(bool unified_layouts, bool memory_priority)
        {
            heap.pNext = nullptr;
            void** tail = &heap.pNext;
            if (unified_layouts)
            {
                *tail = &unified;
                tail = &unified.pNext;
            }
            if (memory_priority) *tail = &priority;
        }

        FeatureChain(const FeatureChain&) = delete;
        FeatureChain& operator=(const FeatureChain&) = delete;
    };

    struct PhysicalDeviceCandidate
    {
        VkPhysicalDevice device = VK_NULL_HANDLE;
        std::string name;
        int score = 0;
        std::vector<std::string> missing;
        bool unified_image_layouts = false;
        bool memory_priority = false;
        bool hdr_metadata = false;
    };

    [[nodiscard]] const char* ResultName(VkResult result);
    [[nodiscard]] std::string ResultMessage(VkResult result);

    [[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT GetMessengerInfo();
    void CollectMissingFeatures(const FeatureChain& chain, std::vector<std::string>& missing);
    void EnableFeatures(FeatureChain& chain, const FeatureChain& supported);
    [[nodiscard]] std::string Join(const std::vector<std::string>& items);
    [[nodiscard]] bool HasExtension(const std::vector<VkExtensionProperties>& extensions,
                                    const char* name);
    [[nodiscard]] PhysicalDeviceCandidate EvaluatePhysicalDevice(VkPhysicalDevice device);

    struct FormatBlock
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t bytes = 0;
    };

    [[nodiscard]] VkFormat GetFormat(Format format);
    [[nodiscard]] bool IsDepthStencil(Format format);
    [[nodiscard]] VkImageAspectFlags GetAspectMask(Format format);
    [[nodiscard]] FormatBlock GetFormatBlock(Format format);
    [[nodiscard]] VkImageCreateInfo GetImageCreateInfo(const TextureCreateInfo& info,
                                                       std::span<const uint32_t> families,
                                                       bool host_transfer);
    [[nodiscard]] VkBufferCreateInfo GetBufferCreateInfo(const BufferCreateInfo& info,
                                                         std::span<const uint32_t> families);

    [[nodiscard]] VkSamplerAddressMode GetAddressMode(Wrap wrap);
    [[nodiscard]] VkCompareOp GetCompareOp(ComparisonFunc func);
    [[nodiscard]] VkStencilOp GetStencilOp(StencilOp op);
    [[nodiscard]] VkStencilOpState GetStencilOpState(const StencilOpState& state,
                                                     uint32_t compare_mask,
                                                     uint32_t write_mask);
    [[nodiscard]] VkBlendFactor GetBlendFactor(BlendFactor factor);
    [[nodiscard]] VkBlendOp GetBlendOp(BlendOp op);
    [[nodiscard]] VkLogicOp GetLogicOp(LogicOp op);
    [[nodiscard]] bool HasConflictingLogicOps(std::span<const BlendInfo> blends);
    [[nodiscard]] VkCullModeFlags GetCullMode(CullMode mode);
    [[nodiscard]] VkPrimitiveTopology GetPrimitiveTopology(PrimitiveTopology topology);
    [[nodiscard]] VkPrimitiveTopology GetPrimitiveTopology(PolygonMode mode);
    [[nodiscard]] VkVertexInputRate GetInputRate(VertexInputRate rate);
    [[nodiscard]] VkPipelineStageFlags2 GetPipelineStage(BarrierStage stage);
    [[nodiscard]] VkAccessFlags2 GetAccess(BarrierAccess access);
    [[nodiscard]] VkImageLayout GetImageLayout(BarrierLayout layout, bool source);
    [[nodiscard]] VkAttachmentLoadOp GetLoadOp(LoadOp op);
    [[nodiscard]] VkAttachmentStoreOp GetStoreOp(StoreOp op);
    [[nodiscard]] VkClearColorValue GetClearColor(Format format, const ClearColor& color);

    struct StageSet
    {
        StageSet()
        {
            stages.reserve(3);
            modules.reserve(3);
            mappings.reserve(3);
        }

        StageSet(const StageSet&) = delete;
        StageSet& operator=(const StageSet&) = delete;

        void Add(const Device* device, VkShaderStageFlagBits stage, const Shader* shader);

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        std::vector<VkShaderModuleCreateInfo> modules;
        std::vector<VkShaderDescriptorSetAndBindingMappingInfoEXT> mappings;
    };

    struct RasterState
    {
        RasterState(const RasterizerState& rasterizer,
                    const DepthStencilState& depth_stencil_state,
                    size_t color_count,
                    std::span<const BlendInfo> blends,
                    bool dynamic_topology);

        RasterState(const RasterState&) = delete;
        RasterState& operator=(const RasterState&) = delete;

        [[nodiscard]] VkPipelineRenderingCreateInfo GetRenderingInfo(
            std::span<const Format> color_formats,
            std::optional<Format> depth_format);

        VkPipelineViewportStateCreateInfo viewport{};
        VkPipelineRasterizationStateCreateInfo rasterization{};
        VkPipelineMultisampleStateCreateInfo multisample{};
        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        VkPipelineColorBlendStateCreateInfo color_blend{};
        VkPipelineDynamicStateCreateInfo dynamic{};
        std::vector<VkPipelineColorBlendAttachmentState> attachments;
        std::vector<VkFormat> formats;
        std::array<VkDynamicState, 4> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT,
                                                     VK_DYNAMIC_STATE_SCISSOR,
                                                     VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                                                     VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY};
    };

    struct DisplayHdr
    {
        DisplayInfo info;
        VkHdrMetadataEXT metadata{VK_STRUCTURE_TYPE_HDR_METADATA_EXT};
    };

    [[nodiscard]] DisplayHdr QueryDisplayHdr(void* window);
#ifdef RIMERA_WINDOWS
    [[nodiscard]] float GetSdrWhiteLevel(const wchar_t* device_name);
#endif

    [[nodiscard]] constexpr uint32_t GetQueueIndex(QueueType type)
    {
        return static_cast<uint32_t>(type);
    }

    template <typename T>
    [[nodiscard]] void* ToNative(T handle)
    {
        if constexpr (std::is_pointer_v<T>)
            return reinterpret_cast<void*>(handle);
        else
            return reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
    }

    template <typename T>
    [[nodiscard]] T FromNative(void* native)
    {
        if constexpr (std::is_pointer_v<T>)
            return reinterpret_cast<T>(native);
        else
            return static_cast<T>(reinterpret_cast<uintptr_t>(native));
    }
}
