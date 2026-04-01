// FlecsArtillerySubsystem - Fragmentation Systems
// ConstraintBreakSystem, FragmentationSystem

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"
#include "FlecsDestructibleComponents.h"
#include "FlecsHealthComponents.h"
#include "FBarragePrimitive.h"
#include "BarrageDispatch.h"
#include "BarrageConstraintSystem.h"
#include "FlecsRenderManager.h"
#include "FlecsNiagaraManager.h"
#include "FlecsNiagaraProfile.h"
#include "FlecsDestructibleProfile.h"
#include "FlecsDestructibleGeometry.h"
#include "FlecsEntityDefinition.h"
#include "FlecsDamageProfile.h"
#include "FlecsHealthProfile.h"
#include "FlecsRenderProfile.h"
#include "FBShapeParams.h"
#include "BarrageSpawnUtils.h"
#include "Skeletonize.h"
#include "FBConstraintParams.h"
#include "FlecsDoorComponents.h"
#include "FlecsPenetrationComponents.h"
#include "FlecsPhysicsProfile.h"

// ═══════════════════════════════════════════════════════════════
// FRAGMENT ENTITY (shared fragmentation logic)
// Called by FragmentationSystem (collision) and PendingFragmentationSystem (explosion).
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::FragmentEntity(
	flecs::entity TargetEntity, FSkeletonKey TargetKey,
	const FVector& ImpactPoint, const FVector& ImpactDirection, float ImpactImpulse)
{
	EnsureBarrageAccess();

	flecs::world& World = *FlecsWorld;

	// Use try_get_mut so we can immediately invalidate the component to prevent
	// duplicate fragmentation from multiple contact events in the same tick.
	// (Flecs deferred add<FTagDead> is NOT visible to other iterations of each(),
	//  but direct writes to committed storage via try_get_mut ARE visible.)
	FDestructibleStatic* DestrStatic = TargetEntity.try_get_mut<FDestructibleStatic>();
	if (!DestrStatic || !DestrStatic->IsValid())
	{
		return;
	}

	UFlecsDestructibleProfile* Profile = DestrStatic->Profile;

	// Immediately invalidate to block duplicate fragmentation this tick
	DestrStatic->Profile = nullptr;
	UFlecsDestructibleGeometry* Geometry = Profile->Geometry;
	if (!Geometry || Geometry->Fragments.Num() == 0)
	{
		return;
	}

	// Get the intact object's world transform from Barrage
	FVector ObjectPosition = FVector::ZeroVector;
	FQuat ObjectRotation = FQuat::Identity;
	if (CachedBarrageDispatch && TargetKey.IsValid())
	{
		FBLet TargetPrim = CachedBarrageDispatch->GetShapeRef(TargetKey);
		if (FBarragePrimitive::IsNotNull(TargetPrim))
		{
			ObjectPosition = FVector(FBarragePrimitive::GetPosition(TargetPrim));
			ObjectRotation = FQuat(FBarragePrimitive::OptimisticGetAbsoluteRotation(TargetPrim));

			// Immediately disable intact body collision — can't wait for
			// DeadEntityCleanupSystem because deferred add<FTagDead>() isn't
			// visible to it until next tick. Without this, fragment bodies
			// (MOVING) collide with the intact body (NON_MOVING for static
			// objects) on the next StepWorld → explosion.
			CachedBarrageDispatch->SetBodyObjectLayer(TargetPrim->KeyIntoBarrage, Layers::DEBRIS);
		}
	}
	FTransform ObjectTransform(ObjectRotation, ObjectPosition);

	// ─────────────────────────────────────────────────────
	// Kill the intact object (ISM remove, DEBRIS layer, tombstone)
	// ─────────────────────────────────────────────────────
	TargetEntity.add<FTagDead>();

	UE_LOG(LogTemp, Log, TEXT("FRAGMENTATION: Destroying intact object %llu, spawning %d fragments at (%.0f,%.0f,%.0f)"),
		TargetEntity.id(), Geometry->Fragments.Num(), ObjectPosition.X, ObjectPosition.Y, ObjectPosition.Z);

	// ─────────────────────────────────────────────────────
	// Spawn fragments
	// ─────────────────────────────────────────────────────
	// Arrays parallel to Geometry->Fragments — same indices.
	// Invalid entries for failed acquires or missing meshes.
	const int32 FragCount = Geometry->Fragments.Num();
	TArray<FSkeletonKey> FragmentKeys;
	TArray<flecs::entity> FragmentEntities;
	FragmentKeys.SetNumZeroed(FragCount);
	FragmentEntities.SetNum(FragCount);

	// World anchor mode: find ALL bottom-layer fragments (lowest Z within tolerance).
	// Each gets a breakable Fixed constraint to Body::sFixedToWorld.
	const bool bAnchorToWorld = Profile->bAnchorToWorld;
	TArray<int32> AnchorIndices;
	if (bAnchorToWorld)
	{
		// Find minimum Z across all fragments
		float LowestZ = TNumericLimits<float>::Max();
		for (int32 i = 0; i < FragCount; ++i)
		{
			const FDestructibleFragment& Frag = Geometry->Fragments[i];
			if (!Frag.Mesh) continue;
			const float FragZ = (Frag.RelativeTransform * ObjectTransform).GetLocation().Z;
			if (FragZ < LowestZ) LowestZ = FragZ;
		}
		// All fragments within 1cm of lowest Z are "bottom layer"
		constexpr float AnchorZTolerance = 1.0f;
		for (int32 i = 0; i < FragCount; ++i)
		{
			const FDestructibleFragment& Frag = Geometry->Fragments[i];
			if (!Frag.Mesh) continue;
			const float FragZ = (Frag.RelativeTransform * ObjectTransform).GetLocation().Z;
			if (FragZ <= LowestZ + AnchorZTolerance)
			{
				AnchorIndices.Add(i);
			}
		}
	}

	// ─────────────────────────────────────────────────────
	// Pre-compute deferred impulse for the nearest fragment.
	// Done BEFORE the entity creation loop so we can set PendingImpulse
	// on FDebrisInstance during construction (avoids deferred ops issue).
	// ─────────────────────────────────────────────────────
	int32 ImpulseFragIdx = INDEX_NONE;
	FVector DeferredImpulse = FVector::ZeroVector;
	if (ImpactImpulse > 0.f)
	{
		const FVector ImpulseDir = ImpactDirection.IsNearlyZero()
			? FVector::UpVector
			: ImpactDirection;
		DeferredImpulse = ImpulseDir * (ImpactImpulse * Profile->ImpulseMultiplier);
		UE_LOG(LogTemp, Log, TEXT("FRAG_IMPULSE: ImpactImpulse=%.1f × ProfileMult=%.2f = DeferredImpulse=(%.1f,%.1f,%.1f)"),
			ImpactImpulse, Profile->ImpulseMultiplier, DeferredImpulse.X, DeferredImpulse.Y, DeferredImpulse.Z);

		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 i = 0; i < FragCount; ++i)
		{
			if (!Geometry->Fragments[i].Mesh) continue;
			const float DistSq = FVector::DistSquared(
				(Geometry->Fragments[i].RelativeTransform * ObjectTransform).GetLocation(),
				ImpactPoint);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				ImpulseFragIdx = i;
			}
		}
	}

	for (int32 FragIdx = 0; FragIdx < FragCount; ++FragIdx)
	{
		const FDestructibleFragment& Fragment = Geometry->Fragments[FragIdx];
		if (!Fragment.Mesh) continue;

		// Compute world transform for this fragment
		FTransform FragWorldTransform = Fragment.RelativeTransform * ObjectTransform;
		FVector FragPos = FragWorldTransform.GetLocation();
		FQuat FragRot = FragWorldTransform.GetRotation();

		// Create per-fragment physics body with collider sized from mesh bounds
		FSkeletonKey FragKey = FBarrageSpawnUtils::GenerateUniqueKey();
		const FVector& HE = Fragment.ColliderHalfExtents;
		FBBoxParams BoxParams = FBarrageBounder::GenerateBoxBounds(
			FragPos,
			HE.X * 2.0, HE.Y * 2.0, HE.Z * 2.0,
			FVector3d::ZeroVector,
			FMassByCategory::MostEnemies
		);
		BoxParams.AllowedDOFs = 0x3F; // All DOFs — bypass MOVING layer RotationY-only restriction

		FBLet FragPrim = CachedBarrageDispatch->CreatePrimitive(
			BoxParams, FragKey, Layers::MOVING,
			false,  // not sensor
			true,   // force dynamic
			true,   // movable
			0.4f,   // friction
			0.2f,   // restitution
			0.1f    // linear damping
		);

		if (!FBarragePrimitive::IsNotNull(FragPrim))
		{
			UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION: Failed to create body for fragment %d"), FragIdx);
			continue;
		}

		// Set rotation (CreatePrimitive uses position from BoxParams but not rotation)
		CachedBarrageDispatch->SetBodyRotationDirect(FragPrim->KeyIntoBarrage, FragRot);

		// Set constrained mass (high → minimal jitter while structure is intact).
		// FreeMassKg is stored in FDebrisInstance so ConstraintBreakSystem can
		// restore it when the last constraint on this fragment breaks.
		CachedBarrageDispatch->SetBodyMass(FragPrim->KeyIntoBarrage, Profile->ConstrainedMassKg);
		FBarragePrimitive::SetGravityFactor(1.0f, FragPrim);

		// Create Flecs entity for this fragment
		flecs::entity FragEntity = World.entity();

		FBarrageBody BarrageComp;
		BarrageComp.BarrageKey = FragKey;
		FragEntity.set<FBarrageBody>(BarrageComp);

		// Reverse binding
		FragPrim->SetFlecsEntity(FragEntity.id());

		// Debris instance data
		FDebrisInstance DebrisInst;
		DebrisInst.LifetimeRemaining = Profile->DebrisLifetime;
		DebrisInst.bAutoDestroy = Profile->bAutoDestroyDebris;
		DebrisInst.PoolSlotIndex = INDEX_NONE; // Not pooled — cleanup uses tombstone
		DebrisInst.FreeMassKg = Profile->FragmentMassKg;
		DebrisInst.bInAnchoredStructure = bAnchorToWorld;
		if (FragIdx == ImpulseFragIdx)
		{
			DebrisInst.PendingImpulse = DeferredImpulse;
			UE_LOG(LogTemp, Log, TEXT("FRAG_IMPULSE: Assigned to fragment %d (entity %llu) PendingImpulse=(%.1f,%.1f,%.1f)"),
				FragIdx, FragEntity.id(), DeferredImpulse.X, DeferredImpulse.Y, DeferredImpulse.Z);
		}
		FragEntity.set<FDebrisInstance>(DebrisInst);

		FragEntity.add<FTagDebrisFragment>();

		// ISM render data
		FISMRender Render;
		Render.Mesh = Fragment.Mesh;
		Render.Scale = FragWorldTransform.GetScale3D();
		FragEntity.set<FISMRender>(Render);

		// Determine fragment definition (per-fragment override or default)
		UFlecsEntityDefinition* FragDef = Fragment.OverrideDefinition
			? Fragment.OverrideDefinition
			: Profile->DefaultFragmentDefinition;

		// Apply profiles from fragment definition (if any)
		if (FragDef)
		{
			if (FragDef->DamageProfile)
				FragEntity.set<FDamageStatic>(FDamageStatic::FromProfile(FragDef->DamageProfile));

			if (FragDef->HealthProfile)
			{
				FragEntity.set<FHealthStatic>(FHealthStatic::FromProfile(FragDef->HealthProfile));

				FHealthInstance HealthInst;
				HealthInst.CurrentHP = FragDef->HealthProfile->GetStartingHealth();
				FragEntity.set<FHealthInstance>(HealthInst);
			}

			// Penetration material — each fragment can have its own resistance
			if (FragDef->PhysicsProfile && FragDef->PhysicsProfile->MaterialResistance > 0.f)
				FragEntity.set<FPenetrationMaterial>(FPenetrationMaterial::FromProfile(FragDef->PhysicsProfile));
		}

		// Queue ISM spawn for game thread
		FPendingFragmentSpawn ISMSpawn;
		ISMSpawn.EntityKey = FragKey;
		ISMSpawn.Mesh = Fragment.Mesh;
		ISMSpawn.Material = (FragDef && FragDef->RenderProfile)
			? FragDef->RenderProfile->MaterialOverride
			: nullptr;
		ISMSpawn.WorldTransform = FragWorldTransform;
		PendingFragmentSpawns.Enqueue(ISMSpawn);

		// Niagara attached effects on fragments
		if (FragDef && FragDef->NiagaraProfile && FragDef->NiagaraProfile->HasAttachedEffect())
		{
			// Queue Niagara registration via pending spawn system
			// (NiagaraManager handles this when ISM is created on game thread)
		}

		// Death VFX on fragment death
		if (FragDef && FragDef->NiagaraProfile && FragDef->NiagaraProfile->HasDeathEffect())
		{
			FNiagaraDeathEffect DeathVFX;
			DeathVFX.Effect = FragDef->NiagaraProfile->DeathEffect;
			DeathVFX.Scale = FragDef->NiagaraProfile->DeathEffectScale;
			FragEntity.set<FNiagaraDeathEffect>(DeathVFX);
		}

		FragmentKeys[FragIdx] = FragKey;
		FragmentEntities[FragIdx] = FragEntity;
	}

	// ─────────────────────────────────────────────────────
	// Create constraints from adjacency graph
	// ─────────────────────────────────────────────────────
	FBarrageConstraintSystem* ConstraintSys = CachedBarrageDispatch
		? CachedBarrageDispatch->GetConstraintSystem()
		: nullptr;

	// ── Accumulate constraint data locally to avoid Flecs deferred ops bug ──
	// obtain<T>() in .run() deferred mode overwrites previous staged data
	// for the same entity. We must build complete FFlecsConstraintData per
	// fragment BEFORE calling .set<>() once per entity.
	TMap<int32, FFlecsConstraintData> FragConstraintData; // fragment index → accumulated constraints

	if (ConstraintSys && Geometry->AdjacencyLinks.Num() > 0)
	{
		int32 ConstraintsCreated = 0;

		for (const FFragmentAdjacency& Link : Geometry->AdjacencyLinks)
		{
			if (Link.FragmentIndexA >= FragmentKeys.Num() || Link.FragmentIndexB >= FragmentKeys.Num())
				continue;

			FSkeletonKey KeyA = FragmentKeys[Link.FragmentIndexA];
			FSkeletonKey KeyB = FragmentKeys[Link.FragmentIndexB];
			if (!KeyA.IsValid() || !KeyB.IsValid()) continue;

			FBLet PrimA = CachedBarrageDispatch->GetShapeRef(KeyA);
			FBLet PrimB = CachedBarrageDispatch->GetShapeRef(KeyB);
			if (!FBarragePrimitive::IsNotNull(PrimA) || !FBarragePrimitive::IsNotNull(PrimB)) continue;

			FBarrageKey BodyA = PrimA->KeyIntoBarrage;
			FBarrageKey BodyB = PrimB->KeyIntoBarrage;
			if (BodyA.KeyIntoBarrage == 0 || BodyB.KeyIntoBarrage == 0) continue;

			FBarrageConstraintKey CKey = CachedBarrageDispatch->CreateFixedConstraint(
				BodyA, BodyB, Profile->ConstraintBreakForce, Profile->ConstraintBreakTorque);

			if (CKey.IsValid())
			{
				++ConstraintsCreated;
				FragConstraintData.FindOrAdd(Link.FragmentIndexA)
					.AddConstraint(CKey.Key, KeyB, Profile->ConstraintBreakForce, Profile->ConstraintBreakTorque);
				FragConstraintData.FindOrAdd(Link.FragmentIndexB)
					.AddConstraint(CKey.Key, KeyA, Profile->ConstraintBreakForce, Profile->ConstraintBreakTorque);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION: %d/%d fragment constraints created"),
			ConstraintsCreated, Geometry->AdjacencyLinks.Num());
	}

	// ─────────────────────────────────────────────────────
	// World anchor constraints — pin bottom fragments to world
	// ─────────────────────────────────────────────────────
	if (bAnchorToWorld && AnchorIndices.Num() > 0 && ConstraintSys)
	{
		int32 WorldConstraintsCreated = 0;
		const FBarrageKey InvalidWorldBody; // KeyIntoBarrage == 0 → Body::sFixedToWorld

		for (int32 AnchorFragIdx : AnchorIndices)
		{
			FSkeletonKey FragKey = FragmentKeys[AnchorFragIdx];
			if (!FragKey.IsValid()) continue;

			FBLet FragPrim = CachedBarrageDispatch->GetShapeRef(FragKey);
			if (!FBarragePrimitive::IsNotNull(FragPrim)) continue;

			FBarrageConstraintKey CKey = CachedBarrageDispatch->CreateFixedConstraint(
				FragPrim->KeyIntoBarrage, InvalidWorldBody,
				Profile->AnchorBreakForce, Profile->AnchorBreakTorque);

			if (CKey.IsValid())
			{
				++WorldConstraintsCreated;
				FragConstraintData.FindOrAdd(AnchorFragIdx)
					.AddConstraint(CKey.Key, FSkeletonKey(), Profile->AnchorBreakForce, Profile->AnchorBreakTorque);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("FRAGMENTATION: Created %d world anchor constraints for %d bottom fragments (BreakForce=%.0f)"),
			WorldConstraintsCreated, AnchorIndices.Num(), Profile->AnchorBreakForce);
	}

	// ── Single .set<>() per entity with complete constraint data ──
	for (auto& [FragIdx, Data] : FragConstraintData)
	{
		if (FragIdx < FragmentEntities.Num() && FragmentEntities[FragIdx].is_valid())
		{
			FragmentEntities[FragIdx].set<FFlecsConstraintData>(Data);
			FragmentEntities[FragIdx].add<FTagConstrained>();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION: Constraint data set on %d fragments"), FragConstraintData.Num());
}

void UFlecsArtillerySubsystem::SetupFragmentationSystems()
{
	flecs::world& World = *FlecsWorld;

	// ═══════════════════════════════════════════════════════════════
	// CONSTRAINT BREAK SYSTEM (runs FIRST — before gameplay systems)
	// Calls Barrage ProcessBreakableConstraints(), removes broken constraints
	// from FFlecsConstraintData on linked entities.
	// ═══════════════════════════════════════════════════════════════

	World.system<>("ConstraintBreakSystem")
		.kind(flecs::OnUpdate)
		.run([this](flecs::iter& It)
		{
			EnsureBarrageAccess();

			FBarrageConstraintSystem* ConstraintSys = CachedBarrageDispatch
				? CachedBarrageDispatch->GetConstraintSystem()
				: nullptr;
			if (!ConstraintSys) return;

			TArray<FBarrageConstraintKey> BrokenConstraints;
			int32 BrokenCount = ConstraintSys->ProcessBreakableConstraints(&BrokenConstraints);
			if (BrokenCount == 0) return;

			int32 Remaining = ConstraintSys->GetConstraintCount();
			UE_LOG(LogTemp, Warning, TEXT("CONSTRAINT_BREAK: %d constraints BROKEN this tick (remaining: %d)"),
				BrokenCount, Remaining);

			flecs::world& World = *FlecsWorld;
			auto* CachedDispatch = CachedBarrageDispatch;

			// ─────────────────────────────────────────────────────
			// Helper: restore FreeMassKg + apply PendingImpulse on a fragment.
			// ─────────────────────────────────────────────────────
			auto RestoreFragment = [CachedDispatch](flecs::entity Entity)
			{
				const FDebrisInstance* Debris = Entity.try_get<FDebrisInstance>();
				const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
				if (!Debris || Debris->FreeMassKg <= 0.f || !Body || !Body->BarrageKey.IsValid() || !CachedDispatch)
					return;

				FBLet Prim = CachedDispatch->GetShapeRef(Body->BarrageKey);
				if (!FBarragePrimitive::IsNotNull(Prim)) return;

				CachedDispatch->SetBodyMass(Prim->KeyIntoBarrage, Debris->FreeMassKg);

				if (!Debris->PendingImpulse.IsNearlyZero())
				{
					CachedDispatch->AddBodyImpulse(Prim->KeyIntoBarrage, Debris->PendingImpulse);
				}
			};

			// ─────────────────────────────────────────────────────
			// Pass 1: Remove broken constraint refs, collect affected entities.
			// ─────────────────────────────────────────────────────
			TArray<flecs::entity> FullyFreed;      // Lost ALL constraints
			TArray<flecs::entity> PartiallyFreed;  // Lost some, still has others

			World.each([&](flecs::entity Entity, FFlecsConstraintData& Data)
			{
				bool bChanged = false;
				for (const FBarrageConstraintKey& BrokenKey : BrokenConstraints)
				{
					if (Data.RemoveConstraint(BrokenKey.Key))
						bChanged = true;
				}

				if (!bChanged) return;

				if (!Data.HasConstraints())
					FullyFreed.Add(Entity);
				else
					PartiallyFreed.Add(Entity);
			});

			UE_LOG(LogTemp, Warning, TEXT("CONSTRAINT_BREAK: FullyFreed=%d PartiallyFreed=%d"),
				FullyFreed.Num(), PartiallyFreed.Num());

			// Handle fully freed fragments (no constraints left at all)
			for (flecs::entity Entity : FullyFreed)
			{
				Entity.remove<FTagConstrained>();
				RestoreFragment(Entity);
			}

			// ─────────────────────────────────────────────────────
			// Pass 2: BFS from partially-freed anchored fragments to detect
			// groups disconnected from world anchors.
			// Only relevant for bInAnchoredStructure fragments.
			// ─────────────────────────────────────────────────────
			TSet<uint64> Visited;

			// Pre-mark fully freed entities so BFS doesn't traverse through them
			for (flecs::entity Entity : FullyFreed)
				Visited.Add(Entity.id());

			for (flecs::entity StartEntity : PartiallyFreed)
			{
				if (Visited.Contains(StartEntity.id())) continue;

				// Only run BFS for anchored structures
				const FDebrisInstance* StartDebris = StartEntity.try_get<FDebrisInstance>();
				if (!StartDebris || !StartDebris->bInAnchoredStructure)
				{
					UE_LOG(LogTemp, Warning, TEXT("CONSTRAINT_BREAK: BFS skipped — bInAnchoredStructure=%d"),
						StartDebris ? StartDebris->bInAnchoredStructure : -1);
					continue;
				}

				// BFS through constraint graph
				TArray<flecs::entity> Component;
				TArray<flecs::entity> Queue;
				bool bHasAnchor = false;
				int32 ResolvedNeighbors = 0;
				int32 FailedResolves = 0;

				Queue.Add(StartEntity);
				Visited.Add(StartEntity.id());

				while (Queue.Num() > 0)
				{
					flecs::entity Current = Queue.Pop();
					Component.Add(Current);

					const FFlecsConstraintData* Data = Current.try_get<FFlecsConstraintData>();
					if (!Data) continue;

					for (const FConstraintLink& Link : Data->Constraints)
					{
						if (!Link.OtherEntityKey.IsValid())
						{
							// World anchor link — this component is still grounded
							bHasAnchor = true;
							continue;
						}

						// Resolve neighbor: SkeletonKey → FBarragePrimitive → Flecs entity
						FBLet Prim = CachedDispatch->GetShapeRef(Link.OtherEntityKey);
						if (!FBarragePrimitive::IsNotNull(Prim))
						{
							++FailedResolves;
							continue;
						}

						uint64 NeighborId = Prim->GetFlecsEntity();
						if (NeighborId == 0 || Visited.Contains(NeighborId)) continue;

						flecs::entity Neighbor = World.entity(NeighborId);
						if (!Neighbor.is_valid()) continue;

						++ResolvedNeighbors;
						Visited.Add(NeighborId);
						Queue.Add(Neighbor);
					}
				}

				UE_LOG(LogTemp, Warning, TEXT("CONSTRAINT_BREAK: BFS component=%d hasAnchor=%d resolved=%d failed=%d"),
					Component.Num(), bHasAnchor, ResolvedNeighbors, FailedResolves);

				if (!bHasAnchor)
				{
					// Entire group disconnected from world — restore mass for all
					for (flecs::entity Entity : Component)
					{
						RestoreFragment(Entity);
					}
					UE_LOG(LogTemp, Warning, TEXT("CONSTRAINT_BREAK: === DISCONNECTED GROUP of %d fragments — MASS RESTORED ==="),
						Component.Num());
				}
			}

			// ─────────────────────────────────────────────────────
			// Pass 3: Door constraint break detection.
			// If a broken constraint belongs to a door entity, mark it dead.
			// DeadEntityCleanupSystem handles ISM removal + tombstone.
			// ─────────────────────────────────────────────────────
			{
				TSet<int64> BrokenKeySet;
				for (const FBarrageConstraintKey& BK : BrokenConstraints)
					BrokenKeySet.Add(BK.Key);

				World.each([&](flecs::entity DoorEntity, FDoorInstance& Door)
				{
					if (Door.HasConstraint() && BrokenKeySet.Contains(Door.ConstraintKey))
					{
						Door.ConstraintKey = 0;
						DoorEntity.add<FTagDead>();
						UE_LOG(LogTemp, Warning, TEXT("DOOR BREAK: Entity %llu hinge broken off!"), DoorEntity.id());
					}
				});
			}
		});

	// ─────────────────────────────────────────────────────────
	// FRAGMENTATION SYSTEM
	// Processes FTagCollisionFragmentation pairs.
	// Destroys the intact object and spawns pre-baked fragment bodies
	// connected by fixed constraints via FragmentEntity().
	// ─────────────────────────────────────────────────────────
	// NOTE: No .without<FTagCollisionProcessed>() — fragmentation co-exists with damage.
	// A projectile hitting a fragmentable object triggers BOTH damage and fragmentation.
	World.system<const FCollisionPair, const FFragmentationData>("FragmentationSystem")
		.with<FTagCollisionFragmentation>()
		.each([this, &World](flecs::entity PairEntity, const FCollisionPair& Pair, const FFragmentationData& FragData)
		{
			EnsureBarrageAccess();

			// Determine which entity is the destructible target
			uint64 TargetId = 0;
			FSkeletonKey TargetKey;
			if (Pair.HasEntity2())
			{
				flecs::entity E2 = World.entity(Pair.EntityId2);
				if (E2.is_valid() && E2.is_alive() && !E2.has<FTagDead>())
				{
					const FDestructibleStatic* Destr = E2.try_get<FDestructibleStatic>();
					if (Destr && Destr->IsValid())
					{
						TargetId = Pair.EntityId2;
						TargetKey = Pair.Key2;
					}
				}
			}
			if (TargetId == 0 && Pair.HasEntity1())
			{
				flecs::entity E1 = World.entity(Pair.EntityId1);
				if (E1.is_valid() && E1.is_alive() && !E1.has<FTagDead>())
				{
					const FDestructibleStatic* Destr = E1.try_get<FDestructibleStatic>();
					if (Destr && Destr->IsValid())
					{
						TargetId = Pair.EntityId1;
						TargetKey = Pair.Key1;
					}
				}
			}

			if (TargetId == 0)
			{
				PairEntity.add<FTagCollisionProcessed>();
				return;
			}

			flecs::entity TargetEntity = World.entity(TargetId);
			if (!TargetEntity.is_valid() || !TargetEntity.is_alive() || TargetEntity.has<FTagDead>())
			{
				PairEntity.add<FTagCollisionProcessed>();
				return;
			}

			FragmentEntity(TargetEntity, TargetKey, FragData.ImpactPoint, FragData.ImpactDirection, FragData.ImpactImpulse);
			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// PENDING FRAGMENTATION SYSTEM
	// Processes FPendingFragmentation set directly on destructible entities
	// by explosions, abilities, or scripts. Runs after FragmentationSystem
	// so collision-triggered fragmentation takes priority.
	// ─────────────────────────────────────────────────────────
	World.system<const FPendingFragmentation, const FBarrageBody>("PendingFragmentationSystem")
		.without<FTagDead>()
		.each([this](flecs::entity Entity, const FPendingFragmentation& Pending, const FBarrageBody& Body)
		{
			FragmentEntity(Entity, Body.BarrageKey, Pending.ImpactPoint, Pending.ImpactDirection, Pending.ImpactImpulse);
			Entity.remove<FPendingFragmentation>();
		});
}
