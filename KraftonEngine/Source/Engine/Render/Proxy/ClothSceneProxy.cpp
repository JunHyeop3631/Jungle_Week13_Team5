#include "Render/Proxy/ClothSceneProxy.h"

#include "Component/Primitive/ClothComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/FrameContext.h"

FClothSceneProxy::FClothSceneProxy(UClothComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
	ProxyFlags |= EPrimitiveProxyFlags::NeverCull;

	bCurrentTwoSided = InComponent && InComponent->ShouldRenderTwoSided();
	RecreateDefaultMaterial();
}

FClothSceneProxy::~FClothSceneProxy()
{
	DynamicVertexBuffer.Release();
	DynamicIndexBuffer.Release();
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
	const bool bNewTwoSided = Component && Component->ShouldRenderTwoSided();
	if (bCurrentTwoSided != bNewTwoSided)
	{
		bCurrentTwoSided = bNewTwoSided;
		RecreateDefaultMaterial();
	}

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
		Vertices[Index].Position = RenderData.Vertices[Index].Position;
		Vertices[Index].Color = Component->GetRenderColor();
		Vertices[Index].SubID = 0;
	}

	Indices = RenderData.Indices;
	IndexCount = static_cast<uint32>(Indices.size());
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
		DynamicVertexBuffer.Create(Device, static_cast<uint32>(Vertices.size()), sizeof(FVertex));
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
	return static_cast<UClothComponent*>(GetOwner());
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
		FShaderManager::Get().GetOrCreate(EShaderPath::Primitive));
}

void FClothSceneProxy::RebuildSectionDraws()
{
	SectionDraws.clear();
	if (DefaultMaterial && IndexCount > 0)
	{
		SectionDraws.push_back({ DefaultMaterial, 0, IndexCount });
	}
}
