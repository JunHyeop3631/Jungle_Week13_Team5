// Copyright Epic Games, Inc. All Rights Reserved.
#include "ShapeComponent.h"
#include "GameFramework/World.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Physics/Cloth/IClothScene.h"
#include "Serialization/Archive.h"
#include "Render/Proxy/ShapeSceneProxy.h"

#include <cstring>

HIDE_FROM_COMPONENT_LIST(UShapeComponent)

UShapeComponent::UShapeComponent()
{
	bCastShadow = false;
}

void UShapeComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();

	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			if (IClothScene* Scene = World->GetClothScene())
			{
				Scene->RegisterShapeCollider(this);
			}
		}
	}
}

void UShapeComponent::EndPlay()
{
	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			if (IClothScene* Scene = World->GetClothScene())
			{
				Scene->UnregisterShapeCollider(this);
			}
		}
	}

	UPrimitiveComponent::EndPlay();
}

FPrimitiveSceneProxy* UShapeComponent::CreateSceneProxy()
{
	return new FShapeSceneProxy(this);
}

void UShapeComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "ShapeColor") == 0 || strcmp(PropertyName, "bDrawOnlyIfSelected") == 0
		|| strcmp(PropertyName, "Shape Color") == 0 || strcmp(PropertyName, "Draw Only If Selected") == 0)
	{
		MarkRenderStateDirty();
	}
}
