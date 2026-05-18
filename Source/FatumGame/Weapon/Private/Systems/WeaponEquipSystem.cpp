// WeaponEquipSystem — ticks equip/holster timer for weapon slot switching.
// Runs on sim thread, before WeaponTickSystem.

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsWeaponComponents.h"
#include "FlecsItemComponents.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsWeaponProfile.h"
#include "FlecsMeleeComponents.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FSimStateCache.h"
#include "Library/FlecsMeleeEquipHelpers.h"  // AllocateBladeBufferForMeleeEntity (Phase 5 extract)

void UFlecsArtillerySubsystem::SetupWeaponEquipSystem()
{
	flecs::world& World = *FlecsWorld;

	World.system<FWeaponSlotState>("WeaponEquipSystem")
		.with<FTagCharacter>()
		.without<FTagDead>()
		.each([this](flecs::entity CharEntity, FWeaponSlotState& SlotState)
		{
			if (SlotState.EquipPhase == EWeaponEquipPhase::Idle) return;

			const float DeltaTime = CharEntity.world().get_info()->delta_time;
			SlotState.EquipTimer -= DeltaTime;
			if (SlotState.EquipTimer > 0.f) return;

			// Resolve weapon slot container
			if (SlotState.WeaponSlotContainerId == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem: WeaponSlotContainerId == 0 → ABORT phase=%d"),
					(int32)SlotState.EquipPhase);
				return;
			}
			flecs::entity Container = CharEntity.world().entity(
				static_cast<flecs::entity_t>(SlotState.WeaponSlotContainerId));
			if (!Container.is_valid() || !Container.is_alive())
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem: Container %lld invalid/dead → ABORT"),
					SlotState.WeaponSlotContainerId);
				return;
			}

			const FContainerSlotsInstance* Slots = Container.try_get<FContainerSlotsInstance>();
			if (!Slots)
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem: Container has NO FContainerSlotsInstance → ABORT"));
				return;
			}

			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem: phase=%d timer expired, processing..."),
				(int32)SlotState.EquipPhase);

			switch (SlotState.EquipPhase)
			{
			case EWeaponEquipPhase::Holstering:
			{
				// Remove FEquippedBy from old weapon
				if (SlotState.ActiveSlotIndex >= 0)
				{
					int64 OldWeaponId = Slots->GetItemInSlot(SlotState.ActiveSlotIndex);
					if (OldWeaponId != 0)
					{
						flecs::entity OldWeapon = CharEntity.world().entity(
							static_cast<flecs::entity_t>(OldWeaponId));
						if (OldWeapon.is_valid() && OldWeapon.is_alive())
						{
							OldWeapon.remove<FEquippedBy>();
							FWeaponInstance* WI = OldWeapon.try_get_mut<FWeaponInstance>();
							if (WI)
							{
								WI->bFireRequested = false;
								WI->bFireTriggerPending = false;
								WI->bReloadRequested = false;
								WI->bTriggerPulling = false;
								WI->TriggerPullTimer = 0.f;
								// Stop cycling timer but preserve bNeedsCycle (resumes on re-equip)
								WI->bCycling = false;
								WI->CycleTimeRemaining = 0.f;
								// Clear all charge state on holster
								WI->bIsCharging = false;
								WI->ChargeAccumulator = 0.f;
								WI->bWasFireRequestedLastTick = false;
								WI->bPendingAutoRestart = false;
								WI->PendingPayload = FChargeShotPayload{};
								WI->LatchedPayload = FChargeShotPayload{};
							}
							if (OldWeapon.has<FTagChargingWeapon>())
							{
								OldWeapon.remove<FTagChargingWeapon>();
							}

							// Melee weapon holster — mirror ranged field-by-field clear.
							// BladeBuffer stays allocated across holster (§E.2 "holster persists");
							// only reset phase/charge state + drop active tags. Buffer is freed
							// only on unequip-from-inventory / entity death via the on_remove hook.
							FMeleeWeaponInstance* MWI = OldWeapon.try_get_mut<FMeleeWeaponInstance>();
							if (MWI)
							{
								MWI->ResetAllChargeAndSwingState();
							}
							if (OldWeapon.has<FTagMeleeAttacking>())
							{
								OldWeapon.remove<FTagMeleeAttacking>();
							}
							if (OldWeapon.has<FTagMeleeCharging>())
							{
								OldWeapon.remove<FTagMeleeCharging>();
							}
						}
					}
				}

				SlotState.ActiveSlotIndex = -1;

				// Check if switching to a new slot or just unequipping
				if (SlotState.PendingSlotIndex < 0)
				{
					SlotState.EquipPhase = EWeaponEquipPhase::Idle;
					EnqueueWeaponEquipSignal(CharEntity, 0, -1, nullptr, nullptr, FTransform::Identity);
					return;
				}

				// Look up target weapon for draw phase
				int64 NewWeaponId = Slots->GetItemInSlot(SlotState.PendingSlotIndex);
				if (NewWeaponId == 0)
				{
					SlotState.PendingSlotIndex = -1;
					SlotState.EquipPhase = EWeaponEquipPhase::Idle;
					EnqueueWeaponEquipSignal(CharEntity, 0, -1, nullptr, nullptr, FTransform::Identity);
					return;
				}

				flecs::entity NewWeapon = CharEntity.world().entity(
					static_cast<flecs::entity_t>(NewWeaponId));
				if (!NewWeapon.is_valid() || !NewWeapon.is_alive())
				{
					SlotState.PendingSlotIndex = -1;
					SlotState.EquipPhase = EWeaponEquipPhase::Idle;
					EnqueueWeaponEquipSignal(CharEntity, 0, -1, nullptr, nullptr, FTransform::Identity);
					return;
				}

				// Read EquipTime from target weapon for draw phase.
				// Melee weapons have no EquipTime field — use a fixed default matching the
				// ranged fallback until designer tuning surfaces a dedicated melee value.
				const FWeaponStatic* WS = NewWeapon.try_get<FWeaponStatic>();
				const FMeleeWeaponStatic* MWS = NewWeapon.try_get<FMeleeWeaponStatic>();
				float DrawTime = WS ? WS->EquipTime * 0.5f : (MWS ? 0.25f : 0.25f);

				SlotState.EquipPhase = EWeaponEquipPhase::Drawing;
				SlotState.EquipTimer = DrawTime;
				break;
			}

			case EWeaponEquipPhase::Drawing:
			{
				int64 NewWeaponId = Slots->GetItemInSlot(SlotState.PendingSlotIndex);
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem DRAW: pending=%d weaponId=%lld"),
					SlotState.PendingSlotIndex, NewWeaponId);
				if (NewWeaponId == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem DRAW: slot %d EMPTY → ABORT (melee not in that slot?)"),
						SlotState.PendingSlotIndex);
					SlotState.PendingSlotIndex = -1;
					SlotState.EquipPhase = EWeaponEquipPhase::Idle;
					EnqueueWeaponEquipSignal(CharEntity, 0, -1, nullptr, nullptr, FTransform::Identity);
					return;
				}

				flecs::entity NewWeapon = CharEntity.world().entity(
					static_cast<flecs::entity_t>(NewWeaponId));
				if (!NewWeapon.is_valid() || !NewWeapon.is_alive())
				{
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem DRAW: entity %lld INVALID/DEAD → ABORT"),
						NewWeaponId);
					SlotState.PendingSlotIndex = -1;
					SlotState.EquipPhase = EWeaponEquipPhase::Idle;
					EnqueueWeaponEquipSignal(CharEntity, 0, -1, nullptr, nullptr, FTransform::Identity);
					return;
				}

				const bool bHasMeleeStatic = NewWeapon.has<FMeleeWeaponStatic>();
				const bool bHasRangedStatic = NewWeapon.has<FWeaponStatic>();
				const bool bHasMeleeInst = NewWeapon.has<FMeleeWeaponInstance>();
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem DRAW: entity %lld components: MeleeStatic=%d RangedStatic=%d MeleeInst=%d"),
					NewWeaponId, bHasMeleeStatic ? 1 : 0, bHasRangedStatic ? 1 : 0, bHasMeleeInst ? 1 : 0);

				// Equip weapon
				FEquippedBy Eq;
				Eq.CharacterEntityId = static_cast<int64>(CharEntity.id());
				Eq.SlotId = SlotState.PendingSlotIndex;
				NewWeapon.set<FEquippedBy>(Eq);

				// Auto-cycle on equip if weapon still needs cycling from previous use
				{
					FWeaponInstance* NewWI = NewWeapon.try_get_mut<FWeaponInstance>();
					if (NewWI && NewWI->bNeedsCycle && !NewWI->bCycling)
					{
						const FWeaponStatic* NewWS = NewWeapon.try_get<FWeaponStatic>();
						if (NewWS && NewWS->bRequiresCycling)
						{
							NewWI->bCycling = true;
							NewWI->CycleTimeRemaining = NewWS->CycleTime;
						}
					}
				}

				SlotState.ActiveSlotIndex = SlotState.PendingSlotIndex;
				SlotState.PendingSlotIndex = -1;
				SlotState.EquipPhase = EWeaponEquipPhase::Idle;

				// ─────────────────────────────────────────────────────────
				// MELEE EQUIP FINALIZATION (§F.5)
				// Dispatch on presence of FMeleeWeaponStatic. Ranged-only paths below
				// (SimStateCache ammo, WeaponProfile signal) are skipped for melee —
				// melee has no ammo and no ADS/recoil profile. Blade trail and attach
				// offset are resolved via a separate path in Phase 7.
				// ─────────────────────────────────────────────────────────
				if (NewWeapon.has<FMeleeWeaponStatic>())
				{
					// Allocate per-weapon blade-socket triple buffer. Writer: game thread
					// (AFlecsCharacter::Tick). Reader: MeleeSweepSystem. Freed by the
					// on_remove hook registered in RegisterFlecsComponents (N-C2).
					//
					// Extracted into FlecsMeleeEquip::AllocateBladeBufferForMeleeEntity per
					// v3 §B so the save system's post-decode rebind pass can re-allocate
					// buffers for every loaded FMeleeWeaponInstance entity (the encoder
					// skips the BladeBuffer pointer; the decoder zeros it).
					FlecsMeleeEquip::AllocateBladeBufferForMeleeEntity(NewWeapon);

					// Ensure character carries the per-character direction-sample buffer.
					if (!CharEntity.has<FMeleeAttackDirectionBuffer>())
					{
						CharEntity.set<FMeleeAttackDirectionBuffer>({});
					}

					// Resolve cosmetic attach target. Melee equip uses its own SkeletalMesh
					// (carries BladeStart/BladeTip sockets) — distinct from UFlecsRenderProfile
					// (StaticMesh, used for the world/ISM pickup visual). WeaponProfile stays
					// null — melee has no ADS/recoil profile. Blade trail VFX is attached
					// separately in Phase 7.
					const FMeleeWeaponStatic* MWS = NewWeapon.try_get<FMeleeWeaponStatic>();
					USkeletalMesh* MeleeMesh = MWS ? MWS->EquippedMesh.Get() : nullptr;
					const FTransform MeleeOffset = MWS ? MWS->AttachOffset : FTransform::Identity;
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem MELEE-BRANCH: MWS=%p EquippedMesh=%s offset loc=(%s) → EnqueueWeaponEquipSignal"),
						MWS, MeleeMesh ? *MeleeMesh->GetName() : TEXT("NULL"),
						*MeleeOffset.GetLocation().ToString());
					EnqueueWeaponEquipSignal(CharEntity, NewWeaponId, SlotState.ActiveSlotIndex,
						MeleeMesh, nullptr, MeleeOffset, /*bIsMelee=*/true);

					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] EquipSystem MELEE-BRANCH: DONE (weapon=%lld slot=%d)"),
						NewWeaponId, SlotState.ActiveSlotIndex);
					break;
				}

				// Resolve visual data for game thread
				const FWeaponStatic* WS = NewWeapon.try_get<FWeaponStatic>();
				USkeletalMesh* Mesh = WS ? WS->EquippedMesh : nullptr;
				FTransform AttachOffset = WS ? WS->AttachOffset : FTransform::Identity;

				// Resolve WeaponProfile from EntityDefinitionRef
				const FEntityDefinitionRef* DefRef = NewWeapon.try_get<FEntityDefinitionRef>();
				UFlecsWeaponProfile* WepProfile = (DefRef && DefRef->Definition)
					? DefRef->Definition->WeaponProfile.Get() : nullptr;

				// Register weapon in SimStateCache and send initial ammo
				GetSimStateCache().Register(NewWeaponId);

				int32 CurrentAmmo = 0;
				int32 MagSize = 0;
				if (WS && WS->bHasChamber) MagSize += 1;

				const FWeaponInstance* WI = NewWeapon.try_get<FWeaponInstance>();
				if (WI)
				{
					if (WI->bChambered) CurrentAmmo += 1;
					if (WI->InsertedMagazineId != 0)
					{
						flecs::entity MagE = CharEntity.world().entity(
							static_cast<flecs::entity_t>(WI->InsertedMagazineId));
						if (MagE.is_valid())
						{
							const FMagazineStatic* MS = MagE.try_get<FMagazineStatic>();
							const FMagazineInstance* MI = MagE.try_get<FMagazineInstance>();
							if (MS) MagSize += MS->Capacity;
							if (MI) CurrentAmmo += MI->AmmoCount;
						}
					}
				}

				GetSimStateCache().WriteWeapon(NewWeaponId, CurrentAmmo, MagSize, 0, false);

				if (UFlecsMessageSubsystem::SelfPtr)
				{
					FUIAmmoMessage AmmoMsg;
					AmmoMsg.WeaponEntityId = NewWeaponId;
					AmmoMsg.CurrentAmmo = CurrentAmmo;
					AmmoMsg.MagazineSize = MagSize;
					AmmoMsg.ReserveAmmo = 0;
					UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
				}

				EnqueueWeaponEquipSignal(CharEntity, NewWeaponId, SlotState.ActiveSlotIndex,
					Mesh, WepProfile, AttachOffset);

				UE_LOG(LogTemp, Log, TEXT("WEAPON EQUIP: Drew weapon %lld from slot %d"),
					NewWeaponId, SlotState.ActiveSlotIndex);
				break;
			}

			default: break;
			}
		});
}
