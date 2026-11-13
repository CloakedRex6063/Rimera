cbuffer Tonemap : register(b3)
{
    uint input_index;
    uint output_index;
    float headroom;
    float sdr_white_scale;
    uint hdr;
    uint apply_curve;
};

float3 LinearToSrgb(float3 color)
{
    return select(color <= 0.0031308, color * 12.92, 1.055 * pow(color, 1.0 / 2.4) - 0.055);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    Texture2D<float4> input = ResourceDescriptorHeap[input_index];
    uint width, height;
    input.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    RWTexture2D<float4> output = ResourceDescriptorHeap[output_index];
    float4 color = input.Load(int3(id.xy, 0));
    // REC709
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    bool tonemapped = apply_curve && color.a > 0.0;
    float4 result = tonemapped ? float4(min(color.rgb / (1.0 + luminance / headroom), headroom), color.a) : float4(color.rgb, 1.0);
    result = hdr ? float4(result.rgb * sdr_white_scale, result.a) : float4(LinearToSrgb(saturate(result.rgb)), result.a);
    output[id.xy] = float4(result);
}
