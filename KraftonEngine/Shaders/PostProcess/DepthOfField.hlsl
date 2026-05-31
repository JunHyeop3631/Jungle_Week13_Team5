#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D<float4> DofFarLayerTexture : register(t27);
Texture2D<float4> DofNearLayerTexture : register(t28);

cbuffer DofCB : register(b2)
{
    float FocusDistance;
    float FocalLength;
    float Aperture;
    float MaxBlurSize;

    float NearZ;
    float FarZ;
    float2 Padding;
}

static const int SAMPLE_COUNT = 16;

static const float2 DiskSamples[SAMPLE_COUNT] =
{
    float2(0.0000f, 0.0000f),
    float2(0.5278f, 0.0859f),
    float2(0.3323f, 0.4431f),
    float2(-0.0401f, 0.5363f),
    float2(-0.4038f, 0.3792f),
    float2(-0.5352f, -0.0432f),
    float2(-0.3263f, -0.4345f),
    float2(0.0468f, -0.5461f),
    float2(0.4174f, -0.3686f),
    float2(0.7912f, 0.2164f),
    float2(0.4201f, 0.7046f),
    float2(-0.2164f, 0.7912f),
    float2(-0.7046f, 0.4201f),
    float2(-0.7912f, -0.2164f),
    float2(-0.4201f, -0.7046f),
    float2(0.2164f, -0.7912f)
};

float LinearizeDepthMeter(float DeviceDepth)
{
    const float N = NearZ;
    const float F = FarZ;
    return (N * F) / max(DeviceDepth * (F - N) + N, 0.0001f);
}

float ComputeSignedCoC(float Depth)
{
    const float z = max(Depth * 10.0f, 0.001f);
    const float zf = max(FocusDistance * 10.0f, 0.001f);
    const float f = FocalLength;
    const float ApertureDiameter = f / max(Aperture, 0.001f);

    const float CoC = (ApertureDiameter * f * (z - zf)) / max(z * (zf - f), 0.001f);
    return clamp(CoC, -1.0f, 1.0f);
}

float SignedCoCAtUV(float2 UV, uint DepthWidth, uint DepthHeight)
{
    int2 Coord = int2(saturate(UV) * float2(DepthWidth - 1, DepthHeight - 1));
    float DeviceDepth = SceneDepthTexture.Load(int3(Coord, 0)).r;
    return ComputeSignedCoC(LinearizeDepthMeter(DeviceDepth));
}

float4 BuildDofLayer(float2 UV, bool bNearLayer)
{
    uint ColorWidth, ColorHeight;
    uint DepthWidth, DepthHeight;
    SceneColorTexture.GetDimensions(ColorWidth, ColorHeight);
    SceneDepthTexture.GetDimensions(DepthWidth, DepthHeight);

    const float CenterCoC = SignedCoCAtUV(UV, DepthWidth, DepthHeight);
    const float CenterAmount = saturate(abs(CenterCoC));

    // Far blur should not cross onto focused/foreground pixels. Near blur is allowed
    // to gather outward so foreground blur can veil the background.
    if (!bNearLayer && CenterCoC <= 0.0f)
    {
        return 0.0f;
    }

    const float2 TexelSize = 1.0f / float2(ColorWidth, ColorHeight);
    const float Radius = max(1.0f, MaxBlurSize * (bNearLayer ? 1.0f : CenterAmount));

    float3 Color = 0.0f;
    float WeightSum = 0.0f;
    float AlphaSum = 0.0f;

    [unroll]
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 SampleUV = UV + DiskSamples[i] * Radius * TexelSize;
        float SampleCoC = SignedCoCAtUV(SampleUV, DepthWidth, DepthHeight);
        bool bInLayer = bNearLayer ? (SampleCoC < 0.0f) : (SampleCoC > 0.0f);
        float Weight = bInLayer ? saturate(abs(SampleCoC)) : 0.0f;

        Color += SceneColorTexture.SampleLevel(LinearClampSampler, SampleUV, 0).rgb * Weight;
        WeightSum += Weight;
        AlphaSum += Weight;
    }

    if (WeightSum <= 0.0001f)
    {
        return 0.0f;
    }

    float Alpha = bNearLayer ? saturate(AlphaSum / (float)SAMPLE_COUNT * 1.5f) : CenterAmount;
    return float4(Color / WeightSum, Alpha);
}

float3 BlurByCoCRange(float2 UV, float BlurRadius, float2 TexelSize, bool bNearBlur, uint Width, uint Height)
{
    float3 Color = 0.0f;
    float WeightSum = 0.0f;

    [unroll]
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 SampleUV = UV + DiskSamples[i] * BlurRadius * TexelSize;
        float SampleCoC = SignedCoCAtUV(SampleUV, Width, Height);
        bool bSampleInRange = bNearBlur ? (SampleCoC < 0.0f) : (SampleCoC > 0.0f);
        float Weight = bSampleInRange ? saturate(abs(SampleCoC)) : 0.0f;

        Color += SceneColorTexture.SampleLevel(LinearClampSampler, SampleUV, 0).rgb * Weight;
        WeightSum += Weight;
    }

    if (WeightSum <= 0.0001f)
    {
        return SceneColorTexture.SampleLevel(LinearClampSampler, UV, 0).rgb;
    }

    return Color / WeightSum;
}

PS_Input_UV VS(uint VertexID : SV_VertexID)
{
    return FullscreenTriangleVS(VertexID);
}

float4 PS_FarLayer(PS_Input_UV Input) : SV_TARGET
{
    return BuildDofLayer(Input.uv, false);
}

float4 PS_NearLayer(PS_Input_UV Input) : SV_TARGET
{
    return BuildDofLayer(Input.uv, true);
}

float4 PS_Composite(PS_Input_UV Input) : SV_TARGET
{
    float4 Scene = SceneColorTexture.SampleLevel(LinearClampSampler, Input.uv, 0);
    float4 FarLayer = DofFarLayerTexture.SampleLevel(LinearClampSampler, Input.uv, 0);
    float4 NearLayer = DofNearLayerTexture.SampleLevel(LinearClampSampler, Input.uv, 0);

    float3 Color = lerp(Scene.rgb, FarLayer.rgb, saturate(FarLayer.a));
    Color = lerp(Color, NearLayer.rgb, saturate(NearLayer.a));

    return float4(Color, Scene.a);
}

float4 PS(PS_Input_UV Input) : SV_TARGET
{
    int2 Coord = int2(Input.position.xy);
    float DeviceDepth = SceneDepthTexture.Load(int3(Coord, 0)).r;
    float Depth = LinearizeDepthMeter(DeviceDepth);

    float SignedCoC = ComputeSignedCoC(Depth);
    float BlurRadius = abs(SignedCoC) * MaxBlurSize;

    uint Width, Height;
    SceneColorTexture.GetDimensions(Width, Height);
    float2 TexelSize = 1.0f / float2(Width, Height);

    float3 SceneColor = SceneColorTexture.SampleLevel(LinearClampSampler, Input.uv, 0).rgb;
    float3 FarBlurredColor = BlurByCoCRange(Input.uv, BlurRadius, TexelSize, false, Width, Height);
    float3 NearBlurredColor = BlurByCoCRange(Input.uv, BlurRadius, TexelSize, true, Width, Height);

    float FarBlurAmount = saturate(SignedCoC);
    float NearBlurAmount = saturate(-SignedCoC);
    float3 FinalColor = lerp(SceneColor, FarBlurredColor, FarBlurAmount);
    FinalColor = lerp(FinalColor, NearBlurredColor, NearBlurAmount);

    return float4(FinalColor, 1.0f);
}
