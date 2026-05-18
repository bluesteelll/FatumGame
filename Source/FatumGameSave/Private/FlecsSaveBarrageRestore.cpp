// FlecsSaveBarrageRestore — implementation. Sim-thread only.

#include "FlecsSaveBarrageRestore.h"

#include "FlecsSaveLog.h"

#include "FlecsBarrageComponents.h"     // FBarrageBody
#include "FlecsArtillerySubsystem.h"     // BindEntityToBarrage, UnbindEntityFromBarrage
#include "FlecsEntityComponents.h"       // FEntityDefinitionRef
#include "FlecsEntityDefinition.h"
#include "FlecsPhysicsProfile.h"
#include "FlecsProjectileProfile.h"

#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "BarrageSpawnUtils.h"   // GenerateUniqueKey
#include "EPhysicsLayer.h"
#include "Skeletonize.h"          // SKELLY::SFIX_GUN_SHOT

#include "flecs.h"

namespace FlecsSaveBarrage
{
	bool EncodeBarrageBodyState(
		const flecs::entity& E,
		UBarrageDispatch* Barrage,
		FBarrageBodyState& Out)
	{
		check(Barrage);

		const FBarrageBody* Body = E.try_get<FBarrageBody>();
		if (!Body || !Body->IsValid())
		{
			return false;
		}

		// Pool-body-safe lookup: use GetShapeRef (NOT GetBarrageKeyFromSkeletonKey, which
		// fails for pool bodies — per MEMORY.md).
		FBLet Prim = Barrage->GetShapeRef(Body->BarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim))
		{
			return false;
		}

		// Position + rotation: live from the Jolt body.
		const FVector3f Pos = FBarragePrimitive::GetPosition(Prim);
		const FQuat4f Rot   = FBarragePrimitive::OptimisticGetAbsoluteRotation(Prim);

		Out.PosX = Pos.X; Out.PosY = Pos.Y; Out.PosZ = Pos.Z;
		Out.RotX = Rot.X; Out.RotY = Rot.Y; Out.RotZ = Rot.Z; Out.RotW = Rot.W;

		// Linear velocity (UE coords from FBarragePrimitive).
		const FVector3f LinVel = FBarragePrimitive::GetVelocity(Prim);
		Out.LinVelX = LinVel.X; Out.LinVelY = LinVel.Y; Out.LinVelZ = LinVel.Z;

		// Angular velocity is already UE-converted by FBarragePrimitive::GetAngularVelocity.
		const FVector AngVel = FBarragePrimitive::GetAngularVelocity(Prim);
		Out.AngVelX = AngVel.X; Out.AngVelY = AngVel.Y; Out.AngVelZ = AngVel.Z;

