#include "Mesh/Static/StaticMesh.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Mesh/MeshManager.h"
#include "Serialization/WindowsArchive.h"
#include "Mesh/Importer/ObjImporter.h"
#include "Texture/Texture2D.h"
#include "Engine/Profiling/Stats/MemoryStats.h"
#include "Mesh/MeshSimplifier.h"
#include "Physics/Asset/BodySetup.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float StaticMeshCollisionPi = 3.14159265358979323846f;
	constexpr const char* StaticMeshBodyName = "StaticMesh";
	constexpr uint32 StaticMeshCollisionMagic = 0x434D534B; // KSMC
	constexpr uint32 StaticMeshCollisionVersion = 2;

	FVector GetAxisVector(int32 Axis)
	{
		if (Axis == 1) return FVector(0.0f, 1.0f, 0.0f);
		if (Axis == 2) return FVector(0.0f, 0.0f, 1.0f);
		return FVector(1.0f, 0.0f, 0.0f);
	}

	float GetAxisValue(const FVector& V, int32 Axis)
	{
		if (Axis == 1) return V.Y;
		if (Axis == 2) return V.Z;
		return V.X;
	}

	FQuat MakeRotationFromCapsuleXAxisToAxis(int32 Axis)
	{
		if (Axis == 1)
		{
			return FQuat::FromAxisAngle(FVector(0.0f, 0.0f, 1.0f), StaticMeshCollisionPi * 0.5f);
		}
		if (Axis == 2)
		{
			return FQuat::FromAxisAngle(FVector(0.0f, 1.0f, 0.0f), -StaticMeshCollisionPi * 0.5f);
		}
		return FQuat::Identity;
	}

	void ResetBodyForStaticMesh(UBodySetup* Body)
	{
		if (!Body) return;

		Body->BoneName = StaticMeshBodyName;
		Body->AggregateGeom.SphereElems.clear();
		Body->AggregateGeom.BoxElems.clear();
		Body->AggregateGeom.CapsuleElems.clear();
		Body->bSimulatePhysics = false;
		Body->bEnableGravity = false;
		Body->PhysicsType = EBodyPhysicsType::Kinematic;
		Body->CollisionEnabled = EBodyCollisionEnabled::QueryAndPhysics;
	}

	void SerializeStaticMeshCollision(FArchive& Ar, UBodySetup*& BodyInstance, EStaticMeshCollisionMode& CollisionMode)
	{
		if (Ar.IsSaving())
		{
			uint32 Magic = StaticMeshCollisionMagic;
			uint32 Version = StaticMeshCollisionVersion;
			uint8 Mode = static_cast<uint8>(CollisionMode);
			bool bHasBodySetup = BodyInstance != nullptr && !BodyInstance->AggregateGeom.IsEmpty();
			Ar << Magic;
			Ar << Version;
			Ar << Mode;
			Ar << bHasBodySetup;
			if (bHasBodySetup)
			{
				BodyInstance->Serialize(Ar);
			}
			return;
		}

		if (!Ar.IsLoading() || Ar.RemainingBytes() < sizeof(uint32) * 2 + sizeof(bool))
		{
			return;
		}

		uint32 Magic = 0;
		uint32 Version = 0;
		Ar << Magic;
		Ar << Version;

		if (Magic != StaticMeshCollisionMagic || Version > StaticMeshCollisionVersion)
		{
			return;
		}

		uint8 Mode = static_cast<uint8>(EStaticMeshCollisionMode::None);
		bool bHasBodySetup = false;
		if (Version >= 2)
		{
			Ar << Mode;
			Ar << bHasBodySetup;
		}
		else
		{
			Ar << bHasBodySetup;
			Mode = bHasBodySetup ? static_cast<uint8>(EStaticMeshCollisionMode::Simple) : static_cast<uint8>(EStaticMeshCollisionMode::None);
		}

		delete BodyInstance;
		BodyInstance = nullptr;
		if (Mode > static_cast<uint8>(EStaticMeshCollisionMode::TriangleMesh))
		{
			Mode = static_cast<uint8>(EStaticMeshCollisionMode::None);
		}
		CollisionMode = static_cast<EStaticMeshCollisionMode>(Mode);

		if (bHasBodySetup)
		{
			BodyInstance = new UBodySetup();
			BodyInstance->Serialize(Ar);
			if (BodyInstance->BoneName.empty())
			{
				BodyInstance->BoneName = StaticMeshBodyName;
			}
		}

		if (CollisionMode == EStaticMeshCollisionMode::Simple && (!BodyInstance || BodyInstance->AggregateGeom.IsEmpty()))
		{
			CollisionMode = EStaticMeshCollisionMode::None;
		}
	}
}

