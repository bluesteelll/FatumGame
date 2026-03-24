// FlecsArtillerySubsystem - Weapon Systems
// WeaponTickSystem, WeaponReloadSystem, WeaponFireSystem

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
#include "FlecsContainerLibrary.h"

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

			// Bloom decay (CurrentBloom = bloom only, decays to 0)
			const FWeaponStatic* Static = Entity.try_get<FWeaponStatic>();
			if (Static)
			{
				Weapon.TimeSinceLastShot += DeltaTime;
				if (Weapon.TimeSinceLastShot > Static->BloomRecoveryDelay
					&& Weapon.CurrentBloom > 0.f)
				{
					Weapon.CurrentBloom -= Static->BloomDecayRate * DeltaTime;
					if (Weapon.CurrentBloom < 0.f)
					{
						Weapon.CurrentBloom = 0.f;
					}
				}
			}
		});

	// ─────────────────────────────────────────────────────────
	// WEAPON RELOAD SYSTEM (Phase-based: Remove → Insert → Chamber)
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance>("WeaponReloadSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([this](flecs::entity Entity, FWeaponInstance& Weapon)
		{
			const FWeaponStatic* Static = Entity.try_get<FWeaponStatic>();
			if (!Static || Static->bUnlimitedAmmo) return;

			const float DeltaTime = Entity.world().get_info()->delta_time;

			// ── Handle reload request (Idle → RemovingMag) ──
			if (Weapon.bReloadRequested && Weapon.ReloadPhase == EWeaponReloadPhase::Idle)
			{
				Weapon.bReloadRequested = false;

				// Find character's inventory
				const FEquippedBy* Equip = Entity.try_get<FEquippedBy>();
				if (!Equip || !Equip->IsEquipped()) return;

				flecs::entity CharEntity = Entity.world().entity(static_cast<flecs::entity_t>(Equip->CharacterEntityId));
				if (!CharEntity.is_valid()) return;

				const FCharacterInventoryRef* InvRef = CharEntity.try_get<FCharacterInventoryRef>();
				if (!InvRef || InvRef->InventoryEntityId == 0) return;

				// Find best magazine in inventory
				// NOTE: FMagazineStatic is on prefab (inherited via IsA), so we can't use it
				// as a typed param in each() — read it via try_get() instead.
				int64 BestMagId = 0;
				int32 BestAmmoCount = 0;
				const int64 InvId = InvRef->InventoryEntityId;
				const int64 CurrentMagId = Weapon.InsertedMagazineId;
				int32 DbgTotal = 0, DbgInInv = 0, DbgHasAmmo = 0, DbgHasStatic = 0, DbgCaliberMatch = 0;

				Entity.world().each([&](flecs::entity MagEntity, const FContainedIn& CI, const FMagazineInstance& MagInst)
				{
					++DbgTotal;
					UE_LOG(LogTemp, Log, TEXT("  DBG MAG: entity=%lld container=%lld ammo=%d (want inv=%lld)"),
						static_cast<int64>(MagEntity.id()), CI.ContainerEntityId, MagInst.AmmoCount, InvId);
					if (CI.ContainerEntityId != InvId) return;
					++DbgInInv;
					if (MagInst.AmmoCount <= 0) return;
					++DbgHasAmmo;
					if (static_cast<int64>(MagEntity.id()) == CurrentMagId) return;

					const FMagazineStatic* MagStatic = MagEntity.try_get<FMagazineStatic>();
					if (!MagStatic) return;
					++DbgHasStatic;
					if (!Static->AcceptsCaliber(MagStatic->CaliberId)) return;
					++DbgCaliberMatch;

					if (MagInst.AmmoCount > BestAmmoCount)
					{
						BestAmmoCount = MagInst.AmmoCount;
						BestMagId = static_cast<int64>(MagEntity.id());
					}
				});

				if (BestMagId == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("WEAPON: No compatible magazine. InvId=%lld Total=%d InInv=%d HasAmmo=%d HasStatic=%d CaliberOK=%d WeaponCalibers=%d[%d,%d,%d,%d]"),
						InvId, DbgTotal, DbgInInv, DbgHasAmmo, DbgHasStatic, DbgCaliberMatch,
						Static->AcceptedCaliberCount,
						Static->AcceptedCaliberIds[0], Static->AcceptedCaliberIds[1],
						Static->AcceptedCaliberIds[2], Static->AcceptedCaliberIds[3]);
					return;
				}

				// Check if current magazine + chamber is empty (for chambering decision)
				// Tactical reload (has chambered round or magazine has ammo) skips chambering
				Weapon.bPrevMagWasEmpty = !Weapon.bChambered;
				if (Weapon.bPrevMagWasEmpty && Weapon.InsertedMagazineId != 0)
				{
					flecs::entity CurMag = Entity.world().entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
					if (CurMag.is_valid())
					{
						const FMagazineInstance* CurMagInst = CurMag.try_get<FMagazineInstance>();
						if (CurMagInst && CurMagInst->AmmoCount > 0)
							Weapon.bPrevMagWasEmpty = false;
					}
				}

				// Get magazine's reload speed modifier
				float MagSpeedMod = 1.f;
				flecs::entity SelMag = Entity.world().entity(static_cast<flecs::entity_t>(BestMagId));
				if (SelMag.is_valid())
				{
					const FMagazineStatic* MagSt = SelMag.try_get<FMagazineStatic>();
					if (MagSt) MagSpeedMod = MagSt->ReloadSpeedModifier;
				}

				// Enter RemovingMag phase
				Weapon.SelectedMagazineId = BestMagId;
				Weapon.ReloadPhase = EWeaponReloadPhase::RemovingMag;
				Weapon.ReloadPhaseTimer = Static->RemoveMagTime * MagSpeedMod;
				Entity.add<FTagReloading>();

				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIReloadMessage ReloadMsg;
					ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
					ReloadMsg.bStarted = true;
					ReloadMsg.MagazineSize = 0; // Will be updated on completion
					MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
				}

				// Update sim→game state cache (reload started)
				SimStateCache.WriteWeapon(static_cast<int64>(Entity.id()), 0, 0, 0, true);

				UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload started (RemovingMag, %.2fs)"), Weapon.ReloadPhaseTimer);
			}
			Weapon.bReloadRequested = false; // consume even if not processed

			// ── Handle reload cancel ──
			if (Weapon.bReloadCancelRequested && Weapon.ReloadPhase != EWeaponReloadPhase::Idle)
			{
				Weapon.bReloadCancelRequested = false;

				if (Weapon.ReloadPhase == EWeaponReloadPhase::RemovingMag)
				{
					// Cancel during remove: magazine stays in weapon
					Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
					Weapon.ReloadPhaseTimer = 0.f;
					Weapon.SelectedMagazineId = 0;
					Entity.remove<FTagReloading>();
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload cancelled (mag stays)"));
				}
				else if (Weapon.ReloadPhase == EWeaponReloadPhase::InsertingMag)
				{
					// Cancel during insert: old mag already in inventory, weapon is EMPTY
					Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
					Weapon.ReloadPhaseTimer = 0.f;
					Weapon.SelectedMagazineId = 0;
					Entity.remove<FTagReloading>();
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload cancelled (weapon empty!)"));
				}
				// Chambering is non-cancellable
			}
			Weapon.bReloadCancelRequested = false;

			// ── Tick reload phase timer ──
			if (Weapon.ReloadPhase == EWeaponReloadPhase::Idle) return;

			Weapon.ReloadPhaseTimer -= DeltaTime;
			if (Weapon.ReloadPhaseTimer > 0.f) return;

			// ── Phase transitions ──
			switch (Weapon.ReloadPhase)
			{
			case EWeaponReloadPhase::RemovingMag:
			{
				// Old magazine removed from weapon → return to inventory
				const FEquippedBy* Equip = Entity.try_get<FEquippedBy>();
				if (Equip && Equip->IsEquipped() && Weapon.InsertedMagazineId != 0)
				{
					flecs::entity CharEntity = Entity.world().entity(static_cast<flecs::entity_t>(Equip->CharacterEntityId));
					if (CharEntity.is_valid())
					{
						const FCharacterInventoryRef* InvRef = CharEntity.try_get<FCharacterInventoryRef>();
						if (InvRef && InvRef->InventoryEntityId != 0)
						{
							if (UFlecsContainerLibrary::PlaceExistingEntityInContainer(
								this, Weapon.InsertedMagazineId, InvRef->InventoryEntityId))
							{
								UE_LOG(LogTemp, Log, TEXT("WEAPON: Old magazine %lld returned to inventory"), Weapon.InsertedMagazineId);
							}
							else
							{
								UE_LOG(LogTemp, Warning, TEXT("WEAPON: Failed to return magazine %lld to inventory (no space?)"), Weapon.InsertedMagazineId);
							}
						}
					}
				}

				Weapon.InsertedMagazineId = 0; // Weapon now has no magazine

				// Transition to InsertingMag
				float MagSpeedMod = 1.f;
				flecs::entity SelMag = Entity.world().entity(static_cast<flecs::entity_t>(Weapon.SelectedMagazineId));
				if (SelMag.is_valid())
				{
					const FMagazineStatic* MagSt = SelMag.try_get<FMagazineStatic>();
					if (MagSt) MagSpeedMod = MagSt->ReloadSpeedModifier;
				}

				Weapon.ReloadPhase = EWeaponReloadPhase::InsertingMag;
				Weapon.ReloadPhaseTimer = Static->InsertMagTime * MagSpeedMod;
				UE_LOG(LogTemp, Log, TEXT("WEAPON: InsertingMag phase (%.2fs)"), Weapon.ReloadPhaseTimer);
				break;
			}

			case EWeaponReloadPhase::InsertingMag:
			{
				// New magazine inserted into weapon
				Weapon.InsertedMagazineId = Weapon.SelectedMagazineId;
				Weapon.SelectedMagazineId = 0;

				// Remove magazine from inventory container (it's now "in the weapon")
				flecs::entity NewMag = Entity.world().entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
				if (NewMag.is_valid())
				{
					NewMag.remove<FContainedIn>();
				}

				// Check if chambering is needed
				if (Static->bHasChamber && Weapon.bPrevMagWasEmpty)
				{
					Weapon.ReloadPhase = EWeaponReloadPhase::Chambering;
					Weapon.ReloadPhaseTimer = Static->ChamberTime;
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Chambering phase (%.2fs)"), Weapon.ReloadPhaseTimer);
				}
				else
				{
					// Tactical reload: preserve existing chambered round
					// Only chamber from new magazine if chamber is empty (shouldn't happen in tactical, but safety)
					if (Static->bHasChamber && !Weapon.bChambered && NewMag.is_valid())
					{
						FMagazineInstance* MI = NewMag.try_get_mut<FMagazineInstance>();
						if (MI && MI->AmmoCount > 0)
						{
							int32 Idx = MI->Pop();
							Weapon.bChambered = true;
							Weapon.ChamberedAmmoTypeIdx = static_cast<uint8>(Idx);
						}
					}

					Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
					Entity.remove<FTagReloading>();

					// Notify UI
					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage ReloadMsg;
						ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
						ReloadMsg.bStarted = false;

						if (NewMag.is_valid())
						{
							const FMagazineInstance* MI = NewMag.try_get<FMagazineInstance>();
							const FMagazineStatic* MS = NewMag.try_get<FMagazineStatic>();
							// +1 for chambered round in UI
							ReloadMsg.NewAmmo = (MI ? MI->AmmoCount : 0) + (Weapon.bChambered ? 1 : 0);
							ReloadMsg.MagazineSize = (MS ? MS->Capacity : 0) + (Static->bHasChamber ? 1 : 0);
						}
						MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
					}

					// Update ammo counter on HUD
					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIAmmoMessage AmmoMsg;
						AmmoMsg.WeaponEntityId = static_cast<int64>(Entity.id());
						if (NewMag.is_valid())
						{
							const FMagazineInstance* MI = NewMag.try_get<FMagazineInstance>();
							const FMagazineStatic* MS = NewMag.try_get<FMagazineStatic>();
							AmmoMsg.CurrentAmmo = (MI ? MI->AmmoCount : 0) + (Weapon.bChambered ? 1 : 0);
							AmmoMsg.MagazineSize = (MS ? MS->Capacity : 0) + (Static->bHasChamber ? 1 : 0);
						}
						MsgSub->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
					}

					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload complete (tactical, chambered=%d)"), Weapon.bChambered);
				}
				break;
			}

			case EWeaponReloadPhase::Chambering:
			{
				// Chambering complete — chamber first round from magazine
				flecs::entity InsertedMag = Entity.world().entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
				if (InsertedMag.is_valid())
				{
					FMagazineInstance* MI = InsertedMag.try_get_mut<FMagazineInstance>();
					if (MI && MI->AmmoCount > 0)
					{
						int32 Idx = MI->Pop();
						Weapon.bChambered = true;
						Weapon.ChamberedAmmoTypeIdx = static_cast<uint8>(Idx);
					}
				}

				Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
				Entity.remove<FTagReloading>();

				// Notify UI
				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIReloadMessage ReloadMsg;
					ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
					ReloadMsg.bStarted = false;

					if (InsertedMag.is_valid())
					{
						const FMagazineInstance* MI = InsertedMag.try_get<FMagazineInstance>();
						const FMagazineStatic* MS = InsertedMag.try_get<FMagazineStatic>();
						ReloadMsg.NewAmmo = (MI ? MI->AmmoCount : 0) + (Weapon.bChambered ? 1 : 0);
						ReloadMsg.MagazineSize = (MS ? MS->Capacity : 0) + (Static->bHasChamber ? 1 : 0);
					}
					MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
				}

				// Update ammo counter on HUD
				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIAmmoMessage AmmoMsg;
					AmmoMsg.WeaponEntityId = static_cast<int64>(Entity.id());
					if (InsertedMag.is_valid())
					{
						const FMagazineInstance* MI = InsertedMag.try_get<FMagazineInstance>();
						const FMagazineStatic* MS = InsertedMag.try_get<FMagazineStatic>();
						AmmoMsg.CurrentAmmo = (MI ? MI->AmmoCount : 0) + (Weapon.bChambered ? 1 : 0);
						AmmoMsg.MagazineSize = (MS ? MS->Capacity : 0) + (Static->bHasChamber ? 1 : 0);
					}
					MsgSub->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
				}

				UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload complete (chambered=%d)"), Weapon.bChambered);
				break;
			}

			default:
				break;
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
			if (!Static) return;

			// Unlimited ammo requires ProjectileDefinition on weapon
			if (Static->bUnlimitedAmmo && !Static->ProjectileDefinition) return;

			// Check fire request: continuous hold OR pending trigger (survives Start+Stop batching)
			if (!Weapon.bFireRequested && !Weapon.bFireTriggerPending) return;

			// Semi-auto: block if already fired while trigger held
			if (!Static->bIsAutomatic && !Static->bIsBurst && Weapon.bHasFiredSincePress)
			{
				UE_LOG(LogTemp, Warning, TEXT("WEAPON: Blocked by semi-auto (bHasFiredSincePress=true)"));
				return;
			}

			// Check if can fire (cooldown expired, has magazine, not reloading)
			if (!Weapon.CanFire())
			{
				return;
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

				// Apply bloom: perturb direction per pellet
				FVector PelletDirection = SpawnDirection;
				if (SpreadRadians > KINDA_SMALL_NUMBER)
				{
					PelletDirection = FMath::VRandCone(SpawnDirection, SpreadRadians);
				}
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
