cbuffer Tonemap : register(b3)
{
    uint input_index;
    uint output_index;
    uint2 size;
    float headroom;
    float sdr_white_scale;
    uint hdr;
};

float3 LinearToSrgb(float3 color)
{
    return select(color <= 0.0031308, color * 12.92, 1.055 * pow(color, 1.0 / 2.4) - 0.055);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= size.x || id.y >= size.y) return;
    Texture2D<float4> input = ResourceDescriptorHeap[input_index];
    RWTexture2D<float4> output = ResourceDescriptorHeap[output_index];
    float4 color = input.Load(int3(id.xy, 0));
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float mapped = luminance / (1.0 + luminance / headroom);
    float3 result = luminance > 0.0 ? color.rgb * (mapped / luminance) : float3(0.0, 0.0, 0.0);
    result = min(result, headroom);
    result = hdr ? result * sdr_white_scale : LinearToSrgb(saturate(result));
    output[id.xy] = float4(result, color.a);
}
