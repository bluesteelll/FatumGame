// Bridge components for Flecs-Barrage integration.
// These provide bidirectional lock-free binding between Flecs entities and Barrage physics bodies.

#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"
#include "FlecsBarrageComponents.generated.h"

class UStaticMesh;

// ═══════════════════════════════════════════════════════════════
// BIDIRECTIONAL BINDING ARCHITECTURE
// ═══════════════════════════════════════════════════════════════
//
// Forward lookup (Entity → BarrageKey):
//   Flecs sparse set: entity.get<FBarrageBody>().BarrageKey  [O(1)]
//
// Reverse lookup (BarrageKey → Entity):
//   libcuckoo map: KeyToFBLet[BarrageKey] → FBLet            [O(1)]
//   atomic load:   FBLet->GetFlecsEntity()                   [O(1)]
//
// Both directions are lock-free and thread-safe.
// ═══════════════════════════════════════════════════════════════

/**
 * Forward binding: Flecs Entity → Barrage physics body.
 * Part of bidirectional lock-free binding system.
 *
 * Usage:
 *   FSkeletonKey Key = entity.get<FBarrageBody>()->BarrageKey;
 *
 * Reverse binding (BarrageKey → Entity) is via FBarragePrimitive::GetFlecsEntity()
 */
USTRUCT(BlueprintType)
struct FLECSBARRAGE_API FBarrageBody
{
	GENERATED_BODY()

	/** SkeletonKey for the Barrage primitive. Used to look up the FBLet. */
	UPROPERTY(BlueprintReadOnly, Category = "Physics")
	FSkeletonKey BarrageKey;

	bool IsValid() const { return BarrageKey.IsValid(); }
};

/**
 * ISM (Instanced Static Mesh) rendering data.
 * Entities with this are rendered via UBarrageRenderManager.
 */
USTRUCT(BlueprintType)
struct FLECSBARRAGE_API FISMRender
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Render")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(BlueprintReadWrite, Category = "Render")
	FVector Scale = FVector::OneVector;
};

// ═══════════════════════════════════════════════════════════════
// CONSTRAINT COMPONENTS
// ═══════════════════════════════════════════════════════════════

/** Single constraint link data. */
USTRUCT(BlueprintType)
struct FLECSBARRAGE_API FConstraintLink
{
	GENERATED_BODY()

	/** Constraint key from Barrage ConstraintSystem */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	int64 ConstraintKey = 0;

	/** SkeletonKey of the other entity in this constraint */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	FSkeletonKey OtherEntityKey;

	/** Break force threshold (0 = unbreakable) */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	float BreakForce = 0.f;

	/** Break torque threshold (0 = unbreakable) */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	float BreakTorque = 0.f;

	bool IsValid() const { return ConstraintKey != 0; }
};

/** Constraint data. Entities with this are constrained to other entities. */
USTRUCT(BlueprintType)
struct FLECSBARRAGE_API FFlecsConstraintData
{
	GENERATED_BODY()

	/** All constraints this entity participates in */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	TArray<FConstraintLink> Constraints;

	int32 GetConstraintCount() const { return Constraints.Num(); }
	bool HasConstraints() const { return Constraints.Num() > 0; }

	void AddConstraint(int64 Key, FSkeletonKey OtherKey, float BreakForce = 0.f, float BreakTorque = 0.f)
	{
		FConstraintLink Link;
		Link.ConstraintKey = Key;
		Link.OtherEntityKey = OtherKey;
		Link.BreakForce = BreakForce;
		Link.BreakTorque = BreakTorque;
		Constraints.Add(Link);
	}

	bool RemoveConstraint(int64 Key)
	{
		return Constraints.RemoveAll([Key](const FConstraintLink& L) { return L.ConstraintKey == Key; }) > 0;
	}
};

/** Tag for entities that are part of a constraint chain */
struct FTagConstrained {};

// ═══════════════════════════════════════════════════════════════
// COLLISION PAIR SYSTEM
// ═══════════════════════════════════════════════════════════════
//
// Each Barrage collision creates a COLLISION PAIR ENTITY with:
//   - FCollisionPair: contact data (entities, point, flags)
//   - Classification tags: determine which systems process it
//
// Flow:
//   1. OnBarrageContact() → creates collision pair entity
//   2. Gameplay systems query pairs by tags and process
//   3. CollisionCleanupSystem destroys all pairs at end of tick
// ═══════════════════════════════════════════════════════════════

/**
 * Collision pair data - created for each Barrage contact event.
 * This is a TRANSIENT entity - exists only for one tick.
 * NOT a USTRUCT - pure Flecs component for performance.
 */
struct FCollisionPair
{
	/** First entity in collision (may be 0 if not Flecs-tracked) */
	uint64 EntityId1 = 0;

	/** Second entity in collision (may be 0 if not Flecs-tracked) */
	uint64 EntityId2 = 0;

	/** Barrage key of first body */
	FSkeletonKey Key1;

	/** Barrage key of second body */
	FSkeletonKey Key2;

	/** World-space contact point */
	FVector ContactPoint = FVector::ZeroVector;

	/** World-space contact normal (from Jolt manifold, axis-swapped) */
	FVector ContactNormal = FVector::ZeroVector;

	/** Pre-collision projectile velocity (UE coords, cm/s). For penetration system. */
	FVector IncomingVelocity = FVector::ZeroVector;

	/** Sub-shape ID of the target body hit (for compound shapes with per-sub-shape materials). 0xFFFFFFFF for simple shapes. */
	uint32 SubShapeID2 = 0xFFFFFFFF;

	/** Was body1 flagged as projectile in Barrage? */
	bool bBody1IsProjectile = false;

	/** Was body2 flagged as projectile in Barrage? */
	bool bBody2IsProjectile = false;

	// Helpers
	bool HasEntity1() const { return EntityId1 != 0; }
	bool HasEntity2() const { return EntityId2 != 0; }
	bool BothEntitiesValid() const { return EntityId1 != 0 && EntityId2 != 0; }

	uint64 GetProjectileEntityId() const
	{
		if (bBody1IsProjectile && EntityId1 != 0) return EntityId1;
		if (bBody2IsProjectile && EntityId2 != 0) return EntityId2;
		return 0;
	}

	uint64 GetTargetEntityId() const
	{
		if (bBody1IsProjectile) return EntityId2;
		if (bBody2IsProjectile) return EntityId1;
		return 0;
	}

	FSkeletonKey GetProjectileKey() const
	{
		return bBody1IsProjectile ? Key1 : Key2;
	}

	FSkeletonKey GetTargetKey() const
	{
		return bBody1IsProjectile ? Key2 : Key1;
	}
};

// ═══════════════════════════════════════════════════════════════
// COLLISION CLASSIFICATION TAGS (zero-size)
// Added to collision pair entities to route them to correct systems.
// ═══════════════════════════════════════════════════════════════

/** Collision involves damage */
struct FTagCollisionDamage {};

/** Collision is item pickup */
struct FTagCollisionPickup {};

/** Collision is projectile bounce */
struct FTagCollisionBounce {};

/** Collision hit destructible */
struct FTagCollisionDestructible {};

/** Collision between two characters */
struct FTagCollisionCharacter {};

/** Collision involves penetrating projectile */
struct FTagCollisionPenetration {};

/** Marker: collision pair has been processed */
struct FTagCollisionProcessed {};
