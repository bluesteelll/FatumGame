// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Barrage Collision Processors - Dual dispatch system (Entity Types + Tags)

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NativeGameplayTags.h"
#include "SkeletonTypes.h"
#include "EEntityType.h"
#include "EPhysicsLayer.h"
#include "PhosphorusDispatcher.h"
#include "BarrageCollisionProcessors.generated.h"

struct BarrageContactEvent;
class FBarragePrimitive;
using FBLet = TSharedPtr<FBarragePrimitive>;

// ═══════════════════════════════════════════════════════════════════════════════
// COLLISION PAYLOAD
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * FBarrageCollisionPayload - Data passed to collision handlers
 */
USTRUCT(BlueprintType)
struct FBarrageCollisionPayload
{
	GENERATED_BODY()

	/** Entity A key (as int64 for Blueprint compatibility) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	int64 EntityA = 0;

	/** Entity B key (as int64 for Blueprint compatibility) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	int64 EntityB = 0;

	/** Contact point in world space */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FVector ContactPoint = FVector::ZeroVector;

	/** Entity type of A (derived from skeleton key) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	EEntityType TypeA = EEntityType::Unknown;

	/** Entity type of B (derived from skeleton key) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	EEntityType TypeB = EEntityType::Unknown;

	/** Gameplay tag of entity A (optional, for tag-based dispatch) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FGameplayTag TagA;

	/** Gameplay tag of entity B (optional, for tag-based dispatch) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FGameplayTag TagB;

	/** Get entity A as FSkeletonKey */
	FSkeletonKey GetKeyA() const { return FSkeletonKey(static_cast<uint64>(EntityA)); }

	/** Get entity B as FSkeletonKey */
	FSkeletonKey GetKeyB() const { return FSkeletonKey(static_cast<uint64>(EntityB)); }

	/** Check if entity A is of given type */
	bool IsAOfType(EEntityType Type) const { return TypeA == Type; }

	/** Check if entity B is of given type */
	bool IsBOfType(EEntityType Type) const { return TypeB == Type; }

	/** Get the key of the entity matching the given type */
	FSkeletonKey GetKeyOfType(EEntityType Type) const
	{
		if (TypeA == Type) return GetKeyA();
		if (TypeB == Type) return GetKeyB();
		return FSkeletonKey();
	}

	/** Get the key of the entity NOT matching the given type */
	FSkeletonKey GetOtherKey(EEntityType Type) const
	{
		if (TypeA == Type) return GetKeyB();
		if (TypeB == Type) return GetKeyA();
		return FSkeletonKey();
	}

	/** Check if entity A has given gameplay tag */
	bool IsAOfTag(FGameplayTag Tag) const { return TagA.MatchesTag(Tag); }

	/** Check if entity B has given gameplay tag */
	bool IsBOfTag(FGameplayTag Tag) const { return TagB.MatchesTag(Tag); }

	/** Get the key of the entity matching the given tag */
	FSkeletonKey GetKeyOfTag(FGameplayTag Tag) const
	{
		if (TagA.MatchesTag(Tag)) return GetKeyA();
		if (TagB.MatchesTag(Tag)) return GetKeyB();
		return FSkeletonKey();
	}
};

// ═══════════════════════════════════════════════════════════════════════════════
// DELEGATES
// ═══════════════════════════════════════════════════════════════════════════════

/** Blueprint collision handler delegate */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(bool, FBPCollisionHandler, const FBarrageCollisionPayload&, Payload);

/** Native collision handler type */
using FNativeCollisionHandler = TDelegate<bool(const FBarrageCollisionPayload&)>;

// ═══════════════════════════════════════════════════════════════════════════════
// COLLISION PROCESSORS SUBSYSTEM
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * UBarrageCollisionProcessors - Dual dispatch collision system
 *
 * Two dispatch mechanisms:
 * 1. ENTITY TYPE (fast) - O(1) matrix lookup based on FSkeletonKey type nibble
 * 2. TAGS (flexible) - Phosphorus dispatcher for dynamic/complex cases
 *
 * Entity types are derived directly from skeleton keys - no storage overhead.
 * Types are checked first (faster), then tags if no type handler consumed the event.
 *
 * Example:
 *   auto* CP = UBarrageCollisionProcessors::Get(GetWorld());
 *
 *   // Fast type-based handler (all projectiles hitting actors)
 *   CP->RegisterTypeHandler(EEntityType::Projectile, EEntityType::Actor, MyHandler);
 *
 *   // Tag handler for special cases (vampire boss + silver weapon)
 *   CP->RegisterTagHandler(TAG_Boss_Vampire, TAG_Weapon_Silver, VampireSilverHandler);
 */
