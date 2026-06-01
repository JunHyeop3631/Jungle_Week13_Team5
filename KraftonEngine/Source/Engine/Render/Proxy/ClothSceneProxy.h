#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/VertexTypes.h"

class UClothComponent;

class FClothSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FClothSceneProxy(UClothComponent* InComponent);
	~FClothSceneProxy() override;

	void UpdateMesh() override;
	void UpdateMaterial() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
		FDrawCommandBuffer& OutBuffer) const override;

private:
	UClothComponent* GetClothComponent() const;
	void RecreateDefaultMaterial();
	void RebuildSectionDraws();

private:
	TArray<FVertex> Vertices;
	TArray<uint32> Indices;
	uint32 IndexCount = 0;

	mutable FDynamicVertexBuffer DynamicVertexBuffer;
	mutable FDynamicIndexBuffer DynamicIndexBuffer;
	mutable bool bDynamicBuffersCreated = false;
	bool bCurrentTwoSided = false;
};