UStaticMesh::~UStaticMesh()
{
	if (StaticMeshAsset)
	{
		const uint32 CPUSize =
			static_cast<uint32>(StaticMeshAsset->Vertices.size() * sizeof(FNormalVertex)) +
			static_cast<uint32>(StaticMeshAsset->Indices.size() * sizeof(uint32));

		MemoryStats::SubStaticMeshCPUMemory(CPUSize);
	}

	delete BodyInstance;
	BodyInstance = nullptr;
}

void UStaticMesh::Serialize(FArchive& Ar)
{
	// 에셋이 비어있으면 로드용으로 생성
	if (Ar.IsLoading() && !StaticMeshAsset)
	{
		StaticMeshAsset = new FStaticMesh();
	}

	// 1. 지오메트리 데이터 직렬화
	StaticMeshAsset->Serialize(Ar);

	// 2. 머티리얼 데이터 직렬화 (필수!)
	Ar << StaticMaterials;

	// 2.5. 스태틱 메시 단순 콜리전 데이터.
	// 구버전 asset에는 이 블록이 없으므로 loading 시 남은 바이트가 없으면 건너뜁니다.
	SerializeStaticMeshCollision(Ar, BodyInstance, CollisionMode);

	// 3. 로딩 시 Section → MaterialIndex 매핑 캐싱 (매 프레임 문자열 비교 방지)
	if (Ar.IsLoading())
	{
		for (FStaticMeshSection& Section : StaticMeshAsset->Sections)
		{
			Section.MaterialIndex = -1;
			for (int32 i = 0; i < (int32)StaticMaterials.size(); ++i)
			{
				if (StaticMaterials[i].MaterialSlotName == Section.MaterialSlotName)
				{
					Section.MaterialIndex = i;
					break;
				}
			}
		}
	}
}

void UStaticMesh::InitResources(ID3D11Device* InDevice)
{
	if (!InDevice || !StaticMeshAsset) return;

	// CPU 메모리 추적
	const uint32 CPUSize =
		static_cast<uint32>(StaticMeshAsset->Vertices.size() * sizeof(FNormalVertex)) +
		static_cast<uint32>(StaticMeshAsset->Indices.size() * sizeof(uint32));
	MemoryStats::AddStaticMeshCPUMemory(CPUSize);

	// CPU → GPU 정점 버퍼 변환
	TMeshData<FVertexPNCTT> RenderMeshData;
	RenderMeshData.Vertices.reserve(StaticMeshAsset->Vertices.size());

	for (const FNormalVertex& RawVert : StaticMeshAsset->Vertices)
	{
		FVertexPNCTT RenderVert;
		RenderVert.Position = RawVert.pos;
		RenderVert.Normal = RawVert.normal;
		RenderVert.Color = RawVert.color;
		RenderVert.UV = RawVert.tex;
		RenderVert.Tangent = RawVert.tangent;
		RenderMeshData.Vertices.push_back(RenderVert);
	}
	RenderMeshData.Indices = StaticMeshAsset->Indices;

	StaticMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();
	StaticMeshAsset->RenderBuffer->Create(InDevice, RenderMeshData);

	// ── LOD 생성 (LOD1: 90%, LOD2: 55%, LOD3: 15%) ──
	/*if (StaticMeshAsset->Vertices.size() >= 100)
	{
		static const float LODRatios[] = { 0.9f, 0.55f, 0.15f };
		for (int lod = 0; lod < 3; ++lod)
		{
			FSimplifiedMesh Simplified = FMeshSimplifier::Simplify(
				StaticMeshAsset->Vertices, StaticMeshAsset->Indices,
				StaticMeshAsset->Sections, LODRatios[lod]);

			AdditionalLODs[lod].Sections = std::move(Simplified.Sections);

			TMeshData<FVertexPNCT> LODRenderData;
			LODRenderData.Vertices.reserve(Simplified.Vertices.size());
			for (const FNormalVertex& RawVert : Simplified.Vertices)
			{
				FVertexPNCT RenderVert;
				RenderVert.Position = RawVert.pos;
				RenderVert.Normal = RawVert.normal;
				RenderVert.Color = RawVert.color;
				RenderVert.UV = RawVert.tex;
				LODRenderData.Vertices.push_back(RenderVert);
			}
			LODRenderData.Indices = std::move(Simplified.Indices);

			AdditionalLODs[lod].RenderBuffer = std::make_unique<FMeshBuffer>();
			AdditionalLODs[lod].RenderBuffer->Create(InDevice, LODRenderData);
		}
		bHasLOD = true;
	}*/
}

