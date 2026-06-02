#include "SkeletalMeshSceneProxy.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/Cloth/ClothTypes.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/FrameContext.h"
#include "Runtime/Engine.h"
#include "Profiling/Time/Timer.h"
#include "Profiling/Stats/Stats.h"

#include <algorithm>
#include <cstring>

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::SkeletalMesh;
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
	RecreateClothDefaultMaterial();
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
	ClothDynamicVertexBuffer.Release();
	ClothDynamicIndexBuffer.Release();
	if (ClothDefaultMaterial)
	{
		FMaterialManager::Get().DestroyTransientMaterial(ClothDefaultMaterial);
		ClothDefaultMaterial = nullptr;
	}
	ReleaseSkinMatrixBuffer();
}   

USkeletalMeshComponent* FSkeletalMeshSceneProxy::GetSkeletalMeshComponent() const
{
	return static_cast<USkeletalMeshComponent*>(GetOwner());
}

void FSkeletalMeshSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
	RebuildClothSectionDraws();
};

void FSkeletalMeshSceneProxy::UpdateMesh()
{
	MeshBuffer = GetOwner()->GetMeshBuffer();
	RebuildSectionDraws();
	ResetClothGeometry();

	CachedDynamicVertexCount = 0;
	UploadedSkinnedRevision = 0;
	UploadedSkinMatrixRevision = 0;
	bDynamicBufferNeedsCreate = true;
	ReleaseSkinMatrixBuffer();

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (Asset)
	{
		CachedDynamicVertexCount = static_cast<uint32>(Asset->Vertices.size());
	}
}

void FSkeletalMeshSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	(void)Frame;

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC || !bVisible || !SMC->IsSkeletalClothBound())
	{
		ResetClothGeometry();
		return;
	}

	FClothRenderData RenderData;
	if (!SMC->GetSkeletalClothRenderData(RenderData) || RenderData.Vertices.empty() || RenderData.Indices.empty())
	{
		ResetClothGeometry();
		return;
	}

	const FMatrix ClothLocalToComponent = SMC->GetSkeletalClothLocalMatrix();
	const FVector4 ClothColor(0.65f, 0.82f, 1.0f, 1.0f);

	ClothVertices.resize(RenderData.Vertices.size());
	for (size_t Index = 0; Index < RenderData.Vertices.size(); ++Index)
	{
		const FClothRenderVertex& SrcVertex = RenderData.Vertices[Index];
		ClothVertices[Index].Position = ClothLocalToComponent.TransformPositionWithW(SrcVertex.Position);
		ClothVertices[Index].Color = ClothColor;
		ClothVertices[Index].SubID = 0;
	}

	ClothIndices = RenderData.Indices;
	ClothIndexCount = static_cast<uint32>(ClothIndices.size());
	RebuildClothSectionDraws();
}

bool FSkeletalMeshSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC) return false;

	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	const TArray<FVertexPNCTT>& SkinnedVertices = SMC->GetSkinnedVertices();
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.size());
	if (VertexCount == 0) return false;

	if (bDynamicBufferNeedsCreate || !DynamicVertexBuffer.GetBuffer())
	{
		DynamicVertexBuffer.Create(Device, CachedDynamicVertexCount ? CachedDynamicVertexCount : VertexCount, sizeof(FVertexPNCTT));
		bDynamicBufferNeedsCreate = false;
	}

	DynamicVertexBuffer.EnsureCapacity(Device, VertexCount);

	const uint64 CurrentRevision = SMC->GetSkinnedRevision();
	if (UploadedSkinnedRevision != CurrentRevision)
	{
		if (!DynamicVertexBuffer.Update(Context, SkinnedVertices.data(), VertexCount))
		{
			return false;
		}
		UploadedSkinnedRevision = CurrentRevision;
	}

	OutBuffer = {};
	OutBuffer.VB = DynamicVertexBuffer.GetBuffer();
	OutBuffer.VBStride = DynamicVertexBuffer.GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

bool FSkeletalMeshSceneProxy::PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	if (!UpdateSkinMatrixBuffer(Device, Context)) return false;

	OutBuffer = {};
	OutBuffer.VB = Asset->RenderBuffer->GetVertexBuffer().GetBuffer();
	OutBuffer.VBStride = Asset->RenderBuffer->GetVertexBuffer().GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

