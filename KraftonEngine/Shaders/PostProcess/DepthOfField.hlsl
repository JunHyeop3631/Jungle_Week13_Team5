// SceneDepth.hlsl
#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

// b2 (PerShader0): SceneDepth visualization
cbuffer DofCB : register(b2)
{
    float FocusDistance;
    float FocalLength;
    float Aperture;
    float MaxBlurSize;
    
    float NearPlane; // m
    float FarPlane; // m
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
    float N = NearPlane;
    float F = FarPlane;

    return (N * F) / max(DeviceDepth * (F - N) + N, 0.0001f);
}

float3 Blur(float2 uv, float BlurRadius, float2 TexelSize)
{
    float3 Color = 0.0f;
    float WeightSum = 0.0f;

    [unroll]
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 SampleUV = uv + DiskSamples[i] * BlurRadius * TexelSize;
        
        // 화면 밖으로 벗어나는 UV인 경우 가중치를 줄이거나 패스하는 방식을 쓸 수 있으나,
        // 여기서는 기본 샘플링을 유지하되 텍셀 연산만 최적화합니다.
        float Weight = 1.0f;
        Color += SceneColorTexture.SampleLevel(LinearClampSampler, SampleUV, 0).rgb * Weight;
        WeightSum += Weight;
    }

    return Color / max(WeightSum, 0.0001f);
}

float ComputeSignedCoC(float Depth)
{
    float z = max(Depth * 10.0f, 0.001f); // m -> mm
    float zf = max(FocusDistance * 10.0f, 0.001f); // m -> mm
    float f = FocalLength; // mm

    float ApertureDiameter = f / max(Aperture, 0.001f);

    // 물리 기반 표준 CoC 유도식 적용
    // 분모의 zf * (z - f) 형태로 구성하여 렌즈 앞/뒤의 부호 역전을 방지합니다.
    float CoC = (ApertureDiameter * f * (z - zf)) / max(z * (zf - f), 0.001f);

    // 연출용 임시 스케일링 멀티플라이어 (필요시 CBuffer에서 조절 가능하도록 구현 권장)
    return clamp(CoC, -1.0f, 1.0f);
}

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    int2 Coord = int2(input.position.xy);
    
    float DeviceDepth = SceneDepthTexture.Load(int3(Coord, 0)).r;
    float Depth = LinearizeDepthMeter(DeviceDepth);
    
    float SignedCoC = ComputeSignedCoC(Depth);
    float BlurRadius = abs(SignedCoC) * MaxBlurSize;
    
    uint Width, Height;
    SceneColorTexture.GetDimensions(Width, Height);
    float2 TexelSize = 1.0f / float2(Width, Height);
    
    float3 SceneColor = SceneColorTexture.SampleLevel(LinearClampSampler, input.uv, 0).rgb;
    float3 BlurredColor = Blur(input.uv, BlurRadius, TexelSize);
    
    float BlurAmount = saturate(abs(SignedCoC));
    float3 FinalColor = lerp(SceneColor, BlurredColor, BlurAmount);
    
    return float4(FinalColor, 1.0f);
}