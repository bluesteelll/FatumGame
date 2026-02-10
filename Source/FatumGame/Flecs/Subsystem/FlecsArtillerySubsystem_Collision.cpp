// FlecsArtillerySubsystem - Collision Handling

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FlecsNiagaraManager.h"

void UFlecsArtillerySubsystem::SubscribeToBarrageEvents()
{
	UBarrageDispatch* Barrage = UBarrageDispatch::SelfPtr;
	if (!Barrage)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: UBarrageDispatch not found, collision events disabled."));
		return;
	}

	ContactEventHandle = Barrage->OnBarrageContactAddedDelegate.AddUObject(
		this, &UFlecsArtillerySubsystem::OnBarrageContact
	);

	UE_LOG(LogTemp, Log, TEXT("FlecsArtillerySubsystem: Subscribed to Barrage collision events."));
}

void UFlecsArtillerySubsystem::OnBarrageContact(const BarrageContactEvent& Event)
{
	if (!FlecsWorld) return;
	if (!CachedBarrageDispatch) return;

	flecs::world& World = *FlecsWorld;

	// ═══════════════════════════════════════════════════════════════
	// STEP 1: Extract physics data from Barrage
	// ═══════════════════════════════════════════════════════════════

	FBLet Body1 = CachedBarrageDispatch->GetShapeRef(Event.ContactEntity1.ContactKey);
	FBLet Body2 = CachedBarrageDispatch->GetShapeRef(Event.ContactEntity2.ContactKey);

	FSkeletonKey Key1 = FBarragePrimitive::IsNotNull(Body1) ? Body1->KeyOutOfBarrage : FSkeletonKey();
	FSkeletonKey Key2 = FBarragePrimitive::IsNotNull(Body2) ? Body2->KeyOutOfBarrage : FSkeletonKey();

	// Skip if both keys are invalid (pure static geometry collision)
	if (!Key1.IsValid() && !Key2.IsValid()) return;

	// Get Flecs entities via LOCK-FREE atomic read - O(1)
	uint64 FlecsId1 = FBarragePrimitive::IsNotNull(Body1) ? Body1->GetFlecsEntity() : 0;
	uint64 FlecsId2 = FBarragePrimitive::IsNotNull(Body2) ? Body2->GetFlecsEntity() : 0;

	bool bBody1IsProjectile = Event.ContactEntity1.bIsProjectile;
	bool bBody2IsProjectile = Event.ContactEntity2.bIsProjectile;

	// ═══════════════════════════════════════════════════════════════
	// STEP 2: Create Collision Pair Entity
	// ═══════════════════════════════════════════════════════════════

	FCollisionPair Pair;
	Pair.EntityId1 = FlecsId1;
	Pair.EntityId2 = FlecsId2;
	Pair.Key1 = Key1;
	Pair.Key2 = Key2;
	Pair.ContactPoint = Event.PointIfAny;
	Pair.bBody1IsProjectile = bBody1IsProjectile;
	Pair.bBody2IsProjectile = bBody2IsProjectile;

	flecs::entity PairEntity = World.entity()
		.set<FCollisionPair>(Pair);

	// ═══════════════════════════════════════════════════════════════
	// STEP 3: Classify collision and add appropriate tags
	// Classification determines which systems will process this pair.
	// ═══════════════════════════════════════════════════════════════

	// Get Flecs entities for classification (if they exist)
	flecs::entity Entity1 = FlecsId1 != 0 ? World.entity(FlecsId1) : flecs::entity();
	flecs::entity Entity2 = FlecsId2 != 0 ? World.entity(FlecsId2) : flecs::entity();

	bool bEntity1Valid = Entity1.is_valid() && Entity1.is_alive();
	bool bEntity2Valid = Entity2.is_valid() && Entity2.is_alive();

	// Check entity capabilities (using new Static/Instance components)
	bool bEntity1HasHealth = bEntity1Valid && Entity1.has<FHealthInstance>();
	bool bEntity2HasHealth = bEntity2Valid && Entity2.has<FHealthInstance>();
	bool bEntity1HasDamage = bEntity1Valid && Entity1.has<FDamageStatic>();
	bool bEntity2HasDamage = bEntity2Valid && Entity2.has<FDamageStatic>();
	bool bEntity1IsCharacter = bEntity1Valid && Entity1.has<FTagCharacter>();
	bool bEntity2IsCharacter = bEntity2Valid && Entity2.has<FTagCharacter>();
	bool bEntity1IsPickupable = bEntity1Valid && Entity1.has<FTagPickupable>() && Entity1.has<FTagItem>();
	bool bEntity2IsPickupable = bEntity2Valid && Entity2.has<FTagPickupable>() && Entity2.has<FTagItem>();
	bool bEntity1IsDestructible = bEntity1Valid && Entity1.has<FTagDestructible>();
	bool bEntity2IsDestructible = bEntity2Valid && Entity2.has<FTagDestructible>();
	bool bEntity1IsProjectile = bEntity1Valid && Entity1.has<FTagProjectile>();
	bool bEntity2IsProjectile = bEntity2Valid && Entity2.has<FTagProjectile>();

	// ─────────────────────────────────────────────────────────
	// DAMAGE CLASSIFICATION
	// Projectile/DamageSource hits entity with Health
	// ─────────────────────────────────────────────────────────
	bool bIsDamageCollision = false;

	// Flecs projectile with FDamageStatic hits target with FHealthInstance
	if ((bEntity1HasDamage && bEntity2HasHealth) || (bEntity2HasDamage && bEntity1HasHealth))
	{
		bIsDamageCollision = true;
	}
	// Physics projectile hits Flecs target with health.
	// REQUIRE valid Flecs entity (FlecsId != 0): if PHASE2 hasn't run yet,
	// we can't check OwnerEntityId (self-damage) or get FDamageStatic (damage amount).
	// By the time PHASE2 creates the entity, the projectile has left the owner's collision volume.
	else if (bBody1IsProjectile && bEntity2HasHealth && FlecsId1 != 0)
	{
		bIsDamageCollision = true;
	}
	else if (bBody2IsProjectile && bEntity1HasHealth && FlecsId2 != 0)
	{
		bIsDamageCollision = true;
	}

	if (bIsDamageCollision)
	{
		PairEntity.add<FTagCollisionDamage>();
	}

	// ─────────────────────────────────────────────────────────
	// BOUNCE CLASSIFICATION
	// Flecs projectile collides with anything (for grace period reset)
	// ─────────────────────────────────────────────────────────
	if (bEntity1IsProjectile || bEntity2IsProjectile)
	{
		PairEntity.add<FTagCollisionBounce>();
	}

	// ─────────────────────────────────────────────────────────
	// PICKUP CLASSIFICATION
	// Character touches pickupable item
	// ─────────────────────────────────────────────────────────
	if ((bEntity1IsCharacter && bEntity2IsPickupable) || (bEntity2IsCharacter && bEntity1IsPickupable))
	{
		PairEntity.add<FTagCollisionPickup>();
	}

	// ─────────────────────────────────────────────────────────
	// DESTRUCTIBLE CLASSIFICATION
	// Projectile or damage source hits destructible
	// ─────────────────────────────────────────────────────────
	if ((bBody1IsProjectile || bEntity1HasDamage) && bEntity2IsDestructible)
	{
		PairEntity.add<FTagCollisionDestructible>();
	}
	else if ((bBody2IsProjectile || bEntity2HasDamage) && bEntity1IsDestructible)
	{
		PairEntity.add<FTagCollisionDestructible>();
	}

	// ─────────────────────────────────────────────────────────
	// CHARACTER COLLISION CLASSIFICATION
	// Two characters collide (for future: knockback, etc)
	// ─────────────────────────────────────────────────────────
	if (bEntity1IsCharacter && bEntity2IsCharacter)
	{
		PairEntity.add<FTagCollisionCharacter>();
	}

	// ─────────────────────────────────────────────────────────
	// KILL NON-BOUNCING PROJECTILES ON ANY CONTACT
	// MaxBounces=0 projectiles die on first contact (wall, floor, entity).
	// Bypasses BounceCollisionSystem grace period entirely.
	// Stores contact point for accurate death VFX position
	// (physics body position is WRONG — StepWorld already bounced it).
	// ─────────────────────────────────────────────────────────
	FVector ContactPoint = Event.PointIfAny;

	auto TryKillNonBouncingProjectile = [&World, ContactPoint](uint64 ProjEntityId, uint64 OtherEntityId)
	{
		if (ProjEntityId == 0) return;

		flecs::entity ProjEntity = World.entity(ProjEntityId);
		if (!ProjEntity.is_valid() || !ProjEntity.is_alive() || ProjEntity.has<FTagDead>()) return;
		if (!ProjEntity.has<FTagProjectile>()) return;

		const FProjectileStatic* ProjStatic = ProjEntity.try_get<FProjectileStatic>();
		const int32 MaxBounces = ProjStatic ? ProjStatic->MaxBounces : 0;

		// Bouncing projectiles (MaxBounces != 0) — let BounceCollisionSystem handle
		if (MaxBounces != 0) return;

		// Don't kill if hitting own owner (projectile should pass through)
		if (OtherEntityId != 0)
		{
			const FProjectileInstance* ProjInst = ProjEntity.try_get<FProjectileInstance>();
			if (ProjInst && ProjInst->OwnerEntityId != 0
				&& static_cast<uint64>(ProjInst->OwnerEntityId) == OtherEntityId)
			{
				return;
			}
		}

		// Store contact point for death VFX (physics position is post-bounce = wrong)
		FDeathContactPoint DCP;
		DCP.Position = ContactPoint;
		ProjEntity.set<FDeathContactPoint>(DCP);

		ProjEntity.add<FTagDead>();
		UE_LOG(LogTemp, Log, TEXT("COLLISION: Projectile %llu killed on contact (MaxBounces=0) at (%.0f,%.0f,%.0f)"),
			ProjEntityId, ContactPoint.X, ContactPoint.Y, ContactPoint.Z);
	};

	// Use physics-layer flag (bBodyIsProjectile) OR Flecs tag — physics layer is
	// always set (even before Flecs entity is fully committed in multi-threaded progress).
	if (bEntity1IsProjectile || (bBody1IsProjectile && FlecsId1 != 0))
		TryKillNonBouncingProjectile(FlecsId1, FlecsId2);
	if (bEntity2IsProjectile || (bBody2IsProjectile && FlecsId2 != 0))
		TryKillNonBouncingProjectile(FlecsId2, FlecsId1);

	// Log collision pair creation (verbose)
	UE_LOG(LogTemp, Verbose, TEXT("COLLISION PAIR: E1=%llu E2=%llu Damage=%d Bounce=%d Pickup=%d Destr=%d"),
		FlecsId1, FlecsId2,
		PairEntity.has<FTagCollisionDamage>(),
		PairEntity.has<FTagCollisionBounce>(),
		PairEntity.has<FTagCollisionPickup>(),
		PairEntity.has<FTagCollisionDestructible>());
}