UCLASS()
class UBarrageCollisionProcessors : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Get subsystem instance for world */
	static UBarrageCollisionProcessors* Get(UWorld* World);

	// ═══════════════════════════════════════════════════════════════════════════
	// ENTITY TYPE HANDLERS (fast, O(1) lookup from skeleton key)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a native handler for an entity type pair
	 * Handler returns true to consume event, false to continue to next handler
	 */
	void RegisterTypeHandler(EEntityType TypeA, EEntityType TypeB, FNativeCollisionHandler Handler);

	/**
	 * Register a Blueprint handler for an entity type pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void RegisterTypeHandlerBP(EEntityType TypeA, EEntityType TypeB, FBPCollisionHandler Handler);

	/**
	 * Unregister handler for an entity type pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void UnregisterTypeHandler(EEntityType TypeA, EEntityType TypeB);

	/**
	 * Check if a handler exists for entity type pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	bool HasTypeHandler(EEntityType TypeA, EEntityType TypeB) const;

	// ═══════════════════════════════════════════════════════════════════════════
	// TAG HANDLERS (flexible, for complex cases)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a gameplay tag with optional parent for hierarchy fallback
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void RegisterTag(FGameplayTag Tag, FGameplayTag Parent = FGameplayTag());

	/**
	 * Check if a tag is registered
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	bool IsTagRegistered(FGameplayTag Tag) const;

	/**
	 * Register a Blueprint tag handler for a tag pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void RegisterTagHandler(FGameplayTag TagA, FGameplayTag TagB, FBPCollisionHandler Handler);

	/**
	 * Register a native tag handler for a tag pair
	 */
	void RegisterNativeTagHandler(FGameplayTag TagA, FGameplayTag TagB, FNativeCollisionHandler Handler);

	/**
	 * Unregister tag handler for a tag pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void UnregisterTagHandler(FGameplayTag TagA, FGameplayTag TagB);

	/**
	 * Check if a tag handler exists for tag pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	bool HasTagHandler(FGameplayTag TagA, FGameplayTag TagB) const;

	// ═══════════════════════════════════════════════════════════════════════════
	// UTILITIES
	// ═══════════════════════════════════════════════════════════════════════════

	/** Clear entity tag cache */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void ClearCache();

	/** Get gameplay tag for entity from Artillery (cached) */
	FGameplayTag GetEntityTag(FSkeletonKey Key);

	/** Dump state to log */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision|Debug")
	void DumpState() const;

	/** Get number of registered type handlers */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision|Debug")
	int32 GetTypeHandlerCount() const;

	/** Get number of registered tag handlers */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision|Debug")
	int32 GetTagHandlerCount() const;

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	// ═══════════════════════════════════════════════════════════════════════════
	// TYPE DISPATCH (fast matrix based on EEntityType)
	// ═══════════════════════════════════════════════════════════════════════════

	/** Get matrix index for type pair (symmetric) */
	static int32 GetTypeIndex(EEntityType A, EEntityType B);

	/** Type handlers stored in flat array (symmetric matrix) */
	TArray<FNativeCollisionHandler> TypeHandlers;

	/** Number of type handlers registered */
	int32 TypeHandlerCount = 0;

	// ═══════════════════════════════════════════════════════════════════════════
	// TAG DISPATCH (Phosphorus)
	// ═══════════════════════════════════════════════════════════════════════════

	/** Blueprint tag dispatcher */
	TPhosphorusDispatcher<FBarrageCollisionPayload> TagDispatcher;

	/** Native tag dispatcher (higher priority) */
	TPhosphorusDispatcher<FBarrageCollisionPayload> NativeTagDispatcher;

	/** Registered tags for cache lookup */
	TArray<FGameplayTag> RegisteredTags;

	// ═══════════════════════════════════════════════════════════════════════════
	// ENTITY TAG CACHE
	// ═══════════════════════════════════════════════════════════════════════════

	/** Cache of entity -> gameplay tag lookups */
	TMap<FSkeletonKey, FGameplayTag> EntityTagCache;

	// ═══════════════════════════════════════════════════════════════════════════
	// EVENT HANDLING
	// ═══════════════════════════════════════════════════════════════════════════

	/** Delegate handle for Barrage contact events */
	FDelegateHandle ContactHandle;

	/** Called when Barrage detects a collision */
	void OnContactAdded(const BarrageContactEvent& Event);

	/** Try type dispatch first */
	bool TryDispatchByType(const FBarrageCollisionPayload& Payload);

	/** Then try tag dispatch */
	bool TryDispatchByTag(FBarrageCollisionPayload& Payload);

	/** Register default collision handlers */
	void RegisterDefaultHandlers();
};