bool FSkeletalMeshSceneProxy::PrepareClothDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
	FDrawCommandBuffer& OutBuffer) const
{
	if (!Device || !Context || ClothVertices.empty() || ClothIndices.empty())
	{
		return false;
	}

	if (!bClothDynamicBuffersCreated)
	{
		ClothDynamicVertexBuffer.Create(Device, static_cast<uint32>(ClothVertices.size()), sizeof(FVertex));
		ClothDynamicIndexBuffer.Create(Device, static_cast<uint32>(ClothIndices.size()));
		bClothDynamicBuffersCreated = true;
	}

	ClothDynamicVertexBuffer.EnsureCapacity(Device, static_cast<uint32>(ClothVertices.size()));
	ClothDynamicIndexBuffer.EnsureCapacity(Device, static_cast<uint32>(ClothIndices.size()));

	if (!ClothDynamicVertexBuffer.Update(Context, ClothVertices.data(), static_cast<uint32>(ClothVertices.size())))
	{
		return false;
	}

	if (!ClothDynamicIndexBuffer.Update(Context, ClothIndices.data(), static_cast<uint32>(ClothIndices.size())))
	{
		return false;
	}

	OutBuffer = {};
	OutBuffer.VB = ClothDynamicVertexBuffer.GetBuffer();
	OutBuffer.VBStride = ClothDynamicVertexBuffer.GetStride();
	OutBuffer.IB = ClothDynamicIndexBuffer.GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

ID3D11ShaderResourceView* FSkeletalMeshSceneProxy::GetSkinMatrixSRV(ID3D11Device* Device, ID3D11DeviceContext* Context) const
{
	UpdateSkinMatrixBuffer(Device, Context);
	return SkinMatrixSRV;
}

void FSkeletalMeshSceneProxy::ReleaseSkinMatrixBuffer() const
{
	if (SkinMatrixSRV)
	{
		SkinMatrixSRV->Release();
		SkinMatrixSRV = nullptr;
	}

	if (SkinMatrixBuffer)
	{
		SkinMatrixBuffer->Release();
		SkinMatrixBuffer = nullptr;
	}

	SkinMatrixCapacity = 0;
}

bool FSkeletalMeshSceneProxy::UpdateSkinMatrixBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Device || !Context || !SMC || !Asset || Asset->Bones.empty()) return false;

	const uint32 MatrixCount = static_cast<uint32>(Asset->Bones.size());
	const uint64 CurrentRevision = SMC->GetSkinnedRevision();

	if (!SkinMatrixBuffer || !SkinMatrixSRV || SkinMatrixCapacity < MatrixCount)
	{
		ReleaseSkinMatrixBuffer();

		D3D11_BUFFER_DESC BufferDesc = {};
		BufferDesc.ByteWidth = sizeof(FMatrix) * MatrixCount;
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufferDesc.StructureByteStride = sizeof(FMatrix);

		if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &SkinMatrixBuffer)))
		{
			ReleaseSkinMatrixBuffer();
			return false;
		}

		SkinMatrixBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen("SkinMatrixBuffer")), "SkinMatrixBuffer");

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = MatrixCount;

		if (FAILED(Device->CreateShaderResourceView(SkinMatrixBuffer, &SRVDesc, &SkinMatrixSRV)))
		{
			ReleaseSkinMatrixBuffer();
			return false;
		}

		SkinMatrixSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen("SkinMatrixSRV")), "SkinMatrixSRV");
		SkinMatrixCapacity = MatrixCount;
		UploadedSkinMatrixRevision = 0;
	}

	if (UploadedSkinMatrixRevision == CurrentRevision)
	{
		return true;
	}

	TArray<FMatrix> SkinMatrices;
	SMC->BuildSkinMatrices(SkinMatrices);
	if (SkinMatrices.size() != MatrixCount) return false;

	{
		SCOPE_STAT_CAT("GPUSkinning_MatrixUpload", "Skinning");

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (FAILED(Context->Map(SkinMatrixBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			return false;
		}

		std::memcpy(Mapped.pData, SkinMatrices.data(), sizeof(FMatrix) * MatrixCount);
		Context->Unmap(SkinMatrixBuffer, 0);
	}

	UploadedSkinMatrixRevision = CurrentRevision;
	return true;
}

void FSkeletalMeshSceneProxy::RebuildSectionDraws()
{
	SectionDraws.clear();

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		MeshBuffer = nullptr;
		SectionDraws.clear();

		return;
	}

	SectionDraws.clear();

	const auto& Slots = Mesh->GetSkeletalMaterials();
	const auto& Overrides = SMC->GetOverrideMaterials();

	for (const FSkeletalMeshSection& Section : Mesh->GetSkeletalMeshAsset()->Sections)
	{
		FMeshSectionDraw Draw;
		Draw.Material = nullptr;
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.IndexCount;


		int32 i = Section.MaterialIndex;
		if (i >= 0 && i < static_cast<int32>(Slots.size()))
		{
			if (i < static_cast<int32>(Overrides.size()) && Overrides[i])
				Draw.Material = Overrides[i];
			else if (Slots[i].MaterialInterface)
				Draw.Material = Slots[i].MaterialInterface;
		}

		if (!Draw.Material)
		{
			Draw.Material = FMaterialManager::Get().GetOrCreateMaterial("None");
		}

		SectionDraws.push_back(Draw);
	}
}

bool FSkeletalMeshSceneProxy::HasRenderableCloth() const
{
	return ClothIndexCount > 0 && !ClothVertices.empty() && !ClothIndices.empty() && !ClothSectionDraws.empty();
}

void FSkeletalMeshSceneProxy::RecreateClothDefaultMaterial()
{
	if (ClothDefaultMaterial)
	{
		FMaterialManager::Get().DestroyTransientMaterial(ClothDefaultMaterial);
		ClothDefaultMaterial = nullptr;
	}

	ClothDefaultMaterial = FMaterialManager::Get().CreateTransientMaterial(
		ERenderPass::Opaque,
		EBlendState::Opaque,
		EDepthStencilState::Default,
		ERasterizerState::SolidNoCull,
		FShaderManager::Get().GetOrCreate(EShaderPath::Primitive));
}

void FSkeletalMeshSceneProxy::RebuildClothSectionDraws()
{
	ClothSectionDraws.clear();
	if (ClothDefaultMaterial && ClothIndexCount > 0)
	{
		ClothSectionDraws.push_back({ ClothDefaultMaterial, 0, ClothIndexCount });
	}
}

void FSkeletalMeshSceneProxy::ResetClothGeometry()
{
	ClothVertices.clear();
	ClothIndices.clear();
	ClothIndexCount = 0;
	RebuildClothSectionDraws();
}
