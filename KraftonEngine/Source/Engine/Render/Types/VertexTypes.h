#pragma once

#include "Math/Vector.h"
#include "Render/Types/RenderTypes.h"
#include <cassert>
#include <cstddef>

struct FVertex
{
	FVector Position;
	FVector4 Color;
	int SubID;
};

struct FOverlayVertex
{
	float X, Y;
};

// Position + TexCoord 범용 버텍스 (FFontGeometry 등 텍스처 기반 동적 지오메트리 공용)
struct FTextureVertex
{
	FVector  Position;
	FVector2 TexCoord;
};

struct FParticleSpriteVertex
{
	FVector Position;
	FVector4 Color;
	FVector2 UV;
};

// Position + Normal + Color + UV (StaticMesh GPU용 정점 형식)
struct FVertexPNCT
{
	FVector Position;
	FVector Normal;
	FVector4 Color;
	FVector2 UV;
};

// Position + Normal + Tangent + Color + UV(StaticMesh GPU용 정점 형식)
struct FVertexPNCTT
{
	FVector Position;
	FVector Normal;
	FVector4 Color;
	FVector2 UV;
	FVector4 Tangent;
};

// Position + Normal + Color + UV + BondIndex + BoneWeight (SkeletalMesh GPU용 정점 형식)
struct FVertexPNCTBW
{
	FVector Position;
	FVector Normal;
	FVector4 Color;
	FVector2 UV;
	FVector4 Tangent;

	int32 BoneIndices[4] = { -1, -1, -1, -1 };
	float BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

template<typename VertexType>
struct TMeshData
{
	TArray<VertexType> Vertices;
	TArray<uint32> Indices;
};

using FMeshData = TMeshData<FVertex>;

// 정점 타입에 무관하게 메시 데이터를 참조하는 뷰.
// Position은 필수이고, Normal/Color/UV/Tangent는 각 메시가 offset layout으로 제공한다.
struct FMeshDataView
{
	static constexpr uint32 InvalidAttributeOffset = 0xFFFFFFFFu;

	const void*   VertexData  = nullptr;
	const uint32* IndexData   = nullptr;
	uint32 VertexCount = 0;
	uint32 IndexCount  = 0;
	uint32 Stride      = 0;
	uint32 PositionOffset = 0;
	uint32 NormalOffset   = InvalidAttributeOffset;
	uint32 ColorOffset    = InvalidAttributeOffset;
	uint32 UVOffset       = InvalidAttributeOffset;
	uint32 TangentOffset  = InvalidAttributeOffset;

	bool IsValid() const { return VertexData && IndexCount > 0; }
	uint32 GetTriangleCount() const { return IndexCount / 3; }
	bool HasAttribute(uint32 Offset, uint32 Size) const
	{
		return VertexData && Offset != InvalidAttributeOffset && Size <= Stride && Offset <= Stride - Size;
	}
	bool HasPosition() const { return HasAttribute(PositionOffset, sizeof(FVector)); }
	bool HasNormal() const { return HasAttribute(NormalOffset, sizeof(FVector)); }
	bool HasColor() const { return HasAttribute(ColorOffset, sizeof(FVector4)); }
	bool HasUV() const { return HasAttribute(UVOffset, sizeof(FVector2)); }
	bool HasTangent() const { return HasAttribute(TangentOffset, sizeof(FVector4)); }

	// N번째 정점을 T 타입으로 반환
	template<typename T>
	const T& GetVertex(uint32 Index) const
	{
		assert(sizeof(T) == Stride && "GetVertex<T>: sizeof(T) must match Stride");
		return *reinterpret_cast<const T*>(
			static_cast<const uint8*>(VertexData) + Index * Stride);
	}

	template<typename T>
	const T& GetAttribute(uint32 Index, uint32 Offset) const
	{
		assert(HasAttribute(Offset, sizeof(T)) && "GetAttribute<T>: invalid vertex attribute layout");
		return *reinterpret_cast<const T*>(
			static_cast<const uint8*>(VertexData) + Index * Stride + Offset);
	}

	const FVector& GetPosition(uint32 Index) const
	{
		return GetAttribute<FVector>(Index, PositionOffset);
	}

	FVector GetNormal(uint32 Index, const FVector& Fallback = FVector::UpVector) const
	{
		return HasNormal() ? GetAttribute<FVector>(Index, NormalOffset) : Fallback;
	}

	FVector4 GetColor(uint32 Index, const FVector4& Fallback = FVector4(1.0f, 1.0f, 1.0f, 1.0f)) const
	{
		return HasColor() ? GetAttribute<FVector4>(Index, ColorOffset) : Fallback;
	}

	FVector2 GetUV(uint32 Index, const FVector2& Fallback = FVector2()) const
	{
		return HasUV() ? GetAttribute<FVector2>(Index, UVOffset) : Fallback;
	}

	FVector4 GetTangent(uint32 Index, const FVector4& Fallback = FVector4(1.0f, 0.0f, 0.0f, 1.0f)) const
	{
		return HasTangent() ? GetAttribute<FVector4>(Index, TangentOffset) : Fallback;
	}

	// N번째 삼각형의 세 정점 인덱스를 반환
	void GetTriangleIndices(uint32 TriIndex, uint32& OutI0, uint32& OutI1, uint32& OutI2) const
	{
		assert(TriIndex * 3 + 2 < IndexCount && "GetTriangleIndices: TriIndex out of range");
		OutI0 = IndexData[TriIndex * 3];
		OutI1 = IndexData[TriIndex * 3 + 1];
		OutI2 = IndexData[TriIndex * 3 + 2];
	}

	template<typename VertexType>
	static FMeshDataView FromMeshData(const TMeshData<VertexType>& Data)
	{
		FMeshDataView View;
		if (!Data.Vertices.empty())
		{
			View.VertexData  = Data.Vertices.data();
			View.VertexCount = (uint32)Data.Vertices.size();
			View.Stride      = sizeof(VertexType);
		}
		if (!Data.Indices.empty())
		{
			View.IndexData  = Data.Indices.data();
			View.IndexCount = (uint32)Data.Indices.size();
		}
		return View;
	}
};
