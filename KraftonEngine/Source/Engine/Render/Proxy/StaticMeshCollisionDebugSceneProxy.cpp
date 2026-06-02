#include "StaticMeshCollisionDebugSceneProxy.h"

#include "Component/Debug/StaticMeshCollisionDebugComponent.h"
#include "Mesh/Static/StaticMesh.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsGeometry.h"

namespace
{
	FVector4 SolidColor() { return FVector4(0.10f, 0.55f, 1.00f, 0.22f); }
	FVector4 WireColor()  { return FVector4(0.20f, 0.65f, 1.00f, 1.00f); }
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
