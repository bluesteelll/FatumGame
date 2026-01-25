// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Barrage Collision Processors - Dual dispatch system (Entity Types + Tags)

#include "BarrageCollisionProcessors.h"
#include "BarrageDispatch.h"
#include "BarrageContactEvent.h"
#include "FBarragePrimitive.h"
#include "Systems/ArtilleryDispatch.h"
#include "Systems/BarrageEntitySpawner.h"
#include "GameplayTagsManager.h"

// ═══════════════════════════════════════════════════════════════════════════════
// CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

namespace
{
	// Size of symmetric matrix for type handlers
	// For N types, we need N*(N+1)/2 slots (upper triangle including diagonal)
	constexpr int32 NumTypes = static_cast<int32>(EEntityType::NUM_TYPES);
	constexpr int32 TypeMatrixSize = (NumTypes * (NumTypes + 1)) / 2;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════════

UBarrageCollisionProcessors* UBarrageCollisionProcessors::Get(UWorld* World)
{
	return World ? World->GetSubsystem<UBarrageCollisionProcessors>() : nullptr;
}

void UBarrageCollisionProcessors::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Initializing..."));

	// Initialize type handler matrix
	TypeHandlers.SetNum(TypeMatrixSize);
	TypeHandlerCount = 0;

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Type matrix allocated (%d slots for %d types)"),
		TypeMatrixSize, NumTypes);

	// Initialize tag dispatchers
	TagDispatcher.SetName(TEXT("BarrageCollision.Tag.Blueprint"));
	NativeTagDispatcher.SetName(TEXT("BarrageCollision.Tag.Native"));

	// Subscribe to Barrage collision events
	if (UBarrageDispatch* BD = InWorld.GetSubsystem<UBarrageDispatch>())
	{
		ContactHandle = BD->OnBarrageContactAddedDelegate.AddUObject(
			this, &UBarrageCollisionProcessors::OnContactAdded);
		UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Subscribed to Barrage contact events"));
	}
	else
	{
		UE_LOG(LogPhosphorus, Warning, TEXT("BarrageCollisionProcessors: UBarrageDispatch not found!"));
	}

	// Register default handlers
	RegisterDefaultHandlers();

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Online"));
}

void UBarrageCollisionProcessors::Deinitialize()
{
	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Deinitializing..."));

	// Unsubscribe from Barrage
	if (UBarrageDispatch::SelfPtr && ContactHandle.IsValid())
	{
		UBarrageDispatch::SelfPtr->OnBarrageContactAddedDelegate.Remove(ContactHandle);
	}

	// Log stats
	int32 TypeCount = TypeHandlerCount;
	int32 TagCount = TagDispatcher.GetRegisteredHandlerCount() + NativeTagDispatcher.GetRegisteredHandlerCount();

	// Clear state
	TypeHandlers.Empty();
	TypeHandlerCount = 0;
	TagDispatcher.Reset();
	NativeTagDispatcher.Reset();
	EntityTagCache.Empty();
	RegisteredTags.Empty();

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Cleared %d type handlers, %d tag handlers"),
		TypeCount, TagCount);

	Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════════════════════
// TYPE INDEX CALCULATION
// ═══════════════════════════════════════════════════════════════════════════════

int32 UBarrageCollisionProcessors::GetTypeIndex(EEntityType A, EEntityType B)
{
	// Symmetric matrix stored as upper triangle
	uint8 i = static_cast<uint8>(A);
	uint8 j = static_cast<uint8>(B);

	// Ensure i <= j for symmetry
	if (i > j)
	{
		Swap(i, j);
	}

	return i * NumTypes - (i * (i + 1)) / 2 + j;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TYPE HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionProcessors::RegisterTypeHandler(EEntityType TypeA, EEntityType TypeB, FNativeCollisionHandler Handler)
{
	if (!Handler.IsBound())
	{
		UE_LOG(LogPhosphorus, Warning,
			TEXT("BarrageCollisionProcessors: RegisterTypeHandler called with unbound handler"));
		return;
	}

	int32 Index = GetTypeIndex(TypeA, TypeB);
	if (!ensureMsgf(Index >= 0 && Index < TypeHandlers.Num(),
		TEXT("Type index out of bounds: %d"), Index))
	{
		return;
	}

	bool bWasEmpty = !TypeHandlers[Index].IsBound();
	TypeHandlers[Index] = MoveTemp(Handler);

	if (bWasEmpty)
	{
		TypeHandlerCount++;
	}

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Registered type handler [%d + %d]"),
		static_cast<int32>(TypeA), static_cast<int32>(TypeB));
}

