#pragma once

// https://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// Updated version including corrected derivates from James McLaren and Stephen Hill
struct BarycentricDeriv
{
    float3 lambda;
    float3 dx;
    float3 dy;
};

BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixel_ndc, float2 win_size)
{

  BarycentricDeriv ret = (BarycentricDeriv)0;

  float3 inv_w = rcp(float3(pt0.w, pt1.w, pt2.w));

  float2 ndc0 = pt0.xy * inv_w.x;
  float2 ndc1 = pt1.xy * inv_w.y;
  float2 ndc2 = pt2.xy * inv_w.z;

  float inv_det = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
  ret.dx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * inv_det * inv_w;
  ret.dy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * inv_det * inv_w;
  float ddx_sum = dot(ret.dx, float3(1,1,1));
  float ddy_sum = dot(ret.dy, float3(1,1,1));

  float2 delta_vec = pixel_ndc - ndc0;
  float interp_inv_w = inv_w.x + delta_vec.x*ddx_sum + delta_vec.y*ddy_sum;
  float interp_w = rcp(interp_inv_w);

  ret.lambda.x = interp_w * (inv_w[0] + delta_vec.x*ret.dx.x + delta_vec.y*ret.dy.x);
  ret.lambda.y = interp_w * (0.0f    + delta_vec.x*ret.dx.y + delta_vec.y*ret.dy.y);
  ret.lambda.z = interp_w * (0.0f    + delta_vec.x*ret.dx.z + delta_vec.y*ret.dy.z);

  ret.dx *= (2.0f/win_size.x);
  ret.dy *= (2.0f/win_size.y);
  ddx_sum *= (2.0f/win_size.x);
  ddy_sum *= (2.0f/win_size.y);

  ret.dy *= -1.0f;
  ddy_sum *= -1.0f;

  float interp_w_ddx = 1.0f / (interp_inv_w + ddx_sum);
  float interp_w_ddy = 1.0f / (interp_inv_w + ddy_sum);

  ret.dx = interp_w_ddx * (ret.lambda*interp_inv_w + ret.dx) - ret.lambda;
  ret.dy = interp_w_ddy * (ret.lambda*interp_inv_w + ret.dy) - ret.lambda;  

  return ret;
}

float3 InterpolateWithDeriv(BarycentricDeriv deriv, float v0, float v1, float v2)
{
  float3 merged_v = float3(v0, v1, v2);
  float3 ret;
  ret.x = dot(merged_v, deriv.lambda);
  ret.y = dot(merged_v, deriv.dx);
  ret.z = dot(merged_v, deriv.dy);
  return ret;
}

struct InterpResult2D
{
    float2 interp;
    float2 dx;
    float2 dy;
};

struct InterpResult3D
{
    float3 interp;
    float3 dx;
    float3 dy;
};

// https://static.graphicsprogrammingconference.com/public/2025/talks/visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf
// GPU Zen 4: Advanced Rendering Techniques, Visibility Buffer and Deferred Rendering in DOOM: The Dark Ages
uint PackDerivative(float value, uint flag)
{
    return f32tof16(clamp(value, -1.999, 1.999)) | (flag << 14);
}

float UnpackDerivative(uint packed)
{
    return f16tof32(packed & ~(1u << 14));
}

// https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Renderer/VisibilityBuffer/Shaders/FSL/VisibilityBufferShadingUtilities.h.fsl#L178
InterpResult2D InterpolateWithDeriv(BarycentricDeriv d, float2 v0, float2 v1, float2 v2)
{
    float3x2 m = float3x2(v0, v1, v2); 
    InterpResult2D r;
    r.interp = mul(d.lambda, m);
    r.dx     = mul(d.dx,    m);
    r.dy     = mul(d.dy,    m);
    return r;
}

InterpResult3D InterpolateWithDeriv(BarycentricDeriv d, float3 v0, float3 v1, float3 v2)
{
    float3x3 m = float3x3(v0, v1, v2);
    InterpResult3D r;
    r.interp = mul(d.lambda, m);
    r.dx     = mul(d.dx,    m);
    r.dy     = mul(d.dy,    m);
    return r;
}
