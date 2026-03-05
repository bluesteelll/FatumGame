// Dev/test scaffolding for AFlecsCharacter.
// Entity spawning, container testing — intended for development/debugging only.

#include "FlecsCharacter.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "FlecsContainerLibrary.h"
#include "Engine/Engine.h"

// ═══════════════════════════════════════════════════════════════════════════
// ENTITY SPAWNING
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::SpawnTestEntity()
{
	if (!TestEntityDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::SpawnTestEntity - No TestEntityDefinition set!"));
		return FSkeletonKey();
	}

	// Calculate spawn location in front of character
	FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * SpawnDistance;
	SpawnLocation.Z += 50.f;  // Lift slightly above ground

	FSkeletonKey Key = UFlecsEntityLibrary::SpawnEntityFromDefinition(
		this,
		TestEntityDefinition,
		SpawnLocation,
		GetActorRotation()
	);

	if (Key.IsValid())
	{
		SpawnedEntityKeys.Add(Key);
		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Spawned entity Key=%llu, Total=%d"),
			static_cast<uint64>(Key), SpawnedEntityKeys.Num());
	}

	return Key;
}

void AFlecsCharacter::DestroyLastSpawnedEntity()
{
	if (SpawnedEntityKeys.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::DestroyLastSpawnedEntity - No entities to destroy!"));
		return;
	}

	FSkeletonKey Key = SpawnedEntityKeys.Pop();
	UFlecsEntityLibrary::DestroyEntity(this, Key);

	UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Destroyed entity Key=%llu, Remaining=%d"),
		static_cast<uint64>(Key), SpawnedEntityKeys.Num());
}

// ═══════════════════════════════════════════════════════════════════════════
// CONTAINER TESTING
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::SpawnTestContainer()
{
	if (!TestContainerDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::SpawnTestContainer - No TestContainerDefinition set!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No TestContainerDefinition set!"));
		}
		return FSkeletonKey();
	}

	// Spawn container in front of character
	FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * SpawnDistance;
	SpawnLocation.Z += 50.f;

	TestContainerKey = UFlecsEntityLibrary::SpawnEntityFromDefinition(
		this,
		TestContainerDefinition,
		SpawnLocation,
		GetActorRotation()
	);

	if (TestContainerKey.IsValid())
	{
		FString Message = FString::Printf(TEXT("Container spawned: %llu"), static_cast<uint64>(TestContainerKey));
		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: %s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Message);
		}
	}
	else
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("Failed to spawn container!"));
		}
	}

	return TestContainerKey;
}

bool AFlecsCharacter::AddItemToTestContainer()
{
	if (!TestContainerKey.IsValid())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No container to add item to!"));
		}
		return false;
	}

	if (!TestItemDefinition)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No TestItemDefinition set!"));
		}
		return false;
	}

	if (!TestItemDefinition->ItemDefinition)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("TestItemDefinition has no ItemDefinition profile!"));
		}
		return false;
	}

	int32 ActuallyAdded = 0;
	// Resolve SkeletonKey → int64 for new container API
	int64 ContainerId = UFlecsEntityLibrary::GetEntityId(this, TestContainerKey);
	bool bSuccess = UFlecsContainerLibrary::AddItemToContainer(
		this,
		ContainerId,
		TestItemDefinition,
		1,  // Add 1 item
		ActuallyAdded,
		false  // bAutoStack = false for testing
	);

	// Get current count for display
	int32 ItemCount = UFlecsContainerLibrary::GetContainerItemCount(this, ContainerId);

	FString ItemName = TestItemDefinition->ItemDefinition->ItemName.ToString();
	FString Message = FString::Printf(TEXT("Added item: %s (Container now has %d items) [Prefab]"), *ItemName, ItemCount + 1);
	UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: %s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, Message);
	}

	return bSuccess;
}

void AFlecsCharacter::RemoveAllItemsFromTestContainer()
{
	if (!TestContainerKey.IsValid())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No container!"));
		}
		return;
	}

	// Get count before removal for display
	int64 ContainerId = UFlecsEntityLibrary::GetEntityId(this, TestContainerKey);
	int32 ItemCount = UFlecsContainerLibrary::GetContainerItemCount(this, ContainerId);

	// Remove all items
	UFlecsContainerLibrary::RemoveAllItemsFromContainer(this, ContainerId);

	FString Message = FString::Printf(TEXT("Removed all items from container (%d items removed)"), ItemCount);
	UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: %s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, Message);
	}
}
