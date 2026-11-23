#pragma once

struct Sphere
{
    float3 center;
    float radius;
};

float4 NormalizePlane(float4 plane)
{
    return plane / length(plane.xyz);
}

bool IsSphereInsidePlane(Sphere sphere, float4 plane)
{
    return dot(plane.xyz, sphere.center) + plane.w >= -sphere.radius;
}

bool IsSphereInFrustum(Sphere sphere, float4x4 view_projection, bool test_depth = true)
{
    float4 row0 = view_projection[0];
    float4 row1 = view_projection[1];
    float4 row2 = view_projection[2];
    float4 row3 = view_projection[3];

    if (!IsSphereInsidePlane(sphere, NormalizePlane(row3 + row0))) return false;
    if (!IsSphereInsidePlane(sphere, NormalizePlane(row3 - row0))) return false;
    if (!IsSphereInsidePlane(sphere, NormalizePlane(row3 + row1))) return false;
    if (!IsSphereInsidePlane(sphere, NormalizePlane(row3 - row1))) return false;
    if (!test_depth) return true;
    if (!IsSphereInsidePlane(sphere, NormalizePlane(row2))) return false;
    return IsSphereInsidePlane(sphere, NormalizePlane(row3 - row2));
}

float2 DepthClipDistance(float4x4 view_projection, float3 position)
{
    float4 clip = mul(view_projection, float4(position, 1));
    return float2(clip.z, clip.w - clip.z);
}
