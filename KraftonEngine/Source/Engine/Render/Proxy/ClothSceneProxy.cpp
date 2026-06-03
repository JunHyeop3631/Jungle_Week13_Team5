#include "Render/Proxy/ClothSceneProxy.h"

#include "Component/Primitive/ClothComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/FrameContext.h"

namespace
{
struct FClothDefaultMaterialConstants
{
	FVector4 SectionColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	float HasNormalMap = 0.0f;
	float Padding[3] = { 0.0f, 0.0f, 0.0f };
};
}

FClothSceneProxy::FClothSceneProxy(UClothComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
	ProxyFlags |= EPrimitiveProxyFlags::NeverCull;
	ProxyFlags |= EPrimitiveProxyFlags::Cloth;

	bCurrentTwoSided = InComponent && InComponent->ShouldRenderClothTwoSided();
	RecreateDefaultMaterial();
}

FClothSceneProxy::~FClothSceneProxy()
{
	DynamicVertexBuffer.Release();
	DynamicIndexBuffer.Release();
	DefaultMaterialCB.Release();
	if (DefaultMaterial)
	{
		FMaterialManager::Get().DestroyTransientMaterial(DefaultMaterial);
		DefaultMaterial = nullptr;
	}
}

void FClothSceneProxy::UpdateMesh()
{
	Vertices.clear();
	Indices.clear();
	IndexCount = 0;
	RebuildSectionDraws();
}

void FClothSceneProxy::UpdateMaterial()
{
	UClothComponent* Component = GetClothComponent();
	const bool bNewTwoSided = Component && Component->ShouldRenderClothTwoSided();
	if (bCurrentTwoSided != bNewTwoSided)
	{
		bCurrentTwoSided = bNewTwoSided;
		RecreateDefaultMaterial();
	}
	UpdateDefaultMaterialConstants();

	RebuildSectionDraws();
}

void FClothSceneProxy::UpdatePerViewport(const FFrameContext& /*Frame*/)
{
	UClothComponent* Component = GetClothComponent();
	if (!Component || !bVisible)
	{
		Vertices.clear();
		Indices.clear();
		IndexCount = 0;
		RebuildSectionDraws();
		return;
	}

	FClothRenderData RenderData;
	if (!Component->GetClothRenderData(RenderData))
	{
		Vertices.clear();
		Indices.clear();
		IndexCount = 0;
		RebuildSectionDraws();
		return;
	}

	Vertices.resize(RenderData.Vertices.size());
	for (size_t Index = 0; Index < RenderData.Vertices.size(); ++Index)
	{
		const FClothRenderVertex& SrcVertex = RenderData.Vertices[Index];
		FVertexPNCTT& DstVertex = Vertices[Index];
		DstVertex.Position = SrcVertex.Position;
		DstVertex.Normal = SrcVertex.Normal.LengthSquared() > 1.0e-8f ? SrcVertex.Normal : FVector::UpVector;
		DstVertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		DstVertex.UV = SrcVertex.UV;
		DstVertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
	}

	Indices = RenderData.Indices;
	IndexCount = static_cast<uint32>(Indices.size());
	UpdateDefaultMaterialConstants();
	RebuildSectionDraws();
}

bool FClothSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
	FDrawCommandBuffer& OutBuffer) const
{
	if (!Device || !Context || Vertices.empty() || Indices.empty())
	{
		return false;
	}

	if (!bDynamicBuffersCreated)
	{
		DynamicVertexBuffer.Create(Device, static_cast<uint32>(Vertices.size()), sizeof(FVertexPNCTT));
		DynamicIndexBuffer.Create(Device, static_cast<uint32>(Indices.size()));
		bDynamicBuffersCreated = true;
	}

	DynamicVertexBuffer.EnsureCapacity(Device, static_cast<uint32>(Vertices.size()));
	DynamicIndexBuffer.EnsureCapacity(Device, static_cast<uint32>(Indices.size()));

	if (!DynamicVertexBuffer.Update(Context, Vertices.data(), static_cast<uint32>(Vertices.size())))
	{
		return false;
	}

	if (!DynamicIndexBuffer.Update(Context, Indices.data(), static_cast<uint32>(Indices.size())))
	{
		return false;
	}

	OutBuffer = {};
	OutBuffer.VB = DynamicVertexBuffer.GetBuffer();
	OutBuffer.VBStride = DynamicVertexBuffer.GetStride();
	OutBuffer.IB = DynamicIndexBuffer.GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

UClothComponent* FClothSceneProxy::GetClothComponent() const
{
	return Cast<UClothComponent>(GetOwner());
}

void FClothSceneProxy::RecreateDefaultMaterial()
{
	if (DefaultMaterial)
	{
		FMaterialManager::Get().DestroyTransientMaterial(DefaultMaterial);
		DefaultMaterial = nullptr;
	}

	DefaultMaterial = FMaterialManager::Get().CreateTransientMaterial(
		ERenderPass::Opaque,
		EBlendState::Opaque,
		EDepthStencilState::Default,
		bCurrentTwoSided ? ERasterizerState::SolidNoCull : ERasterizerState::SolidBackCull,
		FShaderManager::Get().GetOrCreateUberLitPermutation(
			EUberLitDefines::ELightingModel::Unlit,
			EUberLitDefines::EVertexFactory::StaticMesh));
	UpdateDefaultMaterialConstants();
}

void FClothSceneProxy::UpdateDefaultMaterialConstants()
{
	if (!DefaultMaterial)
	{
		return;
	}

	UClothComponent* Component = GetClothComponent();
	FClothDefaultMaterialConstants& Constants =
		DefaultMaterial->BindPerShaderCB<FClothDefaultMaterialConstants>(&DefaultMaterialCB, ECBSlot::PerShader0);
	Constants.SectionColor = Component ? Component->GetClothRenderColor() : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	Constants.HasNormalMap = 0.0f;
}

void FClothSceneProxy::RebuildSectionDraws()
{
	SectionDraws.clear();
	if (IndexCount == 0)
	{
		return;
	}

	UClothComponent* Component = GetClothComponent();
	UMaterialInterface* SectionMaterial = Component ? Component->GetMaterial() : nullptr;
	if (!SectionMaterial)
	{
		SectionMaterial = DefaultMaterial;
	}

	if (SectionMaterial)
	{
		SectionDraws.push_back({ SectionMaterial, 0, IndexCount });
	}
}
