#include "rimera_helpers.hpp"
#include "rimera_device.hpp"

RM::Shader* RM::Helpers::CreateBlitShader(Device* device)
{
    constexpr auto blit_shader = R"(
        struct BlitConstants
        {
            uint source;
            uint dest;
        };
        ConstantBuffer<BlitConstants> PassConstants : register(b1);

        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            Texture2D<float4> source = ResourceDescriptorHeap[PassConstants.source];
            RWTexture2D<float4> dest = ResourceDescriptorHeap[PassConstants.dest];
            uint source_width, source_height;
            uint dest_width, dest_height;
            source.GetDimensions(source_width, source_height);
            dest.GetDimensions(dest_width, dest_height);

            if (id.x >= source_width || id.y >= source_height ||
                id.x >= dest_width || id.y >= dest_height)
                return;

            dest[id.xy] = source.Load(int3(id.xy, 0));
        }
    )";
    constexpr ComputeShaderCreateInfo info{.code = blit_shader, .entry_point = "main"};
    return device->CreateShader(info);
}
