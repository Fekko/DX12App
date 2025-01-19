#include "color.hlsli"

VertexOut main(VertexIn vin)
{
    VertexOut vout;
    
    // Transform to homogenous clip space
    float4 pos = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.Pos = mul(pos, gViewProj);
    
    vout.Color = vin.Color;
    return vout;
};