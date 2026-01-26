// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Barrage Collision Types - Payload, delegates, and type aliases

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SkeletonTypes.h"
#include "EEntityType.h"
#include "PhosphorusEntityDispatcher.h"
#include "BarrageCollisionTypes.generated.h"

// ═══════════════════════════════════════════════════════════════════════════════
// FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════════

struct BarrageContactEvent;
class FBarragePrimitive;
using FBLet = TSharedPtr<FBarragePrimitive>;

// ═══════════════════════════════════════════════════════════════════════════════
// COLLISION PAYLOAD
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * FBarrageCollisionPayload - Data passed to collision handlers
 *
 * Contains all information about a collision event:
 * - Entity keys (as int64 for Blueprint compatibility)
 * - Entity types (derived from skeleton key, O(1))
 * - Gameplay tags (lazily populated for tag dispatch)
 * - Contact point in world space
 */
USTRUCT(BlueprintType)
struct BARRAGECOLLISION_API FBarrageCollisionPayload
{
	GENERATED_BODY()

	// ═══════════════════════════════════════════════════════════════════════════
	// ENTITY IDENTIFICATION
	// ═══════════════════════════════════════════════════════════════════════════

	/** Entity A key (as int64 for Blueprint compatibility) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	int64 EntityA = 0;

	/** Entity B key (as int64 for Blueprint compatibility) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	int64 EntityB = 0;

	/** Entity type of A (derived from skeleton key) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	EEntityType TypeA = EEntityType::Unknown;

	/** Entity type of B (derived from skeleton key) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	EEntityType TypeB = EEntityType::Unknown;

	/** Gameplay tag of entity A (populated lazily for tag dispatch) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FGameplayTag TagA;

	/** Gameplay tag of entity B (populated lazily for tag dispatch) */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FGameplayTag TagB;

	// ═══════════════════════════════════════════════════════════════════════════
	// COLLISION DATA
	// ═══════════════════════════════════════════════════════════════════════════

	/** Contact point in world space */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FVector ContactPoint = FVector::ZeroVector;

	// ═══════════════════════════════════════════════════════════════════════════
	// KEY ACCESSORS
	// ═══════════════════════════════════════════════════════════════════════════

	/** Get entity A as FSkeletonKey */
	FORCEINLINE FSkeletonKey GetKeyA() const
	{
		return FSkeletonKey(static_cast<uint64>(EntityA));
	}

	/** Get entity B as FSkeletonKey */
	FORCEINLINE FSkeletonKey GetKeyB() const
	{
		return FSkeletonKey(static_cast<uint64>(EntityB));
	}

	/** Get entity A key as uint64 */
	FORCEINLINE uint64 GetKeyA_Raw() const
	{
		return static_cast<uint64>(EntityA);
	}

	/** Get entity B key as uint64 */
	FORCEINLINE uint64 GetKeyB_Raw() const
	{
		return static_cast<uint64>(EntityB);
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// TYPE QUERIES
	// ═══════════════════════════════════════════════════════════════════════════

	/** Check if entity A is of given type */
	FORCEINLINE bool IsAOfType(EEntityType Type) const { return TypeA == Type; }

	/** Check if entity B is of given type */
	FORCEINLINE bool IsBOfType(EEntityType Type) const { return TypeB == Type; }

	/** Check if either entity is of given type */
	FORCEINLINE bool HasType(EEntityType Type) const
	{
		return TypeA == Type || TypeB == Type;
	}

	/** Check if collision is between two specific types (order-independent) */
	FORCEINLINE bool IsBetweenTypes(EEntityType Type1, EEntityType Type2) const
	{
		return (TypeA == Type1 && TypeB == Type2) ||
		       (TypeA == Type2 && TypeB == Type1);
	}

	/**
	 * Get the key of the entity matching the given type
	 * Returns invalid key if neither entity matches
	 */
	FORCEINLINE FSkeletonKey GetKeyOfType(EEntityType Type) const
	{
		if (TypeA == Type) return GetKeyA();
		if (TypeB == Type) return GetKeyB();
		return FSkeletonKey();
	}

	/**
	 * Get the key of the entity NOT matching the given type
	 * Returns invalid key if neither entity matches the type
	 */
	FORCEINLINE FSkeletonKey GetOtherKey(EEntityType Type) const
	{
		if (TypeA == Type) return GetKeyB();
		if (TypeB == Type) return GetKeyA();
		return FSkeletonKey();
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// TAG QUERIES
	// ═══════════════════════════════════════════════════════════════════════════

	/** Check if entity A has given gameplay tag */
	FORCEINLINE bool IsAOfTag(FGameplayTag Tag) const
	{
		return TagA.MatchesTag(Tag);
	}

	/** Check if entity B has given gameplay tag */
	FORCEINLINE bool IsBOfTag(FGameplayTag Tag) const
	{
		return TagB.MatchesTag(Tag);
	}

	/** Check if either entity has given gameplay tag */
	FORCEINLINE bool HasTag(FGameplayTag Tag) const
	{
		return TagA.MatchesTag(Tag) || TagB.MatchesTag(Tag);
	}

	/**
	 * Get the key of the entity matching the given tag
	 * Returns invalid key if neither entity matches
	 */
	FORCEINLINE FSkeletonKey GetKeyOfTag(FGameplayTag Tag) const
	{
		if (TagA.MatchesTag(Tag)) return GetKeyA();
		if (TagB.MatchesTag(Tag)) return GetKeyB();
		return FSkeletonKey();
	}

	/**
	 * Get the key of the entity NOT matching the given tag
	 * Returns invalid key if neither entity has the tag
	 */
	FORCEINLINE FSkeletonKey GetOtherKeyByTag(FGameplayTag Tag) const
	{
		if (TagA.MatchesTag(Tag)) return GetKeyB();
		if (TagB.MatchesTag(Tag)) return GetKeyA();
		return FSkeletonKey();
	}
};

// ═══════════════════════════════════════════════════════════════════════════════
// DISPATCHER TYPE ALIASES
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * FBarrageCollisionDispatcher - Specialized dispatcher for collision events
 *
 * Template instantiation of TPhosphorusEntityDispatcher with:
 * - FBarrageCollisionPayload as the event data
 * - EEntityType::NUM_TYPES as the type count (14 types)
 *
 * Provides:
 * - O(1) type-based dispatch via symmetric matrix
 * - O(log N) tag-based dispatch via Phosphorus
 * - Entity tag caching for Artillery lookups
 */
using FBarrageCollisionDispatcher = TPhosphorusEntityDispatcher<
	FBarrageCollisionPayload,
	static_cast<int32>(EEntityType::NUM_TYPES)
>;

// ═══════════════════════════════════════════════════════════════════════════════
// HANDLER DELEGATES
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * FBPCollisionHandler - Blueprint collision handler delegate
 *
 * Signature: bool Handler(const FBarrageCollisionPayload& Payload)
 * Return true to consume the event (stop processing chain)
 * Return false to continue to next handler
 */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(
	bool,
	FBPCollisionHandler,
	const FBarrageCollisionPayload&, Payload
);

/**
 * FNativeCollisionHandler - Native collision handler type
 *
 * Alias for the Phosphorus handler delegate type.
 * Use for C++ handler registration.
 */
using FNativeCollisionHandler = FBarrageCollisionDispatcher::FHandler;
