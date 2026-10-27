#pragma once

static const float PI = 3.14159265359;
static const float INV_PI = 0.31830988618;
static const float MIN_ROUGHNESS = 0.045;
static const float DIELECTRIC_F0 = 0.04;

struct SurfaceData
{
    float3 albedo;
    float3 normal;
    float metallic;
    float roughness;
};

struct BrdfInputs
{
    float3 diffuse_color;
    float3 f0;
    float perceptual_roughness;
    float alpha;
};

// https://github.com/google/filament/blob/main/shaders/src/common_math.glsl
// Apparently faster than pow(x, 5)
float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

BrdfInputs MakeBrdfInputs(SurfaceData surface)
{
    BrdfInputs inputs;
    inputs.diffuse_color = surface.albedo * (1.0 - surface.metallic);
    inputs.f0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), surface.albedo, surface.metallic);
    inputs.perceptual_roughness = clamp(surface.roughness, MIN_ROUGHNESS, 1.0);
    inputs.alpha = inputs.perceptual_roughness * inputs.perceptual_roughness;
    return inputs;
}

float3 FresnelSchlick(float3 f0, float v_dot_h)
{
    return f0 + (1.0 - f0) * Pow5(1.0 - v_dot_h);
}

float FresnelSchlick(float f0, float f90, float v_dot_h)
{
    return f0 + (f90 - f0) * Pow5(1.0 - v_dot_h);
}

float DistributionGGX(float n_dot_h, float alpha)
{
    float a2 = alpha * alpha;
    float d = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float VisibilitySmithGGXCorrelated(float n_dot_v, float n_dot_l, float alpha)
{
    float a2 = alpha * alpha;
    float ggx_v = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2);
    float ggx_l = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2);
    return 0.5 / max(ggx_v + ggx_l, 1e-7);
}

float DiffuseBurley(float n_dot_v, float n_dot_l, float l_dot_h, float perceptual_roughness)
{
    float f90 = 0.5 + 2.0 * perceptual_roughness * l_dot_h * l_dot_h;
    float light_scatter = FresnelSchlick(1.0, f90, n_dot_l);
    float view_scatter = FresnelSchlick(1.0, f90, n_dot_v);
    return light_scatter * view_scatter * INV_PI;
}

float3 SpecularGGX(BrdfInputs inputs, float n_dot_v, float n_dot_l, float n_dot_h, float l_dot_h)
{
    float d = DistributionGGX(n_dot_h, inputs.alpha);
    float v = VisibilitySmithGGXCorrelated(n_dot_v, n_dot_l, inputs.alpha);
    return d * v * FresnelSchlick(inputs.f0, l_dot_h);
}

float3 EvaluateDirectLight(BrdfInputs inputs, float3 n, float3 v, float3 l, float3 radiance)
{
    float n_dot_l = saturate(dot(n, l));
    if (n_dot_l <= 0.0) return float3(0.0, 0.0, 0.0);

    float3 h = normalize(v + l);
    float n_dot_v = max(abs(dot(n, v)), 1e-4);
    float n_dot_h = saturate(dot(n, h));
    float l_dot_h = saturate(dot(l, h));

    float3 specular = SpecularGGX(inputs, n_dot_v, n_dot_l, n_dot_h, l_dot_h);
    float3 diffuse = inputs.diffuse_color * DiffuseBurley(n_dot_v, n_dot_l, l_dot_h, inputs.perceptual_roughness);

    return (diffuse + specular) * radiance * n_dot_l;
}
