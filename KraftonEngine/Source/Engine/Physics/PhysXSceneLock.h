#pragma once

#include <PxPhysicsAPI.h>

#define KRAFTON_CONCAT_IMPL(A, B) A##B
#define KRAFTON_CONCAT(A, B) KRAFTON_CONCAT_IMPL(A, B)

// PhysX scene access policy:
// - Use a write lock for actor/shape/joint creation or mutation, force/velocity writes,
//   and simulate/fetchResults.
// - Use a read lock for pose/stat/debug/query reads.
// - Copy PhysX data while locked, then update engine objects after the lock is released.
#define PHYSX_SCENE_READ_LOCK(ScenePtr) \
	physx::PxSceneReadLock KRAFTON_CONCAT(ScopedPhysXReadLock_, __LINE__)(*(ScenePtr))

#define PHYSX_SCENE_WRITE_LOCK(ScenePtr) \
	physx::PxSceneWriteLock KRAFTON_CONCAT(ScopedPhysXWriteLock_, __LINE__)(*(ScenePtr))