void UBarrageCollisionProcessors::RegisterTypeHandlerBP(EEntityType TypeA, EEntityType TypeB, FBPCollisionHandler Handler)
{
	if (!Handler.IsBound())
	{
		return;
	}

	auto NativeWrapper = FNativeCollisionHandler::CreateLambda(
		[Handler](const FBarrageCollisionPayload& Payload) -> bool
		{
			return Handler.Execute(Payload);
		});

	RegisterTypeHandler(TypeA, TypeB, MoveTemp(NativeWrapper));
}

void UBarrageCollisionProcessors::UnregisterTypeHandler(EEntityType TypeA, EEntityType TypeB)
{
	int32 Index = GetTypeIndex(TypeA, TypeB);
	if (Index >= 0 && Index < TypeHandlers.Num() && TypeHandlers[Index].IsBound())
	{
		TypeHandlers[Index].Unbind();
		TypeHandlerCount--;
		UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Unregistered type handler [%d + %d]"),
			static_cast<int32>(TypeA), static_cast<int32>(TypeB));
	}
}

bool UBarrageCollisionProcessors::HasTypeHandler(EEntityType TypeA, EEntityType TypeB) const
{
	int32 Index = GetTypeIndex(TypeA, TypeB);
	return Index >= 0 && Index < TypeHandlers.Num() && TypeHandlers[Index].IsBound();
}

// ═══════════════════════════════════════════════════════════════════════════════
// TAG HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionProcessors::RegisterTag(FGameplayTag Tag, FGameplayTag Parent)
{
	if (!Tag.IsValid())
	{
		return;
	}

	TagDispatcher.RegisterType(Tag, Parent);
	NativeTagDispatcher.RegisterType(Tag, Parent);

	if (!RegisteredTags.Contains(Tag))
	{
		RegisteredTags.Add(Tag);
	}

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Registered tag '%s'%s"),
		*Tag.ToString(),
		Parent.IsValid() ? *FString::Printf(TEXT(" with parent '%s'"), *Parent.ToString()) : TEXT(""));
}

bool UBarrageCollisionProcessors::IsTagRegistered(FGameplayTag Tag) const
{
	return TagDispatcher.IsTypeRegistered(Tag);
}

void UBarrageCollisionProcessors::RegisterTagHandler(FGameplayTag TagA, FGameplayTag TagB, FBPCollisionHandler Handler)
{
	if (!Handler.IsBound())
	{
		return;
	}

	auto NativeWrapper = TPhosphorusDispatcher<FBarrageCollisionPayload>::FHandler::CreateLambda(
		[Handler](const FBarrageCollisionPayload& Payload) -> bool
		{
			return Handler.Execute(Payload);
		});

	TagDispatcher.RegisterHandler(TagA, TagB, MoveTemp(NativeWrapper));

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Registered BP tag handler [%s + %s]"),
		*TagA.ToString(), *TagB.ToString());
}

void UBarrageCollisionProcessors::RegisterNativeTagHandler(FGameplayTag TagA, FGameplayTag TagB, FNativeCollisionHandler Handler)
{
	if (!Handler.IsBound())
	{
		return;
	}

	auto PhosphorusHandler = TPhosphorusDispatcher<FBarrageCollisionPayload>::FHandler::CreateLambda(
		[Handler](const FBarrageCollisionPayload& Payload) -> bool
		{
			return Handler.Execute(Payload);
		});

	NativeTagDispatcher.RegisterHandler(TagA, TagB, MoveTemp(PhosphorusHandler));

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Registered native tag handler [%s + %s]"),
		*TagA.ToString(), *TagB.ToString());
}

void UBarrageCollisionProcessors::UnregisterTagHandler(FGameplayTag TagA, FGameplayTag TagB)
{
	bool bRemovedBP = TagDispatcher.UnregisterHandler(TagA, TagB);
	bool bRemovedNative = NativeTagDispatcher.UnregisterHandler(TagA, TagB);

	if (bRemovedBP || bRemovedNative)
	{
		UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Unregistered tag handler [%s + %s]"),
			*TagA.ToString(), *TagB.ToString());
	}
}

