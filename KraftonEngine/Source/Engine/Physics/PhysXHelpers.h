#pragma once

#include <PxPhysicsAPI.h>
using namespace physx;

namespace PhysXHelpers
{
	inline PxVec3 ToPxVec3(const FVector& V)
	{
		return PxVec3(V.X, V.Y, V.Z);
	}

	inline PxQuat ToPxQuat(const FQuat& Q)
	{
		return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
	}

	inline FVector ToFVector(const PxVec3& V)
	{
		return FVector(V.x, V.y, V.z);
	}

	inline FQuat ToFQuat(const PxQuat& Q)
	{
		return FQuat(Q.x, Q.y, Q.z, Q.w);
	}

	inline PxTransform ToPxTransform(const FTransform& Transform)
	{
		return PxTransform(ToPxVec3(Transform.Location), ToPxQuat(Transform.Rotation));
	}

	inline FTransform ToFTransform(const PxTransform& Transform)
	{
		return FTransform(ToFVector(Transform.p), ToFQuat(Transform.q), FVector(1.0f, 1.0f, 1.0f));
	}

	inline PxD6Motion::Enum ToPxD6Motion(EPhysicsMotionType Motion)
	{
		switch (Motion)
		{
		case EPhysicsMotionType::Free:
			return PxD6Motion::eFREE;
		case EPhysicsMotionType::Limited:
			return PxD6Motion::eLIMITED;
		case EPhysicsMotionType::Locked:
		default:
			return PxD6Motion::eLOCKED;
		}
	}

	inline PxRigidActor* GetPxActor(FBodyInstance* Body)
	{
		return Body ? static_cast<PxRigidActor*>(Body->ActorHandle.NativePtr) : nullptr;
	}

	inline const PxRigidActor* GetPxActor(const FBodyInstance* Body)
	{
		return Body ? static_cast<const PxRigidActor*>(Body->ActorHandle.NativePtr) : nullptr;
	}

	inline PxRigidDynamic* GetPxDynamic(FBodyInstance* Body)
	{
		PxRigidActor* Actor = GetPxActor(Body);
		return Actor ? Actor->is<PxRigidDynamic>() : nullptr;
	}

	inline bool BuildGeometry(const FPhysicsShapeDesc& Desc, PxGeometryHolder& OutGeometry)
	{
		switch (Desc.ShapeType)
		{
		case EPhysicsShapeType::Box:
			OutGeometry = PxBoxGeometry(Desc.HalfExtent.X, Desc.HalfExtent.Y, Desc.HalfExtent.Z);
			return true;
		case EPhysicsShapeType::Sphere:
			OutGeometry = PxSphereGeometry(Desc.Radius);
			return true;
		case EPhysicsShapeType::Capsule:
			OutGeometry = PxCapsuleGeometry(Desc.Radius, Desc.HalfHeight);
			return true;
		case EPhysicsShapeType::Convex:
		case EPhysicsShapeType::TriangleMesh:
		default:
			return false;
		}
	}

	inline FVector DecodeDebugColor(PxU32 Color)
	{
		const float R = static_cast<float>((Color >> 16) & 0xff) / 255.0f;
		const float G = static_cast<float>((Color >> 8) & 0xff) / 255.0f;
		const float B = static_cast<float>(Color & 0xff) / 255.0f;
		return FVector(R, G, B);
	}
}
