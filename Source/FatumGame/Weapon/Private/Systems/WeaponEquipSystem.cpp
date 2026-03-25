// WeaponEquipSystem — ticks equip/holster timer for weapon slot switching.
// Runs on sim thread, before WeaponTickSystem.

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsWeaponComponents.h"
#include "FlecsItemComponents.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsWeaponProfile.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FSimStateCache.h"

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
			if (SlotState.WeaponSlotContainerId == 0) return;
			flecs::entity Container = CharEntity.world().entity(
				static_cast<flecs::entity_t>(SlotState.WeaponSlotContainerId));
			if (!Container.is_valid() || !Container.is_alive()) return;

			const FContainerSlotsInstance* Slots = Container.try_get<FContainerSlotsInstance>();
			if (!Slots) return;

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

				// Read EquipTime from target weapon for draw phase
				const FWeaponStatic* WS = NewWeapon.try_get<FWeaponStatic>();
				float DrawTime = WS ? WS->EquipTime * 0.5f : 0.25f;

				SlotState.EquipPhase = EWeaponEquipPhase::Drawing;
				SlotState.EquipTimer = DrawTime;
				break;
			}

			case EWeaponEquipPhase::Drawing:
			{
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

				// Equip weapon
				FEquippedBy Eq;
				Eq.CharacterEntityId = static_cast<int64>(CharEntity.id());
				Eq.SlotId = SlotState.PendingSlotIndex;
				NewWeapon.set<FEquippedBy>(Eq);

				SlotState.ActiveSlotIndex = SlotState.PendingSlotIndex;
				SlotState.PendingSlotIndex = -1;
				SlotState.EquipPhase = EWeaponEquipPhase::Idle;

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