void UStaticMesh::SetStaticMeshAsset(FStaticMesh* InMesh)
{
	StaticMeshAsset = InMesh;
	// 현재는 static mesh asset이 로드 후 고정된다고 보고, 메시 변경 dirty 갱신은 비활성화합니다.
	// MarkMeshTrianglePickingBVHDirty();

	// Section → MaterialIndex 캐싱 갱신
	if (StaticMeshAsset)
	{
		for (FStaticMeshSection& Section : StaticMeshAsset->Sections)
		{
			Section.MaterialIndex = -1;
			for (int32 i = 0; i < (int32)StaticMaterials.size(); ++i)
			{
				if (StaticMaterials[i].MaterialSlotName == Section.MaterialSlotName)
				{
					Section.MaterialIndex = i;
					break;
				}
			}
		}
		EnsureMeshTrianglePickingBVHBuilt();
	}
}

FStaticMesh* UStaticMesh::GetStaticMeshAsset() const
{
	return StaticMeshAsset;
}

void UStaticMesh::SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials)
{
	StaticMaterials = InMaterials;
}

const TArray<FStaticMaterial>& UStaticMesh::GetStaticMaterials() const
{
	return StaticMaterials;
}

void UStaticMesh::EnsureMeshTrianglePickingBVHBuilt() const
{
	if (!StaticMeshAsset)
	{
		return;
	}

	MeshTrianglePickingBVH.EnsureBuilt(*StaticMeshAsset);
}

bool UStaticMesh::RaycastMeshTrianglesWithBVHLocal(const FVector& LocalOrigin, const FVector& LocalDirection, FHitResult& OutHitResult) const
{
	if (!StaticMeshAsset)
	{
		return false;
	}

	EnsureMeshTrianglePickingBVHBuilt();
	return MeshTrianglePickingBVH.RaycastLocal(LocalOrigin, LocalDirection, *StaticMeshAsset, OutHitResult);
}

FMeshBuffer* UStaticMesh::GetLODMeshBuffer(uint32 LODLevel) const
{
	if (LODLevel == 0 && StaticMeshAsset)
		return StaticMeshAsset->RenderBuffer.get();
	if (LODLevel >= 1 && LODLevel <= 3 && bHasLOD)
		return AdditionalLODs[LODLevel - 1].RenderBuffer.get();
	return StaticMeshAsset ? StaticMeshAsset->RenderBuffer.get() : nullptr;
}

static const TArray<FStaticMeshSection> EmptySections;

const TArray<FStaticMeshSection>& UStaticMesh::GetLODSections(uint32 LODLevel) const
{
	if (LODLevel == 0 && StaticMeshAsset)
		return StaticMeshAsset->Sections;
	if (LODLevel >= 1 && LODLevel <= 3 && bHasLOD)
		return AdditionalLODs[LODLevel - 1].Sections;
	return StaticMeshAsset ? StaticMeshAsset->Sections : EmptySections;
}

UBodySetup* UStaticMesh::GetOrCreateBodySetup()
{
	if (!BodyInstance)
	{
		BodyInstance = new UBodySetup();
		ResetBodyForStaticMesh(BodyInstance);
	}
	return BodyInstance;
}

void UStaticMesh::ClearBodySetup()
{
	delete BodyInstance;
	BodyInstance = nullptr;
	if (CollisionMode == EStaticMeshCollisionMode::Simple)
	{
		CollisionMode = EStaticMeshCollisionMode::None;
	}
}

void UStaticMesh::SetCollisionMode(EStaticMeshCollisionMode InMode)
{
	CollisionMode = InMode;
}

