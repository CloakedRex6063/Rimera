#pragma once

struct TerrainParams
{
    float height;
    float frequency;
    float exponent;
};

float Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) / 4294967296.0;
}

float Noise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = Hash(uint(i.x) + uint(i.y) * 57u);
    float b = Hash(uint(i.x + 1.0) + uint(i.y) * 57u);
    float c = Hash(uint(i.x) + uint(i.y + 1.0) * 57u);
    float d = Hash(uint(i.x + 1.0) + uint(i.y + 1.0) * 57u);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

//https://www.redblobgames.com/maps/terrain-from-noise/
float TerrainHeight(float2 xz, TerrainParams terrain)
{
    float2 p = xz * terrain.frequency + 1000.0;
    float elevation = 1.0 * Noise2D(0.25 * p) + 0.5 * Noise2D(0.5 * p) + 0.25 * Noise2D(1.0 * p) +
                      0.125 * Noise2D(2.0 * p) + 0.0625 * Noise2D(4.0 * p) + 0.03125 * Noise2D(8.0 * p);
    elevation /= 1.0 + 0.5 + 0.25 + 0.125 + 0.0625 + 0.03125;
    elevation = saturate((elevation - 0.2) / 0.6);
    return pow(elevation, terrain.exponent) * terrain.height;
}

float3 PlaceOnTerrain(float3 position, TerrainParams terrain)
{
    return float3(position.x, position.y + TerrainHeight(position.xz, terrain), position.z);
}

float3 TerrainNormal(float2 xz, TerrainParams terrain, float epsilon)
{
    float left = TerrainHeight(xz - float2(epsilon, 0), terrain);
    float right = TerrainHeight(xz + float2(epsilon, 0), terrain);
    float back = TerrainHeight(xz - float2(0, epsilon), terrain);
    float front = TerrainHeight(xz + float2(0, epsilon), terrain);
    return normalize(float3(left - right, 2.0 * epsilon, back - front));
}
