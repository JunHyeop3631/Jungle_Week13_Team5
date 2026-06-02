#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/VertexTypes.h"

class USkeletalMeshComponent;
class UMaterial;
struct FFrameContext;
struct FDrawCommandBuffer;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	~FSkeletalMeshSceneProxy() override;

	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;
	bool PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const;
	bool PrepareClothDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const;
	ID3D11ShaderResourceView* GetSkinMatrixSRV(ID3D11Device* Device, ID3D11DeviceContext* Context) const;
	USkeletalMeshComponent* GetSkeletalMeshComponent() const;
	bool HasRenderableCloth() const;
	const TArray<FMeshSectionDraw>& GetClothSectionDraws() const { return ClothSectionDraws; }
	
private:
	void RebuildSectionDraws();
	void RebuildClothSectionDraws();
	void RecreateClothDefaultMaterial();
	void ResetClothGeometry();
	void ReleaseSkinMatrixBuffer() const;
	bool UpdateSkinMatrixBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const;

private:
	mutable FDynamicVertexBuffer DynamicVertexBuffer;
	mutable uint64 UploadedSkinnedRevision = 0;
	uint32 CachedDynamicVertexCount = 0;
	mutable bool bDynamicBufferNeedsCreate = true;

	mutable ID3D11Buffer* SkinMatrixBuffer = nullptr;
	mutable ID3D11ShaderResourceView* SkinMatrixSRV = nullptr;
	mutable uint32 SkinMatrixCapacity = 0;
	mutable uint64 UploadedSkinMatrixRevision = 0;

	TArray<FVertex> ClothVertices;
	TArray<uint32> ClothIndices;
	TArray<FMeshSectionDraw> ClothSectionDraws;
	uint32 ClothIndexCount = 0;
	mutable FDynamicVertexBuffer ClothDynamicVertexBuffer;
	mutable FDynamicIndexBuffer ClothDynamicIndexBuffer;
	mutable bool bClothDynamicBuffersCreated = false;
	UMaterial* ClothDefaultMaterial = nullptr;
};