bool UStaticMesh::GenerateSimpleCollision(EStaticMeshSimpleCollisionShape ShapeType)
{
	if (!StaticMeshAsset || StaticMeshAsset->Vertices.empty())
	{
		return false;
	}

	if (!StaticMeshAsset->bBoundsValid)
	{
		StaticMeshAsset->CacheBounds();
	}
	if (!StaticMeshAsset->bBoundsValid)
	{
		return false;
	}

	UBodySetup* Body = GetOrCreateBodySetup();
	ResetBodyForStaticMesh(Body);

	const FVector Center = StaticMeshAsset->BoundsCenter;
	const FVector Extent = StaticMeshAsset->BoundsExtent;

	switch (ShapeType)
	{
	case EStaticMeshSimpleCollisionShape::Box:
	{
		FKBoxElem Box;
		Box.Center = Center;
		Box.Rotation = FQuat::Identity;
		Box.HalfX = (std::max)(Extent.X, 0.001f);
		Box.HalfY = (std::max)(Extent.Y, 0.001f);
		Box.HalfZ = (std::max)(Extent.Z, 0.001f);
		Body->AggregateGeom.BoxElems.push_back(Box);
		CollisionMode = EStaticMeshCollisionMode::Simple;
		return true;
	}
	case EStaticMeshSimpleCollisionShape::Sphere:
	{
		float RadiusSq = 0.0f;
		for (const FNormalVertex& Vertex : StaticMeshAsset->Vertices)
		{
			const FVector Delta = Vertex.pos - Center;
			RadiusSq = (std::max)(RadiusSq, Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z);
		}

		FKSphereElem Sphere;
		Sphere.Center = Center;
		Sphere.Radius = (std::max)(std::sqrt(RadiusSq), 0.001f);
		Body->AggregateGeom.SphereElems.push_back(Sphere);
		CollisionMode = EStaticMeshCollisionMode::Simple;
		return true;
	}
	case EStaticMeshSimpleCollisionShape::Capsule:
	{
		int32 Axis = 0;
		if (Extent.Y > Extent.X && Extent.Y >= Extent.Z) Axis = 1;
		else if (Extent.Z > Extent.X && Extent.Z > Extent.Y) Axis = 2;

		float MinAxis = GetAxisValue(StaticMeshAsset->Vertices[0].pos, Axis);
		float MaxAxis = MinAxis;
		float RadiusSq = 0.0f;
		const FVector AxisVector = GetAxisVector(Axis);
		for (const FNormalVertex& Vertex : StaticMeshAsset->Vertices)
		{
			const float AxisValue = GetAxisValue(Vertex.pos, Axis);
			MinAxis = (std::min)(MinAxis, AxisValue);
			MaxAxis = (std::max)(MaxAxis, AxisValue);

			const FVector AxialPoint = Center + AxisVector * (AxisValue - GetAxisValue(Center, Axis));
			const FVector Perp = Vertex.pos - AxialPoint;
			RadiusSq = (std::max)(RadiusSq, Perp.X * Perp.X + Perp.Y * Perp.Y + Perp.Z * Perp.Z);
		}

		const float Radius = (std::max)(std::sqrt(RadiusSq), 0.001f);
		const float HalfAxisLength = (std::max)((MaxAxis - MinAxis) * 0.5f, Radius);
		FKCapsuleElem Capsule;
		Capsule.Center = Center;
		Capsule.Rotation = MakeRotationFromCapsuleXAxisToAxis(Axis);
		Capsule.Radius = Radius;
		Capsule.HalfHeight = HalfAxisLength;
		Body->AggregateGeom.CapsuleElems.push_back(Capsule);
		CollisionMode = EStaticMeshCollisionMode::Simple;
		return true;
	}
	default:
		return false;
	}
}

bool UStaticMesh::GenerateTriangleMeshCollision()
{
	if (!StaticMeshAsset || StaticMeshAsset->Vertices.empty() || StaticMeshAsset->Indices.size() < 3 || StaticMeshAsset->Indices.size() % 3 != 0)
	{
		return false;
	}

	CollisionMode = EStaticMeshCollisionMode::TriangleMesh;
	return true;
}

bool UStaticMesh::HasTriangleMeshCollision() const
{
	return CollisionMode == EStaticMeshCollisionMode::TriangleMesh
		&& StaticMeshAsset
		&& !StaticMeshAsset->Vertices.empty()
		&& StaticMeshAsset->Indices.size() >= 3
		&& StaticMeshAsset->Indices.size() % 3 == 0;
}
