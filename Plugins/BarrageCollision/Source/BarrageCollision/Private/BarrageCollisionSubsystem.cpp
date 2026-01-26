// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Barrage Collision Subsystem - Event listener and Blueprint API

#include "BarrageCollisionSubsystem.h"
#include "BarrageCollisionModule.h"
#include "BarrageDispatch.h"
#include "BarrageContactEvent.h"
#include "FBarragePrimitive.h"
#include "Systems/ArtilleryDispatch.h"
#include "Systems/BarrageEntitySpawner.h"

// ═══════════════════════════════════════════════════════════════════════════════
// STATIC ACCESS
// ═══════════════════════════════════════════════════════════════════════════════

UBarrageCollisionSubsystem* UBarrageCollisionSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UBarrageCollisionSubsystem>() : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollisionSubsystem: Initializing..."));

	// Configure dispatcher
	Dispatcher.SetName(TEXT("BarrageCollision"));
	Dispatcher.SetTagLookupFunction(&UBarrageCollisionSubsystem::LookupEntityTag);

	// Subscribe to Barrage collision events
	if (UBarrageDispatch* BarrageDispatch = InWorld.GetSubsystem<UBarrageDispatch>())
	{
		ContactEventHandle = BarrageDispatch->OnBarrageContactAddedDelegate.AddUObject(
			this, &UBarrageCollisionSubsystem::OnBarrageContact
		);
		UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollisionSubsystem: Subscribed to Barrage contact events"));
	}
	else
	{
		UE_LOG(LogBarrageCollision, Warning, TEXT("BarrageCollisionSubsystem: UBarrageDispatch not found - collision events will not be processed"));
	}

	// Register default handlers
	RegisterDefaultHandlers();

	UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollisionSubsystem: Online (types: %d, tags: %d)"),
		Dispatcher.GetTypeHandlerCount(), Dispatcher.GetTagHandlerCount());
}

void UBarrageCollisionSubsystem::Deinitialize()
{
	UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollisionSubsystem: Deinitializing..."));

	// Unsubscribe from Barrage events
	if (UBarrageDispatch::SelfPtr && ContactEventHandle.IsValid())
	{
		UBarrageDispatch::SelfPtr->OnBarrageContactAddedDelegate.Remove(ContactEventHandle);
		ContactEventHandle.Reset();
	}

	// Log stats before cleanup
	const int32 TypeCount = Dispatcher.GetTypeHandlerCount();
	const int32 TagCount = Dispatcher.GetTagHandlerCount();

	// Reset dispatcher (clears handlers and cache)
	Dispatcher.Reset();

	UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollisionSubsystem: Cleared %d type handlers, %d tag handlers"),
		TypeCount, TagCount);

	Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLUEPRINT API - TYPE HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::RegisterTypeHandler(EEntityType TypeA, EEntityType TypeB, FBPCollisionHandler Handler)
{
	if (!Handler.IsBound())
	{
		UE_LOG(LogBarrageCollision, Warning, TEXT("RegisterTypeHandler: Unbound handler for [%d + %d]"),
			static_cast<int32>(TypeA), static_cast<int32>(TypeB));
		return;
	}

	// Wrap Blueprint delegate in native handler
	auto NativeHandler = FNativeCollisionHandler::CreateLambda(
		[Handler](const FBarrageCollisionPayload& Payload) -> bool
		{
			return Handler.Execute(Payload);
		}
	);

	Dispatcher.RegisterTypeHandler(TypeA, TypeB, MoveTemp(NativeHandler));
}

void UBarrageCollisionSubsystem::UnregisterTypeHandler(EEntityType TypeA, EEntityType TypeB)
{
	Dispatcher.UnregisterTypeHandler(TypeA, TypeB);
}

