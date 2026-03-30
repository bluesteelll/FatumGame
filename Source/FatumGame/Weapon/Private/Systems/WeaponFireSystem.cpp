// FlecsArtillerySubsystem - WeaponFireSystem
// Processes fire requests, aim correction, spread, projectile spawn.

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"
#include "FlecsWeaponComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsHealthComponents.h"
#include "FBarragePrimitive.h"
#include "BarrageDispatch.h"
#include "FBShapeParams.h"
#include "BarrageSpawnUtils.h"
#include "Skeletonize.h"
#include "FlecsRenderProfile.h"
#include "FlecsProjectileProfile.h"
#include "FlecsPhysicsProfile.h"
#include "FlecsEntityDefinition.h"
#include "FlecsDamageProfile.h"
#include "FlecsNiagaraProfile.h"
#include "FlecsExplosionProfile.h"
#include "FlecsExplosionComponents.h"
#include "FlecsNiagaraManager.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "FlecsRecoilTypes.h"
#include "FlecsMovementStatic.h"
#include "FlecsMovementComponents.h"
#include "FlecsWeaponProfile.h"
#include "FlecsItemComponents.h"
#include "FlecsAmmoTypeDefinition.h"
#include "FlecsVitalsComponents.h"

void UFlecsArtillerySubsystem::SetupWeaponFireSystem()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// WEAPON FIRE SYSTEM
	// Processes fire requests for equipped weapons.
	// Queues projectile spawns for game thread processing.
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance, const FEquippedBy>("WeaponFireSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([this, &World](flecs::entity WeaponEntity, FWeaponInstance& Weapon, const FEquippedBy& EquippedBy)
		{
			// Flecs worker threads need Barrage access for physics lookups
			EnsureBarrageAccess();

			// Only process equipped weapons
			if (!EquippedBy.IsEquipped()) return;

			// DBG: trace fire system processing
			if (Weapon.bFireRequested || Weapon.bFireTriggerPending)
			{
				UE_LOG(LogTemp, Warning, TEXT("FIRE DBG: entity=%lld FireReq=%d TrigPend=%d Phase=%d NeedsCycle=%d Cycling=%d Chambered=%d MagId=%lld"),
					static_cast<int64>(WeaponEntity.id()), Weapon.bFireRequested, Weapon.bFireTriggerPending,
					static_cast<int>(Weapon.ReloadPhase), Weapon.bNeedsCycle, Weapon.bCycling, Weapon.bChambered, Weapon.InsertedMagazineId);
			}

			const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>();
			if (!Static) return;

			// Unlimited ammo requires ProjectileDefinition on weapon
			if (Static->bUnlimitedAmmo && !Static->ProjectileDefinition) return;

			// Check fire request: continuous hold OR pending trigger (survives Start+Stop batching)
			// Trigger pull weapons require CONTINUOUS hold — latched trigger not sufficient
			if (Static->bEnableTriggerPull)
			{
				if (!Weapon.bFireRequested)
				{
					// Fire released — cancel trigger pull, consume pending trigger
					if (Weapon.bTriggerPulling)
					{
						Weapon.bTriggerPulling = false;
						Weapon.TriggerPullTimer = 0.f;
					}
					Weapon.bFireTriggerPending = false;
					return;
				}
			}
			else
			{
				if (!Weapon.bFireRequested && !Weapon.bFireTriggerPending)
					return;
			}

			// Semi-auto: block if already fired while trigger held
			if (!Static->bIsAutomatic && !Static->bIsBurst && Weapon.bHasFiredSincePress)
				return;

			// Post-fire cycling: must cycle before next shot (bolt/pump)
			if (Static->bRequiresCycling && Weapon.bNeedsCycle && !Weapon.bCycling)
			{
				Weapon.bCycling = true;
				Weapon.CycleTimeRemaining = Static->CycleTime;
				return;
			}
			if (Weapon.bCycling)
				return;

			// Check if can fire (cooldown expired, has magazine, not reloading)
			if (!Weapon.CanFire())
			{
				UE_LOG(LogTemp, Warning, TEXT("WEAPON FIRE DBG: CanFire=false (Phase=%d, MagId=%lld, Cooldown=%.3f, Burst=%.3f, NeedsCycle=%d)"),
					static_cast<int>(Weapon.ReloadPhase), Weapon.InsertedMagazineId,
					Weapon.FireCooldownRemaining, Weapon.BurstCooldownRemaining, Weapon.bNeedsCycle);
				// No magazine inserted — consume trigger so it doesn't fire after reload
				if (Weapon.InsertedMagazineId == 0)
					Weapon.bFireTriggerPending = false;
				return;
			}

			// ── Trigger pull delay (revolver double-action) ──
			if (Static->bEnableTriggerPull)
			{
				const float DeltaTime = WeaponEntity.world().get_info()->delta_time;

				if (!Weapon.bTriggerPulling)
				{
					// First shot always requires pull. Subsequent: only if bTriggerPullEveryShot.
					bool bNeedsPull = (Weapon.ShotsFiredTotal == 0)
						|| Static->bTriggerPullEveryShot
						|| !Weapon.bFireRequested;  // re-press after release = new pull

					if (bNeedsPull && Weapon.FireCooldownRemaining <= 0.f)
					{
						Weapon.bTriggerPulling = true;
						Weapon.TriggerPullTimer = Static->TriggerPullTime;
					}
				}

				if (Weapon.bTriggerPulling)
				{
					Weapon.TriggerPullTimer -= DeltaTime;
					if (Weapon.TriggerPullTimer > 0.f)
						return;  // still pulling — don't fire yet

					// Pull complete — proceed to fire
					Weapon.bTriggerPulling = false;
					Weapon.TriggerPullTimer = 0.f;
				}
			}

			// Check magazine has ammo or chambered round (only for non-unlimited)
			if (!Static->bUnlimitedAmmo)
			{
				bool bHasAmmo = Weapon.bChambered;  // chambered round counts
				if (!bHasAmmo)
				{
					flecs::entity MagCheck = World.entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
					if (MagCheck.is_valid())
					{
						const FMagazineInstance* MagCheckInst = MagCheck.try_get<FMagazineInstance>();
						if (MagCheckInst && !MagCheckInst->IsEmpty())
							bHasAmmo = true;
					}
				}
				if (!bHasAmmo)
				{
					// Auto-reload on empty
					Weapon.bReloadRequested = true;
					Weapon.bFireTriggerPending = false;
					return;
				}
			}

			// Get character entity - if dead or invalid, stop firing
			flecs::entity CharacterEntity = World.entity(static_cast<flecs::entity_t>(EquippedBy.CharacterEntityId));
			if (!CharacterEntity.is_valid() || !CharacterEntity.is_alive() || CharacterEntity.has<FTagDead>())
			{
				Weapon.bFireRequested = false;
				Weapon.bFireTriggerPending = false;
				return;
			}

			const FBarrageBody* CharBody = CharacterEntity.try_get<FBarrageBody>();
			if (!CharBody || !CharBody->IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("WEAPON: CharBody null or invalid! HasComponent=%d"), CharBody != nullptr);
				return;
			}

			// MUZZLE CALCULATION — reads aim state from FAimDirection
			// MuzzleWorldPosition is the actual weapon socket position (follows animations).
			// CharacterPosition is camera position (for aim raycast origin).
			FVector MuzzleLocation = FVector::ZeroVector;
			FVector FireDirection = FVector::ForwardVector;
			FVector CharPosD = FVector::ZeroVector;

			const FAimDirection* AimDir = CharacterEntity.try_get<FAimDirection>();
			if (AimDir)
			{
				if (!AimDir->Direction.IsNearlyZero())
				{
					FireDirection = AimDir->Direction;
				}
				CharPosD = AimDir->CharacterPosition;

				if (!AimDir->MuzzleWorldPosition.IsNearlyZero())
				{
					MuzzleLocation = AimDir->MuzzleWorldPosition;
				}
			}

			// Fallback: compute from camera + weapon static offset (no weapon mesh socket)
			if (MuzzleLocation.IsNearlyZero())
			{
				FQuat AimQuat = FRotationMatrix::MakeFromX(FireDirection).ToQuat();
				FTransform MuzzleTransform(AimQuat, CharPosD);
				MuzzleLocation = MuzzleTransform.TransformPosition(Static->MuzzleOffset);
			}

			// ─────────────────────────────────────────────────────
			// RESOLVE PROJECTILE DEFINITION FROM MAGAZINE AMMO STACK
			// ─────────────────────────────────────────────────────
			UFlecsEntityDefinition* ProjDef = nullptr;
			float AmmoDamageMult = 1.f;
			float AmmoSpeedMult = 1.f;

			if (Static->bUnlimitedAmmo)
			{
				ProjDef = Static->ProjectileDefinition;
			}
			else
			{
				flecs::entity MagEntity = World.entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
				checkf(MagEntity.is_valid(), TEXT("WeaponFireSystem: InsertedMagazineId %lld is invalid"), Weapon.InsertedMagazineId);

				FMagazineInstance* MagInst = MagEntity.try_get_mut<FMagazineInstance>();
				const FMagazineStatic* MagStatic = MagEntity.try_get<FMagazineStatic>();
				checkf(MagInst && MagStatic, TEXT("WeaponFireSystem: Magazine entity missing components"));

				// Fire chambered round first, then chamber next from magazine
				int32 AmmoTypeIdx;
				if (Weapon.bChambered)
				{
					AmmoTypeIdx = Weapon.ChamberedAmmoTypeIdx;
					Weapon.bChambered = false;

					// Chamber next round from magazine (if available)
					if (MagInst->AmmoCount > 0)
					{
						int32 NextIdx = MagInst->Pop();
						Weapon.bChambered = true;
						Weapon.ChamberedAmmoTypeIdx = static_cast<uint8>(NextIdx);
					}
				}
				else
				{
					// No chambered round — pop directly from magazine
					AmmoTypeIdx = MagInst->Pop();
					checkf(AmmoTypeIdx >= 0, TEXT("WeaponFireSystem: Magazine is empty and no chambered round"));
				}

				checkf(AmmoTypeIdx >= 0 && AmmoTypeIdx < MagStatic->AcceptedAmmoTypeCount,
					TEXT("WeaponFireSystem: AmmoTypeIdx %d out of range (%d)"), AmmoTypeIdx, MagStatic->AcceptedAmmoTypeCount);

				UFlecsAmmoTypeDefinition* AmmoType = MagStatic->AcceptedAmmoTypes[AmmoTypeIdx];
				checkf(AmmoType && AmmoType->ProjectileDefinition, TEXT("WeaponFireSystem: AmmoType or its ProjectileDefinition is null"));

				ProjDef = AmmoType->ProjectileDefinition;
				AmmoDamageMult = AmmoType->DamageMultiplier;
				AmmoSpeedMult = AmmoType->SpeedMultiplier;
			}

			check(ProjDef);
			UFlecsProjectileProfile* ProjProfile = ProjDef->ProjectileProfile;
			if (!ProjProfile)
			{
				UE_LOG(LogTemp, Error, TEXT("WEAPON: ProjectileDefinition '%s' has no ProjectileProfile!"),
					*ProjDef->EntityName.ToString());
				return;
			}
			UFlecsPhysicsProfile* PhysProfile = ProjDef->PhysicsProfile;
			UFlecsRenderProfile* RenderProfile = ProjDef->RenderProfile;

			const float CollisionRadius = PhysProfile ? PhysProfile->CollisionRadius : 30.f;
			const float GravityFactor = PhysProfile ? PhysProfile->GravityFactor : 0.f;
			const float ProjFriction = PhysProfile ? PhysProfile->Friction : 0.2f;
			const float ProjRestitution = PhysProfile ? PhysProfile->Restitution : 0.3f;
			const float ProjLinearDamping = PhysProfile ? PhysProfile->LinearDamping : 0.0f;
			const bool bIsBouncing = ProjProfile->IsBouncing();
			// ALL projectiles use dynamic body — sensors tunnel at high speed (no CCD).
			// Non-bouncing non-gravity projectiles: dynamic + restitution=0 + gravity=0.
			const bool bNeedsDynamic = true;

			// ─────────────────────────────────────────────────────
			// AIM CORRECTION: Raycast from camera to find actual target,
			// then compute barrel→target direction. Projectile spawns
			// from barrel AND flies exactly where the crosshair points.
			// ─────────────────────────────────────────────────────
			const float AimTraceRange = 100000.f; // 1km
			FVector TargetPoint = CharPosD + FireDirection * AimTraceRange;

			{
				FBLet CharPrim = CachedBarrageDispatch->GetShapeRef(CharBody->BarrageKey);
				if (FBarragePrimitive::IsNotNull(CharPrim))
				{
					auto BPFilter = CachedBarrageDispatch->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
					FastExcludeObjectLayerFilter ObjFilter({
						EPhysicsLayer::PROJECTILE,
						EPhysicsLayer::ENEMYPROJECTILE,
						EPhysicsLayer::DEBRIS
					});
					auto BodyFilter = CachedBarrageDispatch->GetFilterToIgnoreSingleBody(CharPrim);

					TSharedPtr<FHitResult> AimHit = MakeShared<FHitResult>();
					CachedBarrageDispatch->CastRay(
						CharPosD,
						FireDirection * AimTraceRange,
						BPFilter, ObjFilter, BodyFilter,
						AimHit);

					if (AimHit->bBlockingHit)
					{
						TargetPoint = AimHit->ImpactPoint;
					}
				}
			}

			// Minimum engagement distance: if target too close to camera,
			// push it along aim ray to prevent barrel parallax issues.
			constexpr float MinEngagementDist = 300.f; // 3m
			if (FVector::DistSquared(CharPosD, TargetPoint) < MinEngagementDist * MinEngagementDist)
			{
				TargetPoint = CharPosD + FireDirection * MinEngagementDist;
			}

			// Direction from barrel to target (accounts for barrel offset at any distance)
			FVector SpawnDirection = (TargetPoint - MuzzleLocation).GetSafeNormal();

			// Dot product safety: if barrel→target diverges too much from aim,
			// fall back to aim direction (catches extreme edge cases).
			if (FVector::DotProduct(SpawnDirection, FireDirection) < 0.85f) // > ~32° deviation
			{
				SpawnDirection = FireDirection;
			}

			float Speed = ProjProfile->DefaultSpeed * Static->ProjectileSpeedMultiplier * AmmoSpeedMult;

			// ─────────────────────────────────────────────────────
			// SPREAD: BaseSpread * BaseMultiplier + Bloom * BloomMultiplier
			// All values in decidegrees (1 unit = 0.1°), converted to radians at the end.
			// ─────────────────────────────────────────────────────
			float EffectiveSpread;
			{
				float BaseMult = 1.f;
				float BloomMult = 1.f;
				const FMovementState* MoveState = CharacterEntity.try_get<FMovementState>();
				if (MoveState)
				{
					EWeaponMoveState WeaponState = ResolveWeaponMoveState(MoveState->MoveMode, MoveState->Posture);
					uint8 StateIdx = static_cast<uint8>(WeaponState);
					BaseMult = Static->BaseSpreadMultipliers[StateIdx];
					BloomMult = Static->BloomMultipliers[StateIdx];
				}
				// Min() handles state transitions where BloomMult decreases mid-spray
				float Bloom = FMath::Min(Weapon.CurrentBloom, Static->MaxBloom) * BloomMult;
				EffectiveSpread = Static->BaseSpread * BaseMult + Bloom;
			}
			// Decidegrees → radians (÷10 → degrees → radians)
			const float SpreadRadians = FMath::DegreesToRadians(EffectiveSpread * 0.1f);

			// ─────────────────────────────────────────────────────────
			// PELLET DIRECTIONS
			// Ring system (Technique G): fixed rings + random rotation per shot.
			// Legacy path: independent VRandCone per pellet (single-projectile weapons).
			// ─────────────────────────────────────────────────────────
			TArray<FVector, TInlineAllocator<16>> PelletDirections;
			PelletDirections.Reserve(Static->ProjectilesPerShot);

			if (Static->PelletRingCount > 0)
			{
				// Sample bloom ONCE — all pellets share the same cone center drift
				FVector BloomDirection = SpawnDirection;
				if (SpreadRadians > KINDA_SMALL_NUMBER)
					BloomDirection = FMath::VRandCone(SpawnDirection, SpreadRadians);

				// Random rotation applied uniformly to all rings this shot
				const float RandomRotation = FMath::FRandRange(0.f, TWO_PI);

				// Build right/up basis perpendicular to bloom direction
				FVector Right, Up;
				BloomDirection.FindBestAxisVectors(Right, Up);

				for (int32 RingIdx = 0; RingIdx < Static->PelletRingCount; ++RingIdx)
				{
					const FPelletRingData& Ring = Static->PelletRings[RingIdx];

					const float AngleStep = Ring.PelletCount > 1 ? (TWO_PI / Ring.PelletCount) : 0.f;
					for (int32 PelletIdx = 0; PelletIdx < Ring.PelletCount; ++PelletIdx)
					{
						// Per-pellet jitter: angular (along ring) and radial (toward/away from center)
						const float AzimuthJitter = Ring.AngularJitterRadians > KINDA_SMALL_NUMBER
							? FMath::FRandRange(-Ring.AngularJitterRadians, Ring.AngularJitterRadians) : 0.f;
						const float RadiusJitter = Ring.RadialJitterRadians > KINDA_SMALL_NUMBER
							? FMath::FRandRange(-Ring.RadialJitterRadians, Ring.RadialJitterRadians) : 0.f;

						const float Azimuth = RandomRotation + PelletIdx * AngleStep + AzimuthJitter;
						const float JitteredRadius = Ring.RadiusRadians + RadiusJitter;

						float SinR, CosR;
						FMath::SinCos(&SinR, &CosR, JitteredRadius);
						float SinA, CosA;
						FMath::SinCos(&SinA, &CosA, Azimuth);

						FVector Dir = BloomDirection * CosR
							+ Right * (SinR * CosA)
							+ Up    * (SinR * SinA);
						PelletDirections.Add(Dir.GetSafeNormal());
					}
				}
			}
			else
			{
				// Legacy: each pellet independently samples within spread cone
				for (int32 i = 0; i < Static->ProjectilesPerShot; ++i)
				{
					FVector Dir = SpawnDirection;
					if (SpreadRadians > KINDA_SMALL_NUMBER)
						Dir = FMath::VRandCone(SpawnDirection, SpreadRadians);
					PelletDirections.Add(Dir);
				}
			}

			for (int32 i = 0; i < PelletDirections.Num(); ++i)
			{
				FSkeletonKey ProjectileKey = FBarrageSpawnUtils::GenerateUniqueKey(SKELLY::SFIX_GUN_SHOT);

				// Create Barrage physics body
				FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(MuzzleLocation, CollisionRadius);
				FBLet Body;

				if (bNeedsDynamic)
				{
					Body = CachedBarrageDispatch->CreateBouncingSphere(
						SphereParams, ProjectileKey,
						static_cast<uint16>(EPhysicsLayer::PROJECTILE),
						bIsBouncing ? ProjRestitution : 0.f, ProjFriction, ProjLinearDamping);
				}
				else
				{
					Body = CachedBarrageDispatch->CreatePrimitive(
						SphereParams, ProjectileKey,
						static_cast<uint16>(EPhysicsLayer::PROJECTILE), true);
				}

				if (!FBarragePrimitive::IsNotNull(Body))
				{
					UE_LOG(LogTemp, Error, TEXT("WEAPON: Failed to create projectile body!"));
					continue;
				}

				FVector PelletDirection = PelletDirections[i];
				FVector PelletVelocity = PelletDirection * Speed;

				FBarragePrimitive::SetVelocity(PelletVelocity, Body);
				FBarragePrimitive::SetGravityFactor(GravityFactor, Body);

				// Create Flecs entity with components (no prefab — avoids deferred timing issues)
				flecs::entity ProjEntity = World.entity();

				FBarrageBody BarrageComp;
				BarrageComp.BarrageKey = ProjectileKey;
				ProjEntity.set<FBarrageBody>(BarrageComp);

				// Reverse binding (atomic in FBarragePrimitive)
				Body->SetFlecsEntity(ProjEntity.id());

				FProjectileInstance ProjInst;
				ProjInst.LifetimeRemaining = ProjProfile->Lifetime;
				ProjInst.BounceCount = 0;
				ProjInst.GraceFramesRemaining = ProjProfile->GetGraceFrames();
				ProjInst.FuseRemaining = ProjProfile->FuseTime;
				ProjInst.OwnerEntityId = EquippedBy.CharacterEntityId;
				ProjEntity.set<FProjectileInstance>(ProjInst);
				ProjEntity.add<FTagProjectile>();

				// Static data directly on entity (projectile-specific, no prefab sharing needed)
				if (ProjProfile)
					ProjEntity.set<FProjectileStatic>(FProjectileStatic::FromProfile(ProjProfile));

				if (ProjDef->DamageProfile)
				{
					FDamageStatic DmgStatic = FDamageStatic::FromProfile(ProjDef->DamageProfile);
					DmgStatic.Damage *= Static->DamageMultiplier * AmmoDamageMult;
					ProjEntity.set<FDamageStatic>(DmgStatic);
				}

				if (ProjDef->ExplosionProfile)
					ProjEntity.set<FExplosionStatic>(FExplosionStatic::FromProfile(ProjDef->ExplosionProfile));

				if (RenderProfile && RenderProfile->Mesh)
				{
					FISMRender Render;
					Render.Mesh = RenderProfile->Mesh;
					Render.Scale = RenderProfile->Scale;
					ProjEntity.set<FISMRender>(Render);

					// Queue ISM render for game thread
					FPendingProjectileSpawn RenderSpawn;
					RenderSpawn.Mesh = RenderProfile->Mesh;
					RenderSpawn.Material = RenderProfile->MaterialOverride;
					RenderSpawn.Scale = RenderProfile->Scale;
					RenderSpawn.RotationOffset = RenderProfile->RotationOffset;
					RenderSpawn.SpawnDirection = PelletDirection;
					RenderSpawn.SimComputedLocation = MuzzleLocation;
					RenderSpawn.EntityKey = ProjectileKey;

					// Niagara VFX fields (attached effect)
					if (ProjDef->NiagaraProfile && ProjDef->NiagaraProfile->HasAttachedEffect())
					{
						RenderSpawn.NiagaraEffect = ProjDef->NiagaraProfile->AttachedEffect;
						RenderSpawn.NiagaraScale = ProjDef->NiagaraProfile->AttachedEffectScale;
						RenderSpawn.NiagaraOffset = ProjDef->NiagaraProfile->AttachedOffset;
					}

					PendingProjectileSpawns.Enqueue(RenderSpawn);
				}

				// Death VFX component (read by DeadEntityCleanupSystem on death)
				if (ProjDef->NiagaraProfile && ProjDef->NiagaraProfile->HasDeathEffect())
				{
					FNiagaraDeathEffect DeathVFX;
					DeathVFX.Effect = ProjDef->NiagaraProfile->DeathEffect;
					DeathVFX.Scale = ProjDef->NiagaraProfile->DeathEffectScale;
					ProjEntity.set<FNiagaraDeathEffect>(DeathVFX);
				}

				UE_LOG(LogTemp, Log, TEXT("WEAPON: Projectile Key=%llu Entity=%llu AimDir=(%.3f,%.3f,%.3f) SpawnDir=(%.3f,%.3f,%.3f) Speed=%.0f Gravity=%.2f Target=(%.0f,%.0f,%.0f)"),
					static_cast<uint64>(ProjectileKey), ProjEntity.id(),
					FireDirection.X, FireDirection.Y, FireDirection.Z,
					SpawnDirection.X, SpawnDirection.Y, SpawnDirection.Z,
					Speed, GravityFactor,
					TargetPoint.X, TargetPoint.Y, TargetPoint.Z);
			}

			// Ammo was already consumed by MagInst->Pop() above (for non-unlimited).
			// Read current magazine state for UI.

			// Increment bloom (CurrentBloom = bloom only, capped at MaxBloom)
			Weapon.CurrentBloom = FMath::Min(Weapon.CurrentBloom + Static->SpreadPerShot, Static->MaxBloom);
			Weapon.TimeSinceLastShot = 0.f;

			// Enqueue shot-fired event for game thread recoil
			{
				FShotFiredEvent ShotEvent;
				ShotEvent.WeaponEntityId = static_cast<int64>(WeaponEntity.id());
				ShotEvent.ShotIndex = Weapon.ShotsFiredTotal++;
				PendingShotEvents.Enqueue(ShotEvent);
			}

			// Get ammo count from magazine for UI (+1 if chambered)
			int32 CurrentAmmoForUI = Weapon.bChambered ? 1 : 0;
			int32 MagSizeForUI = Static->bHasChamber ? 1 : 0;
			if (!Static->bUnlimitedAmmo && Weapon.InsertedMagazineId != 0)
			{
				flecs::entity MagUI = World.entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
				if (MagUI.is_valid())
				{
					const FMagazineInstance* MI = MagUI.try_get<FMagazineInstance>();
					const FMagazineStatic* MS = MagUI.try_get<FMagazineStatic>();
					if (MI) CurrentAmmoForUI += MI->AmmoCount;
					if (MS) MagSizeForUI += MS->Capacity;
				}
			}

			UE_LOG(LogTemp, Log, TEXT("WEAPON: FIRED! MagAmmo=%d/%d, Auto=%d, Burst=%d, Bloom=%.2f"),
				CurrentAmmoForUI, MagSizeForUI,
				Static->bIsAutomatic, Static->bIsBurst, Weapon.CurrentBloom);

			// Broadcast ammo change to message system (sim→game thread)
			if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
			{
				FUIAmmoMessage AmmoMsg;
				AmmoMsg.WeaponEntityId = static_cast<int64>(WeaponEntity.id());
				AmmoMsg.CurrentAmmo = CurrentAmmoForUI;
				AmmoMsg.MagazineSize = MagSizeForUI;
				AmmoMsg.ReserveAmmo = 0; // Reserve concept replaced by physical magazines
				MsgSub->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
			}

			// Update sim→game state cache
			SimStateCache.WriteWeapon(static_cast<int64>(WeaponEntity.id()),
				CurrentAmmoForUI, MagSizeForUI, 0, Weapon.IsReloading());

			// Carry-over overshoot for consistent average fire rate.
			// If cooldown was -0.003 when we fire, += FireInterval gives 0.097
			// instead of 0.1, compensating for the overshoot.
			Weapon.FireCooldownRemaining += Static->FireInterval;

			// Start post-fire cycling (bolt/pump must cycle before next shot)
			if (Static->bRequiresCycling)
			{
				Weapon.bNeedsCycle = true;
				Weapon.bCycling = true;
				Weapon.CycleTimeRemaining = Static->CycleTime;
			}

			// Consume pending trigger (one shot per click guaranteed)
			Weapon.bFireTriggerPending = false;

			// Mark as fired for semi-auto
			Weapon.bHasFiredSincePress = true;

			// Handle burst mode
			if (Static->bIsBurst)
			{
				if (Weapon.BurstShotsRemaining == 0)
				{
					// Starting new burst
					Weapon.BurstShotsRemaining = Static->BurstCount - 1;
				}
				else
				{
					Weapon.BurstShotsRemaining--;
					if (Weapon.BurstShotsRemaining == 0)
					{
						// Burst complete, enter cooldown
						Weapon.BurstCooldownRemaining = Static->BurstDelay;
						Weapon.bHasFiredSincePress = true; // Block until trigger release
					}
				}
			}

			// Auto-reload when magazine AND chamber are both empty
			if (!Static->bUnlimitedAmmo && !Weapon.bChambered && Weapon.InsertedMagazineId != 0)
			{
				flecs::entity MagAutoReload = World.entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
				if (MagAutoReload.is_valid())
				{
					const FMagazineInstance* MagInst = MagAutoReload.try_get<FMagazineInstance>();
					if (MagInst && MagInst->IsEmpty())
					{
						Weapon.bReloadRequested = true;
					}
				}
			}
		});
}
