// SceneDepth.hlsl
#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"

// b2 (PerShader0): SceneDepth visualization
cbuffer DofCB : register(b2)
{
    float FocusDistance;
    float FocalLength;
    float Aperture;
    float MaxBlurSize;
}

// SceneDepthTexture (t16) is declared in Common/SystemResources.hlsli

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    int2 Coord = int2(input.position.xy);
    
    float Depth = SceneDepthTexture.Load(int3(Coord, 0));
    
    float BlurRadius = abs(Depth - FocusDistance) * MaxBlurSize;
    
    return float4(v, v, v, 1.0f);
}