bool UBarrageCollisionSubsystem::HasTypeHandler(EEntityType TypeA, EEntityType TypeB) const
{
	return Dispatcher.HasTypeHandler(TypeA, TypeB);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLUEPRINT API - TAG REGISTRATION
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::RegisterTag(FGameplayTag Tag, FGameplayTag Parent)
{
	Dispatcher.RegisterTag(Tag, Parent);
}

bool UBarrageCollisionSubsystem::IsTagRegistered(FGameplayTag Tag) const
{
	return Dispatcher.IsTagRegistered(Tag);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLUEPRINT API - TAG HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::RegisterTagHandler(FGameplayTag TagA, FGameplayTag TagB, FBPCollisionHandler Handler)
{
	if (!Handler.IsBound())
	{
		UE_LOG(LogBarrageCollision, Warning, TEXT("RegisterTagHandler: Unbound handler for [%s + %s]"),
			*TagA.ToString(), *TagB.ToString());
		return;
	}

	// Wrap Blueprint delegate in native handler
	auto NativeHandler = FNativeCollisionHandler::CreateLambda(
		[Handler](const FBarrageCollisionPayload& Payload) -> bool
		{
			return Handler.Execute(Payload);
		}
	);

	Dispatcher.RegisterBlueprintTagHandler(TagA, TagB, MoveTemp(NativeHandler));
}

void UBarrageCollisionSubsystem::UnregisterTagHandler(FGameplayTag TagA, FGameplayTag TagB)
{
	Dispatcher.UnregisterTagHandler(TagA, TagB);
}

bool UBarrageCollisionSubsystem::HasTagHandler(FGameplayTag TagA, FGameplayTag TagB) const
{
	return Dispatcher.HasTagHandler(TagA, TagB);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLUEPRINT API - CACHE MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::ClearCache()
{
	Dispatcher.ClearEntityCache();
}

void UBarrageCollisionSubsystem::InvalidateCacheEntry(int64 EntityKey)
{
	Dispatcher.InvalidateCacheEntry(static_cast<uint64>(EntityKey));
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLUEPRINT API - DEBUG
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::DumpState() const
{
	Dispatcher.DumpState();
}

int32 UBarrageCollisionSubsystem::GetTypeHandlerCount() const
{
	return Dispatcher.GetTypeHandlerCount();
}

int32 UBarrageCollisionSubsystem::GetTagHandlerCount() const
{
	return Dispatcher.GetTagHandlerCount();
}

int32 UBarrageCollisionSubsystem::GetCacheSize() const
{
	return Dispatcher.GetCacheSize();
}

// ═══════════════════════════════════════════════════════════════════════════════
// TAG LOOKUP
// ═══════════════════════════════════════════════════════════════════════════════

bool UBarrageCollisionSubsystem::LookupEntityTag(uint64 Key, FGameplayTag Tag)
{
	UArtilleryDispatch* Artillery = UArtilleryDispatch::SelfPtr;
	if (!Artillery)
	{
		return false;
	}
	return Artillery->DoesEntityHaveTag(FSkeletonKey(Key), Tag);
}

// ═══════════════════════════════════════════════════════════════════════════════
// DEFAULT HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::RegisterDefaultHandlers()
{
	// Get destructible tag (avoids cross-module linking issues)
	const FGameplayTag DestructibleTag = FGameplayTag::RequestGameplayTag(
		FName("Barrage.Destructible"), /*bErrorIfNotFound=*/ false
	);

	if (!DestructibleTag.IsValid())
	{
		UE_LOG(LogBarrageCollision, Warning,
			TEXT("BarrageCollisionSubsystem: Tag 'Barrage.Destructible' not found - destructible handler not registered"));
	}

	// Handler: Projectile + BarragePrimitive → Destroy destructible entities
	Dispatcher.RegisterTypeHandler(
		EEntityType::Projectile,
		EEntityType::BarragePrimitive,
		FNativeCollisionHandler::CreateLambda([DestructibleTag](const FBarrageCollisionPayload& Payload) -> bool
		{
			// Get the BarragePrimitive entity
			const FSkeletonKey EntityKey = Payload.GetKeyOfType(EEntityType::BarragePrimitive);
			if (!EntityKey.IsValid())
			{
				return false;
			}

			// Check if entity is destructible via Artillery tags
			UArtilleryDispatch* Artillery = UArtilleryDispatch::SelfPtr;
			if (!Artillery)
			{
				return false;
			}

			if (DestructibleTag.IsValid() && Artillery->DoesEntityHaveTag(EntityKey, DestructibleTag))
			{
				UE_LOG(LogBarrageCollision, Verbose,
					TEXT("Destroying destructible entity: Key=%llu"),
					static_cast<uint64>(EntityKey));

				ABarrageEntitySpawner::DestroyEntity(EntityKey);
				return true; // Consumed
			}

			return false; // Not consumed, continue to next handler
		})
	);

	UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollisionSubsystem: Default handlers registered"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// EVENT HANDLING
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionSubsystem::OnBarrageContact(const BarrageContactEvent& Event)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return;
	}

	// Get physics bodies from Barrage
	const FBLet Body1 = Physics->GetShapeRef(Event.ContactEntity1.ContactKey);
	const FBLet Body2 = Physics->GetShapeRef(Event.ContactEntity2.ContactKey);

	// Extract skeleton keys from bodies
	const FSkeletonKey Key1 = FBarragePrimitive::IsNotNull(Body1) ? Body1->KeyOutOfBarrage : FSkeletonKey();
	const FSkeletonKey Key2 = FBarragePrimitive::IsNotNull(Body2) ? Body2->KeyOutOfBarrage : FSkeletonKey();

	// Need at least one valid entity
	if (!Key1.IsValid() && !Key2.IsValid())
	{
		return;
	}

	// Build collision payload
	FBarrageCollisionPayload Payload;
	Payload.EntityA = static_cast<int64>(static_cast<uint64>(Key1));
	Payload.EntityB = static_cast<int64>(static_cast<uint64>(Key2));
	Payload.ContactPoint = Event.PointIfAny;
	Payload.TypeA = EntityTypeUtils::GetEntityType(Key1);
	Payload.TypeB = EntityTypeUtils::GetEntityType(Key2);
	// Tags are populated lazily by dispatcher when needed

	// Dispatch through Phosphorus (Type dispatch first, then Tag dispatch)
	Dispatcher.DispatchWithKeyLookup(
		Payload.TypeA, Payload.TypeB,
		Payload.GetKeyA_Raw(), Payload.GetKeyB_Raw(),
		Payload
	);
}