		return true;
	}

	bool RestoreBarrageBodyForEntity(
		const flecs::entity& E,
		const FBarrageBodyState& Body,
		UBarrageDispatch* Barrage)
	{
		check(Barrage);

		// Resolve EntityDefinition to determine the body type.
		const FEntityDefinitionRef* DefRef = E.try_get<FEntityDefinitionRef>();
		if (!DefRef || !DefRef->Definition)
		{
			UE_LOG(LogFlecsSave, Warning,
				TEXT("RestoreBarrageBodyForEntity: entity %llu lacks FEntityDefinitionRef → cannot restore body"),
				static_cast<uint64>(E.id()));
			return false;
		}

		const UFlecsEntityDefinition* Def = DefRef->Definition;
		const UFlecsPhysicsProfile* PhysProfile = Def->PhysicsProfile;
		const UFlecsProjectileProfile* ProjProfile = Def->ProjectileProfile;

		if (!PhysProfile)
		{
			UE_LOG(LogFlecsSave, Warning,
				TEXT("RestoreBarrageBodyForEntity: entity %llu definition '%s' has no PhysicsProfile — no body to restore"),
				static_cast<uint64>(E.id()), *Def->GetName());
			return false;
		}

		// PHASE 5 SCOPE: compound bodies and characters are deferred. Surface a clear
		// Warning so the test/QA pass knows the entity wasn't fully restored.
		if (PhysProfile->CompoundSubShapes.Num() > 0)
		{
			UE_LOG(LogFlecsSave, Warning,
				TEXT("RestoreBarrageBodyForEntity: entity %llu definition '%s' is a compound body — Phase 5 stub: not restored (Phase 6/7 will wire this)"),
				static_cast<uint64>(E.id()), *Def->GetName());
			return false;
		}

		// Build the world transform from the saved state.
		const FVector Position(Body.PosX, Body.PosY, Body.PosZ);
		const FQuat Rotation(Body.RotX, Body.RotY, Body.RotZ, Body.RotW);
		const FVector LinVel(Body.LinVelX, Body.LinVelY, Body.LinVelZ);

		// Reuse the existing FBarrageBody.BarrageKey if present. Currently the on-disk
		// per-entity Flags bit-0 indicates "Barrage block present" but the FBarrageBody
		// COMPONENT itself isn't decoded by an encoder (it's a forward-binding component
		// re-created at body-spawn time). For Phase 5 we generate a fresh key by routing
		// through the standard projectile generator below — same id space the spawn path
		// uses, so reverse lookups stay correct.
		FSkeletonKey EntityKey;
		if (const FBarrageBody* ExistingBody = E.try_get<FBarrageBody>())
		{
			EntityKey = ExistingBody->BarrageKey;
		}
		if (!EntityKey.IsValid())
		{
			// Fresh key generation: projectiles use SFIX_GUN_SHOT nibble; other bodies
			// get the default. FBarrageSpawnUtils::GenerateUniqueKey is sim-thread-safe
			// (atomic counter increment).
			EntityKey = ProjProfile
				? FBarrageSpawnUtils::GenerateUniqueKey(SKELLY::SFIX_GUN_SHOT)
				: FBarrageSpawnUtils::GenerateUniqueKey();
		}

		// ─────────────────────────────────────────────────────────────
		// PROJECTILE PATH: bouncing sphere
		// ─────────────────────────────────────────────────────────────
		if (ProjProfile)
		{
			const float CollisionRadius = PhysProfile->CollisionRadius;
			const bool bBouncing = ProjProfile->IsBouncing();

			FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(Position, CollisionRadius);

			FBLet NewBody = Barrage->CreateBouncingSphere(
				SphereParams,
				EntityKey,
				static_cast<uint16>(EPhysicsLayer::PROJECTILE),
				bBouncing ? PhysProfile->Restitution : 0.f,
				PhysProfile->Friction,
				PhysProfile->LinearDamping,
				PhysProfile->Mass,
				PhysProfile->AngularDamping);

			if (!FBarragePrimitive::IsNotNull(NewBody))
			{
				UE_LOG(LogFlecsSave, Error,
					TEXT("RestoreBarrageBodyForEntity: CreateBouncingSphere failed for entity %llu"),
					static_cast<uint64>(E.id()));
				return false;
			}

			// Apply saved velocity + gravity.
			if (!LinVel.IsNearlyZero())
			{
				FBarragePrimitive::SetVelocity(LinVel, NewBody);
			}
			FBarragePrimitive::SetGravityFactor(PhysProfile->GravityFactor, NewBody);

			// Bind the entity to the new body. Forward (FBarrageBody) + reverse (atomic).
			// flecs::entity is value-type (16 bytes); the by-value parameter is fine.
			if (UFlecsArtillerySubsystem* Artillery = UFlecsArtillerySubsystem::SelfPtr)
			{
				flecs::entity EntityCopy(E);
				Artillery->BindEntityToBarrage(EntityCopy, EntityKey);
			}
			return true;
		}

		// ─────────────────────────────────────────────────────────────
		// NON-PROJECTILE BOX (item / destructible / door)
		// ─────────────────────────────────────────────────────────────
		// PHASE 5 STUB: full mesh-based body restoration requires LoadComplexStaticMesh or
		// FBarrageSpawnUtils::SpawnEntity (which expects RenderProfile mesh + collision auto).
		// That code path needs the game thread (StaticMesh asset access + ISM AddInstance).
		// For Phase 5 we mark this as deferred — the entity stays alive in Flecs but lacks
		// a physics body. Game-side collision queries (interact, raycast) will miss it
		// until Phase 6/7 wires the full restore.
		UE_LOG(LogFlecsSave, Warning,
			TEXT("RestoreBarrageBodyForEntity: entity %llu definition '%s' is a non-projectile body — Phase 5 stub: not restored (Phase 6/7 will wire the static-mesh + door + character paths)"),
			static_cast<uint64>(E.id()), *Def->GetName());
		return false;
	}

	void UnbindAndTombstoneBarrageBody(
		UFlecsArtillerySubsystem* Artillery,
		const FBarrageBody& Body)
	{
		check(Artillery);

		if (!Body.IsValid())
		{
			return;
		}

		UBarrageDispatch* Barrage = Artillery->GetBarrageDispatch();
		if (!Barrage)
		{
			return;
		}

		FBLet Prim = Barrage->GetShapeRef(Body.BarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim))
		{
			return;
		}

		// Standard safe-destroy sequence (per CLAUDE.md "Entity Destruction (CORRECT)"):
		//   1. Clear Flecs binding atomically so any in-flight contact callback can't
		//      look up a dangling entity.
		//   2. Move to DEBRIS layer for instant collision disable.
		//   3. SuggestTombstone for safe deferred destroy (~19 sec).
		Prim->ClearFlecsEntity();
		Barrage->SetBodyObjectLayer(Prim->KeyIntoBarrage, static_cast<uint8>(EPhysicsLayer::DEBRIS));
		Barrage->SuggestTombstone(Prim);
	}
}
