// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Spawners/EnaceItemSpawner.h"
#include "EnaceDispatch.h"
#include "EnaceModule.h"
#include "Items/EnaceItemDefinition.h"
#include "Components/StaticMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnaceItemSpawner)

AEnaceItemSpawner::AEnaceItemSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Preview mesh (editor only, hidden at runtime)
	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMeshComponent->SetupAttachment(RootComponent);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->bHiddenInGame = true;
	PreviewMeshComponent->CastShadow = false;

#if WITH_EDITORONLY_DATA
	// Make it slightly transparent in editor
	PreviewMeshComponent->SetMaterial(0, nullptr);
#endif
}

void AEnaceItemSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Hide preview at runtime
	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetVisibility(false);
	}

	// Spawn the item
	ItemKey = DoSpawn();

	// Optionally destroy spawner
	if (bDestroyAfterSpawn && ItemKey.IsValid())
	{
		Destroy();
	}
}

FSkeletonKey AEnaceItemSpawner::DoSpawn()
{
	if (!ItemDefinition)
	{
		UE_LOG(LogEnace, Warning, TEXT("EnaceItemSpawner '%s': No ItemDefinition set!"), *GetName());
		return FSkeletonKey();
	}

	UEnaceDispatch* Enace = UEnaceDispatch::Get(GetWorld());
	if (!Enace)
	{
		UE_LOG(LogEnace, Warning, TEXT("EnaceItemSpawner '%s': EnaceDispatch not available"), *GetName());
		return FSkeletonKey();
	}

	// Spawn via EnaceDispatch (handles physics + render + data registration)
	FSkeletonKey Key = Enace->SpawnWorldItem(
		ItemDefinition,
		GetActorLocation(),
		Count,
		InitialVelocity
	);

	if (!Key.IsValid())
	{
		UE_LOG(LogEnace, Warning, TEXT("EnaceItemSpawner '%s': Failed to spawn item"), *GetName());
		return FSkeletonKey();
	}

	// Override despawn time if specified
	if (DespawnTimeOverride >= 0.f)
	{
		FEnaceItemData ItemData;
		if (Enace->TryGetItemData(Key, ItemData))
		{
			ItemData.DespawnTimer = DespawnTimeOverride;
			// Note: Would need a SetItemData method to update this
			// For now, the despawn timer is set at spawn time via ItemDefinition
		}
	}

	UE_LOG(LogEnace, Log, TEXT("EnaceItemSpawner '%s': Spawned '%s' x%d (Key: %llu)"),
		*GetName(),
		*ItemDefinition->ItemId.ToString(),
		Count,
		(uint64)Key);

	return Key;
}

FSkeletonKey AEnaceItemSpawner::SpawnItem(
	UObject* WorldContextObject,
	UEnaceItemDefinition* Definition,
	FVector Location,
	int32 InCount,
	FVector InVelocity)
{
	if (!WorldContextObject || !Definition)
	{
		return FSkeletonKey();
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return FSkeletonKey();
	}

	UEnaceDispatch* Enace = UEnaceDispatch::Get(World);
	if (!Enace)
	{
		return FSkeletonKey();
	}

	return Enace->SpawnWorldItem(Definition, Location, InCount, InVelocity);
}

#if WITH_EDITOR
void AEnaceItemSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

void AEnaceItemSpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdatePreview();
}

void AEnaceItemSpawner::UpdatePreview()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	if (!bShowPreview || !ItemDefinition || !ItemDefinition->WorldMesh)
	{
		PreviewMeshComponent->SetVisibility(false);
		return;
	}

	PreviewMeshComponent->SetStaticMesh(ItemDefinition->WorldMesh);
	PreviewMeshComponent->SetWorldScale3D(ItemDefinition->WorldMeshScale);
	PreviewMeshComponent->SetVisibility(true);

	// Tint by rarity color for visual feedback
	if (ItemDefinition->RarityColor != FLinearColor::White)
	{
		UMaterialInstanceDynamic* DynMat = PreviewMeshComponent->CreateDynamicMaterialInstance(0);
		if (DynMat)
		{
			DynMat->SetVectorParameterValue(TEXT("Color"), ItemDefinition->RarityColor);
		}
	}
}
#endif
