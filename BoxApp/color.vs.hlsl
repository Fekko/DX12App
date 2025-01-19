#include "color.hlsli"

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
};

VertexOut main(VertexIn vin)
{
    VertexOut vout;
    
    // Transform to homogenous clip space
    vout.PosH = mul(float4(vin.Pos, 1), gWorldViewProj);
    
    vout.Color = vin.Color;
    return vout;
};