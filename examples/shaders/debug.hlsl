cbuffer Indices : register(b3)
{
    uint visibility_index;
    uint color_index;
    uint2 size;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= size.x || id.y >= size.y) return;
    Texture2D<uint> visibility = ResourceDescriptorHeap[visibility_index];
    RWTexture2D<float4> color = ResourceDescriptorHeap[color_index];
    uint value = visibility.Load(int3(id.xy, 0));
    if (value == 0)
    {
        color[id.xy] = float4(0.392157, 0.584314, 0.929412, 1.0);
        return;
    }
    uint hash = value * 2654435761u;
    hash ^= hash >> 16;
    color[id.xy] = float4(hash & 255, (hash >> 8) & 255, (hash >> 16) & 255, 255) / 255.0;
}
