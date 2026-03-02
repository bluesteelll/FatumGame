// FlecsArtillerySubsystem - Weapon Systems
// WeaponTickSystem, WeaponReloadSystem, WeaponFireSystem

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsWeaponComponents.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
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
#include "FlecsNiagaraManager.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"

void UFlecsArtillerySubsystem::SetupWeaponSystems()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// WEAPON TICK SYSTEM
	// Updates timers for fire rate, burst cooldown, and semi-auto reset.
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance>("WeaponTickSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FWeaponInstance& Weapon)
		{
			const float DeltaTime = Entity.world().get_info()->delta_time;

			// Countdown fire cooldown (subtract from clean value, no accumulation error)
			if (Weapon.FireCooldownRemaining > 0.f)
			{
				Weapon.FireCooldownRemaining -= DeltaTime;
			}

			// Update burst cooldown
			if (Weapon.BurstCooldownRemaining > 0.f)
			{
				Weapon.BurstCooldownRemaining -= DeltaTime;
			}

			// Reset semi-auto flag when trigger released
			if (!Weapon.bFireRequested && Weapon.bHasFiredSincePress)
			{
				Weapon.bHasFiredSincePress = false;
			}
		});

	// ─────────────────────────────────────────────────────────
	// WEAPON RELOAD SYSTEM
	// Handles reload state machine.
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance>("WeaponReloadSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([this](flecs::entity Entity, FWeaponInstance& Weapon)
		{
			const float DeltaTime = Entity.world().get_info()->delta_time;

			const FWeaponStatic* Static = Entity.try_get<FWeaponStatic>();
			if (!Static) return;

			// Process reload request
			if (Weapon.bReloadRequested && !Weapon.bIsReloading)
			{
				if (Weapon.CanReload(Static->MagazineSize, Static->bUnlimitedAmmo))
				{
					Weapon.bIsReloading = true;
					Weapon.ReloadTimeRemaining = Static->ReloadTime;
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload started, %.2f sec"), Static->ReloadTime);

					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage ReloadMsg;
						ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
						ReloadMsg.bStarted = true;
						ReloadMsg.MagazineSize = Static->MagazineSize;
						MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
					}

					// Update sim→game state cache (reload started)
					SimStateCache.WriteWeapon(static_cast<int64>(Entity.id()),
						Weapon.CurrentAmmo, Static->MagazineSize, Weapon.ReserveAmmo, true);
				}
				Weapon.bReloadRequested = false;
			}

			// Process reload timer
			if (Weapon.bIsReloading)
			{
				Weapon.ReloadTimeRemaining -= DeltaTime;

				if (Weapon.ReloadTimeRemaining <= 0.f)
				{
					// Complete reload
					int32 AmmoNeeded = Static->MagazineSize - Weapon.CurrentAmmo;

					if (Static->bUnlimitedAmmo)
					{
						Weapon.CurrentAmmo = Static->MagazineSize;
					}
					else
					{
						int32 AmmoToLoad = FMath::Min(AmmoNeeded, Weapon.ReserveAmmo);
						Weapon.CurrentAmmo += AmmoToLoad;
						Weapon.ReserveAmmo -= AmmoToLoad;
					}

					Weapon.bIsReloading = false;
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload complete, ammo=%d/%d reserve=%d"),
						Weapon.CurrentAmmo, Static->MagazineSize, Weapon.ReserveAmmo);

					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage ReloadMsg;
						ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
						ReloadMsg.bStarted = false;
						ReloadMsg.NewAmmo = Weapon.CurrentAmmo;
						ReloadMsg.MagazineSize = Static->MagazineSize;
						MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
					}

					// Update sim→game state cache (reload complete)
					SimStateCache.WriteWeapon(static_cast<int64>(Entity.id()),
						Weapon.CurrentAmmo, Static->MagazineSize, Weapon.ReserveAmmo, false);
				}
			}
		});

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

			const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>();
			if (!Static || !Static->ProjectileDefinition) return;

			// Check fire request: continuous hold OR pending trigger (survives Start+Stop batching)
			if (!Weapon.bFireRequested && !Weapon.bFireTriggerPending) return;

			// Semi-auto: block if already fired while trigger held
			if (!Static->bIsAutomatic && !Static->bIsBurst && Weapon.bHasFiredSincePress)
			{
				UE_LOG(LogTemp, Warning, TEXT("WEAPON: Blocked by semi-auto (bHasFiredSincePress=true)"));
				return;
			}

			// Check if can fire (cooldown expired, has ammo, not reloading)
			if (!Weapon.CanFire())
			{
				return;
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
			// PROJECTILE CREATION (on sim thread — no game thread round-trip)
			// Creates Barrage body + Flecs entity immediately.
			// Only ISM render queued for game thread.
			// ─────────────────────────────────────────────────────
			UFlecsEntityDefinition* ProjDef = Static->ProjectileDefinition;
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

			float Speed = ProjProfile->DefaultSpeed * Static->ProjectileSpeedMultiplier;
			FVector Velocity = SpawnDirection * Speed;

			for (int32 i = 0; i < Static->ProjectilesPerShot; ++i)
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

				FBarragePrimitive::SetVelocity(Velocity, Body);
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
				ProjInst.OwnerEntityId = EquippedBy.CharacterEntityId;
				ProjEntity.set<FProjectileInstance>(ProjInst);
				ProjEntity.add<FTagProjectile>();

				// Static data directly on entity (projectile-specific, no prefab sharing needed)
				if (ProjProfile)
					ProjEntity.set<FProjectileStatic>(FProjectileStatic::FromProfile(ProjProfile));

				if (ProjDef->DamageProfile)
					ProjEntity.set<FDamageStatic>(FDamageStatic::FromProfile(ProjDef->DamageProfile));

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
					RenderSpawn.SpawnDirection = SpawnDirection;
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

			// Consume ammo
			int32 AmmoBefore = Weapon.CurrentAmmo;
			if (!Static->bUnlimitedAmmo)
			{
				Weapon.CurrentAmmo -= Static->AmmoPerShot;
				Weapon.CurrentAmmo = FMath::Max(0, Weapon.CurrentAmmo);
			}

			UE_LOG(LogTemp, Log, TEXT("WEAPON: FIRED! Ammo=%d->%d, Auto=%d, Burst=%d"),
				AmmoBefore, Weapon.CurrentAmmo,
				Static->bIsAutomatic, Static->bIsBurst);

			// Broadcast ammo change to message system (sim→game thread)
			if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
			{
				FUIAmmoMessage AmmoMsg;
				AmmoMsg.WeaponEntityId = static_cast<int64>(WeaponEntity.id());
				AmmoMsg.CurrentAmmo = Weapon.CurrentAmmo;
				AmmoMsg.MagazineSize = Static->MagazineSize;
				AmmoMsg.ReserveAmmo = Weapon.ReserveAmmo;
				MsgSub->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
			}

			// Update sim→game state cache
			SimStateCache.WriteWeapon(static_cast<int64>(WeaponEntity.id()),
				Weapon.CurrentAmmo, Static->MagazineSize, Weapon.ReserveAmmo, Weapon.bIsReloading);

			// Carry-over overshoot for consistent average fire rate.
			// If cooldown was -0.003 when we fire, += FireInterval gives 0.097
			// instead of 0.1, compensating for the overshoot.
			Weapon.FireCooldownRemaining += Static->FireInterval;

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

			// Auto-reload when empty
			if (Weapon.CurrentAmmo == 0 && !Static->bUnlimitedAmmo && Weapon.ReserveAmmo > 0)
			{
				Weapon.bReloadRequested = true;
			}
		});
}
