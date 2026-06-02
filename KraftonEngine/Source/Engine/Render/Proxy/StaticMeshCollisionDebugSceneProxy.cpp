#include "StaticMeshCollisionDebugSceneProxy.h"

#include "Component/Debug/StaticMeshCollisionDebugComponent.h"
#include "Mesh/Static/StaticMesh.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsGeometry.h"

namespace
{
	FVector4 SolidColor() { return FVector4(0.10f, 0.55f, 1.00f, 0.22f); }
	FVector4 WireColor()  { return FVector4(0.20f, 0.65f, 1.00f, 1.00f); }

	void AppendTriangleMeshWire(TArray<FColoredLine>& Out, const FStaticMesh& Mesh, const FVector4& Color)
	{
		if (Mesh.Vertices.empty() || Mesh.Indices.size() < 3 || Mesh.Indices.size() % 3 != 0)
		{
			return;
		}

		Out.reserve(Out.size() + Mesh.Indices.size());
		for (size_t Index = 0; Index + 2 < Mesh.Indices.size(); Index += 3)
		{
			const uint32 I0 = Mesh.Indices[Index];
			const uint32 I1 = Mesh.Indices[Index + 1];
			const uint32 I2 = Mesh.Indices[Index + 2];
			if (I0 >= Mesh.Vertices.size() || I1 >= Mesh.Vertices.size() || I2 >= Mesh.Vertices.size())
			{
				continue;
			}

			const FVector& V0 = Mesh.Vertices[I0].pos;
			const FVector& V1 = Mesh.Vertices[I1].pos;
			const FVector& V2 = Mesh.Vertices[I2].pos;
			ShapeDebugUtils::PushLine(Out, V0, V1, Color);
			ShapeDebugUtils::PushLine(Out, V1, V2, Color);
			ShapeDebugUtils::PushLine(Out, V2, V0, Color);
		}
	}
}

FStaticMeshCollisionDebugSceneProxy::FStaticMeshCollisionDebugSceneProxy(
	UStaticMeshCollisionDebugComponent* InComponent)
	: FPhysicsShapeDebugSceneProxy(static_cast<UPrimitiveComponent*>(InComponent), false)
{
	RebuildGeometry();
}

FStaticMeshCollisionDebugSceneProxy::~FStaticMeshCollisionDebugSceneProxy()
{
}

void FStaticMeshCollisionDebugSceneProxy::RebuildGeometry()
{
	CachedSolid.clear();
	CachedWire.clear();

	UStaticMeshCollisionDebugComponent* Comp =
		static_cast<UStaticMeshCollisionDebugComponent*>(GetOwner());
	if (!Comp) return;

	UStaticMesh* Mesh = Comp->GetStaticMesh();
	if (!Mesh) return;

	if (Mesh->HasTriangleMeshCollision())
	{
		if (const FStaticMesh* MeshAsset = Mesh->GetStaticMeshAsset())
		{
			AppendTriangleMeshWire(CachedWire, *MeshAsset, WireColor());
		}
	}

	UBodySetup* Body = Mesh->GetBodySetup();
	if (!Body) return;

	for (const FKBoxElem& E : Body->AggregateGeom.BoxElems)
	{
		ShapeDebugUtils::AppendSolidBox(CachedSolid, E.Center, E.Rotation,
			E.HalfX, E.HalfY, E.HalfZ, SolidColor());
		ShapeDebugUtils::AppendWireBox(CachedWire, E.Center, E.Rotation,
			E.HalfX, E.HalfY, E.HalfZ, WireColor());
	}

	for (const FKSphereElem& E : Body->AggregateGeom.SphereElems)
	{
		ShapeDebugUtils::AppendSolidSphere(CachedSolid, E.Center, E.Radius, SolidColor());
		ShapeDebugUtils::AppendWireSphere (CachedWire,  E.Center, E.Radius, WireColor());
	}

	for (const FKCapsuleElem& E : Body->AggregateGeom.CapsuleElems)
	{
		ShapeDebugUtils::AppendSolidCapsule(CachedSolid, E.Center, E.Rotation,
			E.Radius, E.HalfHeight, SolidColor());
		ShapeDebugUtils::AppendWireCapsule(CachedWire, E.Center, E.Rotation,
			E.Radius, E.HalfHeight, WireColor());
	}
}