bool UBarrageCollisionProcessors::HasTagHandler(FGameplayTag TagA, FGameplayTag TagB) const
{
	return TagDispatcher.HasHandler(TagA, TagB) || NativeTagDispatcher.HasHandler(TagA, TagB);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CACHE
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionProcessors::ClearCache()
{
	int32 CacheSize = EntityTagCache.Num();
	EntityTagCache.Reset();
	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Cleared tag cache (%d entries)"), CacheSize);
}

FGameplayTag UBarrageCollisionProcessors::GetEntityTag(FSkeletonKey Key)
{
	if (!Key.IsValid())
	{
		return FGameplayTag();
	}

	if (FGameplayTag* Cached = EntityTagCache.Find(Key))
	{
		return *Cached;
	}

	FGameplayTag Result;
	UArtilleryDispatch* Artillery = UArtilleryDispatch::SelfPtr;

	if (Artillery)
	{
		for (const FGameplayTag& Tag : RegisteredTags)
		{
			if (Artillery->DoesEntityHaveTag(Key, Tag))
			{
				Result = Tag;
				break;
			}
		}
	}

	EntityTagCache.Add(Key, Result);
	return Result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// UTILITIES
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionProcessors::DumpState() const
{
	UE_LOG(LogPhosphorus, Log, TEXT("════════════════════════════════════════════════════════════════"));
	UE_LOG(LogPhosphorus, Log, TEXT("BARRAGE COLLISION PROCESSORS - STATE DUMP"));
	UE_LOG(LogPhosphorus, Log, TEXT("════════════════════════════════════════════════════════════════"));
	UE_LOG(LogPhosphorus, Log, TEXT("Type Handlers: %d"), TypeHandlerCount);
	UE_LOG(LogPhosphorus, Log, TEXT("Tag Handlers: %d"), TagDispatcher.GetRegisteredHandlerCount() + NativeTagDispatcher.GetRegisteredHandlerCount());
	UE_LOG(LogPhosphorus, Log, TEXT("Registered Tags: %d"), RegisteredTags.Num());
	UE_LOG(LogPhosphorus, Log, TEXT("Entity Tag Cache: %d entries"), EntityTagCache.Num());
	UE_LOG(LogPhosphorus, Log, TEXT("════════════════════════════════════════════════════════════════"));
}

int32 UBarrageCollisionProcessors::GetTypeHandlerCount() const
{
	return TypeHandlerCount;
}

int32 UBarrageCollisionProcessors::GetTagHandlerCount() const
{
	return TagDispatcher.GetRegisteredHandlerCount() + NativeTagDispatcher.GetRegisteredHandlerCount();
}

// ═══════════════════════════════════════════════════════════════════════════════
// DEFAULT HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionProcessors::RegisterDefaultHandlers()
{
	// Get destructible tag by name (avoids cross-module linking issues)
	FGameplayTag DestructibleTag = FGameplayTag::RequestGameplayTag(FName("Barrage.Destructible"), false);

	if (!DestructibleTag.IsValid())
	{
		UE_LOG(LogPhosphorus, Warning, TEXT("BarrageCollisionProcessors: Tag 'Barrage.Destructible' not found"));
	}

	// Projectile + BarragePrimitive: destroy destructible entities
	RegisterTypeHandler(
		EEntityType::Projectile,
		EEntityType::BarragePrimitive,
		FNativeCollisionHandler::CreateLambda([DestructibleTag](const FBarrageCollisionPayload& P) -> bool
		{
			FSkeletonKey EntityKey = P.GetKeyOfType(EEntityType::BarragePrimitive);
			if (!EntityKey.IsValid())
			{
				UE_LOG(LogPhosphorus, Warning, TEXT("BarrageCollisionProcessors: Projectile+BarragePrimitive handler - no valid BarragePrimitive key"));
				return false;
			}

			UArtilleryDispatch* Artillery = UArtilleryDispatch::SelfPtr;
			if (!Artillery)
			{
				UE_LOG(LogPhosphorus, Warning, TEXT("BarrageCollisionProcessors: No Artillery dispatch"));
				return false;
			}

			// Check if entity is destructible
			if (DestructibleTag.IsValid() && Artillery->DoesEntityHaveTag(EntityKey, DestructibleTag))
			{
				UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Destroying destructible entity Key=%llu"),
					static_cast<uint64>(EntityKey));

				// Use the proper destroy method that handles both physics and render
				ABarrageEntitySpawner::DestroyEntity(EntityKey);
				return true; // consumed
			}
			else
			{
				UE_LOG(LogPhosphorus, Verbose, TEXT("BarrageCollisionProcessors: Entity Key=%llu not destructible (tag valid=%d)"),
					static_cast<uint64>(EntityKey), DestructibleTag.IsValid() ? 1 : 0);
			}

			return false;
		})
	);

	UE_LOG(LogPhosphorus, Log, TEXT("BarrageCollisionProcessors: Registered default handlers"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// COLLISION PROCESSING
// ═══════════════════════════════════════════════════════════════════════════════

void UBarrageCollisionProcessors::OnContactAdded(const BarrageContactEvent& Event)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return;
	}

	// Get bodies from Barrage
	FBLet Body1 = Physics->GetShapeRef(Event.ContactEntity1.ContactKey);
	FBLet Body2 = Physics->GetShapeRef(Event.ContactEntity2.ContactKey);

	// Extract keys
	FSkeletonKey Key1 = FBarragePrimitive::IsNotNull(Body1) ? Body1->KeyOutOfBarrage : FSkeletonKey();
	FSkeletonKey Key2 = FBarragePrimitive::IsNotNull(Body2) ? Body2->KeyOutOfBarrage : FSkeletonKey();

	// Need at least one valid entity
	if (!Key1.IsValid() && !Key2.IsValid())
	{
		return;
	}

	// Build payload - types derived from skeleton keys (O(1))
	FBarrageCollisionPayload Payload;
	Payload.EntityA = static_cast<int64>(static_cast<uint64>(Key1));
	Payload.EntityB = static_cast<int64>(static_cast<uint64>(Key2));
	Payload.ContactPoint = Event.PointIfAny;
	Payload.TypeA = EntityTypeUtils::GetEntityType(Key1);
	Payload.TypeB = EntityTypeUtils::GetEntityType(Key2);

	UE_LOG(LogPhosphorus, Verbose, TEXT("BarrageCollisionProcessors: Contact event - TypeA=%d TypeB=%d"),
		static_cast<int32>(Payload.TypeA), static_cast<int32>(Payload.TypeB));

	// Try type dispatch first (faster, no cache lookup)
	if (TryDispatchByType(Payload))
	{
		return;
	}

	// Then try tag dispatch (populates tags lazily)
	TryDispatchByTag(Payload);
}

bool UBarrageCollisionProcessors::TryDispatchByType(const FBarrageCollisionPayload& Payload)
{
	// Skip if both are Unknown
	if (Payload.TypeA == EEntityType::Unknown && Payload.TypeB == EEntityType::Unknown)
	{
		return false;
	}

	int32 Index = GetTypeIndex(Payload.TypeA, Payload.TypeB);
	if (Index >= 0 && Index < TypeHandlers.Num() && TypeHandlers[Index].IsBound())
	{
		bool bConsumed = TypeHandlers[Index].Execute(Payload);
		if (bConsumed)
		{
			UE_LOG(LogPhosphorus, Verbose, TEXT("BarrageCollisionProcessors: Type handler consumed event"));
			return true;
		}
	}

	return false;
}

bool UBarrageCollisionProcessors::TryDispatchByTag(FBarrageCollisionPayload& Payload)
{
	// Only lookup tags if we have registered tags
	if (RegisteredTags.Num() == 0)
	{
		return false;
	}

	// Lazy populate tag fields
	Payload.TagA = GetEntityTag(Payload.GetKeyA());
	Payload.TagB = GetEntityTag(Payload.GetKeyB());

	if (!Payload.TagA.IsValid() && !Payload.TagB.IsValid())
	{
		return false;
	}

	// Try native handlers first
	Phosphorus::EDispatchResult NativeResult = NativeTagDispatcher.Dispatch(Payload.TagA, Payload.TagB, Payload);
	if (NativeResult == Phosphorus::EDispatchResult::Consumed)
	{
		return true;
	}

	// Then try Blueprint handlers
	Phosphorus::EDispatchResult BPResult = TagDispatcher.Dispatch(Payload.TagA, Payload.TagB, Payload);
	if (BPResult == Phosphorus::EDispatchResult::Consumed)
	{
		return true;
	}

	return false;
}
