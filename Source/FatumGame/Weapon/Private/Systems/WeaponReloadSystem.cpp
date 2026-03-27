// FlecsArtillerySubsystem - WeaponReloadSystem
// Phase-based reload state machine: Remove → Insert → Chamber.

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsWeaponComponents.h"
#include "FlecsItemComponents.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsMovementStatic.h"
#include "FlecsMovementComponents.h"
#include "FlecsContainerLibrary.h"
#include "FlecsVitalsComponents.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"

void UFlecsArtillerySubsystem::SetupWeaponReloadSystem()
{
	flecs::world& World = *FlecsWorld;

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

			// ── Handle reload request (Idle → first phase) ──
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

				if (Static->IsSingleRoundReload())
				{
					// ── SINGLE-ROUND RELOAD (shotgun/revolver) ──
					// Magazine stays in weapon (internal tube/cylinder). Push rounds into it.
					if (Weapon.InsertedMagazineId == 0) return;

					flecs::entity MagEntity = Entity.world().entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
					if (!MagEntity.is_valid()) return;

					const FMagazineStatic* MagStatic = MagEntity.try_get<FMagazineStatic>();
					const FMagazineInstance* MagInst = MagEntity.try_get<FMagazineInstance>();
					if (!MagStatic || !MagInst) return;

					// Already full — nothing to reload
					if (MagInst->IsFull(MagStatic->Capacity))
					{
						if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
						{
							FUIReloadMessage Msg;
							Msg.WeaponEntityId = static_cast<int64>(Entity.id());
							Msg.bStarted = false;
							MsgSub->EnqueueMessage(TAG_UI_Reload, Msg);
						}
						return;
					}

					// TODO: Check inventory for compatible loose ammo.
					// For now, single-round reload uses ammo type index 0 directly.

					Weapon.RoundsInsertedThisReload = 0;
					Entity.add<FTagReloading>();

					if (Static->OpenTime > 0.f)
					{
						Weapon.ReloadPhase = EWeaponReloadPhase::Opening;
						Weapon.ReloadPhaseTimer = Static->OpenTime;
						UE_LOG(LogTemp, Log, TEXT("WEAPON: Single-round reload started (Opening, %.2fs)"), Static->OpenTime);
					}
					else
					{
						Weapon.ReloadPhase = EWeaponReloadPhase::InsertingRound;
						Weapon.ReloadPhaseTimer = Static->InsertRoundTime;
						UE_LOG(LogTemp, Log, TEXT("WEAPON: Single-round reload started (InsertingRound, %.2fs)"), Static->InsertRoundTime);
					}

					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage ReloadMsg;
						ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
						ReloadMsg.bStarted = true;
						ReloadMsg.MagazineSize = 0;
						MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
					}

					SimStateCache.WriteWeapon(static_cast<int64>(Entity.id()), 0, 0, 0, true);
				}
				else
				{
					// ── MAGAZINE RELOAD (standard) ──
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

						// Notify game thread that reload didn't start (clears ActionBit::Reloading)
						if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
						{
							FUIReloadMessage Msg;
							Msg.WeaponEntityId = static_cast<int64>(Entity.id());
							Msg.bStarted = false;
							MsgSub->EnqueueMessage(TAG_UI_Reload, Msg);
						}
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
			}
			Weapon.bReloadRequested = false; // consume even if not processed

			// ── Handle reload cancel ──
			if (Weapon.bReloadCancelRequested && Weapon.ReloadPhase != EWeaponReloadPhase::Idle)
			{
				if (Weapon.ReloadPhase == EWeaponReloadPhase::RemovingMag)
				{
					// Cancel during remove: magazine stays in weapon
					Weapon.bReloadCancelRequested = false;
					Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
					Weapon.ReloadPhaseTimer = 0.f;
					Weapon.SelectedMagazineId = 0;
					Entity.remove<FTagReloading>();

					// Notify game thread (clears ActionBit::Reloading)
					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage Msg;
						Msg.WeaponEntityId = static_cast<int64>(Entity.id());
						Msg.bStarted = false;
						MsgSub->EnqueueMessage(TAG_UI_Reload, Msg);
					}
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload cancelled (mag stays)"));
				}
				else if (Weapon.ReloadPhase == EWeaponReloadPhase::InsertingMag)
				{
					// Cancel during insert: old mag already in inventory, weapon is EMPTY
					Weapon.bReloadCancelRequested = false;
					Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
					Weapon.ReloadPhaseTimer = 0.f;
					Weapon.SelectedMagazineId = 0;
					Entity.remove<FTagReloading>();

					// Notify game thread (clears ActionBit::Reloading)
					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage Msg;
						Msg.WeaponEntityId = static_cast<int64>(Entity.id());
						Msg.bStarted = false;
						MsgSub->EnqueueMessage(TAG_UI_Reload, Msg);
					}
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload cancelled (weapon empty!)"));
				}
				else if (Weapon.ReloadPhase == EWeaponReloadPhase::Opening)
				{
					// Cancel during Opening: instant abort, no Close needed (nothing committed yet)
					Weapon.bReloadCancelRequested = false;
					Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
					Weapon.ReloadPhaseTimer = 0.f;
					Weapon.RoundsInsertedThisReload = 0;
					Entity.remove<FTagReloading>();

					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage Msg;
						Msg.WeaponEntityId = static_cast<int64>(Entity.id());
						Msg.bStarted = false;
						MsgSub->EnqueueMessage(TAG_UI_Reload, Msg);
					}
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Single-round reload cancelled during Opening"));
				}
				else if (Weapon.ReloadPhase == EWeaponReloadPhase::InsertingRound)
				{
					// Cancel during InsertingRound: let current round finish (timer expires),
					// then Close phase begins. Leave bReloadCancelRequested set — the
					// InsertingRound phase transition will consume it.
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Single-round reload cancel requested during InsertingRound"));
				}
				else
				{
					// Closing and Chambering are non-cancellable — just consume the flag
					Weapon.bReloadCancelRequested = false;
				}
			}

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

			// ── Single-round reload phases ──

			case EWeaponReloadPhase::Opening:
			{
				// Opening complete → start inserting rounds
				Weapon.ReloadPhase = EWeaponReloadPhase::InsertingRound;
				Weapon.ReloadPhaseTimer = Static->InsertRoundTime;
				UE_LOG(LogTemp, Log, TEXT("WEAPON: Opening complete → InsertingRound (%.2fs)"), Static->InsertRoundTime);
				break;
			}

			case EWeaponReloadPhase::InsertingRound:
			{
				// Round insertion timer expired → find ammo in inventory and commit one round
				checkf(Weapon.InsertedMagazineId != 0, TEXT("InsertingRound: No magazine in weapon"));
				flecs::entity MagEntity = Entity.world().entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
				checkf(MagEntity.is_valid(), TEXT("InsertingRound: Magazine entity invalid"));

				FMagazineInstance* MagInst = MagEntity.try_get_mut<FMagazineInstance>();
				const FMagazineStatic* MagStatic = MagEntity.try_get<FMagazineStatic>();
				checkf(MagInst && MagStatic, TEXT("InsertingRound: Magazine missing components"));

				// Find compatible ammo in character's inventory
				const FEquippedBy* Equip = Entity.try_get<FEquippedBy>();
				flecs::entity CharEntity = Equip ? Entity.world().entity(
					static_cast<flecs::entity_t>(Equip->CharacterEntityId)) : flecs::entity();
				const FCharacterInventoryRef* InvRef = CharEntity.is_valid()
					? CharEntity.try_get<FCharacterInventoryRef>() : nullptr;
				const int64 InvId = InvRef ? InvRef->InventoryEntityId : 0;

				// Search inventory for a loose ammo item matching magazine's accepted types
				flecs::entity FoundAmmoEntity;
				int32 FoundAmmoTypeIdx = -1;
				bool bRoundInserted = false;

				if (InvId != 0)
				{
					Entity.world().each([&](flecs::entity AmmoEntity, const FContainedIn& CI,
						FItemInstance& Item)
					{
						if (FoundAmmoEntity.is_valid()) return;  // already found one
						if (CI.ContainerEntityId != InvId) return;  // wrong container
						if (Item.Count <= 0) return;  // empty stack

						// Get AmmoTypeDefinition via prefab → EntityDefinitionRef
						const FEntityDefinitionRef* DefRef = AmmoEntity.try_get<FEntityDefinitionRef>();
						if (!DefRef || !DefRef->Definition || !DefRef->Definition->AmmoTypeDefinition) return;

						// Check if this ammo type is accepted by the magazine
						int32 Idx = MagStatic->FindAmmoTypeIndex(DefRef->Definition->AmmoTypeDefinition);
						if (Idx < 0) return;  // incompatible

						FoundAmmoEntity = AmmoEntity;
						FoundAmmoTypeIdx = Idx;
					});
				}

				if (FoundAmmoEntity.is_valid() && FoundAmmoTypeIdx >= 0)
				{
					// Consume one round from inventory
					FItemInstance* AmmoItem = FoundAmmoEntity.try_get_mut<FItemInstance>();
					if (AmmoItem)
					{
						AmmoItem->Count -= 1;
						if (AmmoItem->Count <= 0)
						{
							// Stack depleted — free grid space and destroy entity
							const FContainedIn* AmmoCI = FoundAmmoEntity.try_get<FContainedIn>();
							if (AmmoCI && AmmoCI->ContainerEntityId != 0)
							{
								flecs::entity AmmoContainer = Entity.world().entity(
									static_cast<flecs::entity_t>(AmmoCI->ContainerEntityId));
								if (AmmoContainer.is_valid())
								{
									FContainerGridInstance* Grid = AmmoContainer.try_get_mut<FContainerGridInstance>();
									const FItemStaticData* ASD = FoundAmmoEntity.try_get<FItemStaticData>();
									if (Grid && ASD && AmmoCI->IsInGrid())
										Grid->Free(AmmoCI->GridPosition, ASD->GridSize, 10); // TODO: read actual width

									FContainerInstance* CI = AmmoContainer.try_get_mut<FContainerInstance>();
									if (CI) CI->CurrentCount = FMath::Max(0, CI->CurrentCount - 1);
								}
							}
							FoundAmmoEntity.destruct();
						}
					}

					// Push round into magazine
					MagInst->Push(static_cast<uint8>(FoundAmmoTypeIdx));
					++Weapon.RoundsInsertedThisReload;
					bRoundInserted = true;

					UE_LOG(LogTemp, Log, TEXT("WEAPON: Inserted round %d (mag now %d/%d, ammoType=%d)"),
						Weapon.RoundsInsertedThisReload, MagInst->AmmoCount, MagStatic->Capacity, FoundAmmoTypeIdx);
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("WEAPON: No compatible ammo in inventory — ending reload"));
				}

				// Send per-round ammo update
				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIAmmoMessage AmmoMsg;
					AmmoMsg.WeaponEntityId = static_cast<int64>(Entity.id());
					AmmoMsg.CurrentAmmo = MagInst->AmmoCount + (Weapon.bChambered ? 1 : 0);
					AmmoMsg.MagazineSize = MagStatic->Capacity + (Static->bHasChamber ? 1 : 0);
					MsgSub->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
				}

				// Decide: continue inserting, or transition to Close/Idle?
				const bool bFull = MagInst->IsFull(MagStatic->Capacity);
				const bool bNoAmmo = !bRoundInserted;  // no more ammo in inventory
				const bool bCancel = Weapon.bReloadCancelRequested
					|| Weapon.bFireRequested
					|| Weapon.bFireTriggerPending;

				if (bFull || bCancel || bNoAmmo)
				{
					Weapon.bReloadCancelRequested = false;

					if (Static->CloseTime > 0.f)
					{
						Weapon.ReloadPhase = EWeaponReloadPhase::Closing;
						Weapon.ReloadPhaseTimer = Static->CloseTime;
						UE_LOG(LogTemp, Log, TEXT("WEAPON: InsertingRound → Closing (%.2fs)%s"),
							Static->CloseTime, bCancel ? TEXT(" [cancelled]") : TEXT(" [full]"));
					}
					else
					{
						// No close phase — finish immediately
						Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
						Weapon.RoundsInsertedThisReload = 0;
						Entity.remove<FTagReloading>();

						if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
						{
							FUIReloadMessage ReloadMsg;
							ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
							ReloadMsg.bStarted = false;
							ReloadMsg.NewAmmo = MagInst->AmmoCount + (Weapon.bChambered ? 1 : 0);
							ReloadMsg.MagazineSize = MagStatic->Capacity + (Static->bHasChamber ? 1 : 0);
							MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
						}
						UE_LOG(LogTemp, Log, TEXT("WEAPON: Single-round reload complete (%d rounds)"), Weapon.RoundsInsertedThisReload);
					}
				}
				else
				{
					// Loop: insert another round
					Weapon.ReloadPhaseTimer = Static->InsertRoundTime;
				}
				break;
			}

			case EWeaponReloadPhase::Closing:
			{
				// Closing complete → return to Idle
				Weapon.ReloadPhase = EWeaponReloadPhase::Idle;
				Entity.remove<FTagReloading>();

				// Send final ammo update
				flecs::entity MagEntity = Entity.world().entity(static_cast<flecs::entity_t>(Weapon.InsertedMagazineId));
				int32 FinalAmmo = Weapon.bChambered ? 1 : 0;
				int32 FinalMagSize = Static->bHasChamber ? 1 : 0;
				if (MagEntity.is_valid())
				{
					const FMagazineInstance* MagInst = MagEntity.try_get<FMagazineInstance>();
					const FMagazineStatic* MagStatic = MagEntity.try_get<FMagazineStatic>();
					if (MagInst) FinalAmmo += MagInst->AmmoCount;
					if (MagStatic) FinalMagSize += MagStatic->Capacity;
				}

				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIReloadMessage ReloadMsg;
					ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
					ReloadMsg.bStarted = false;
					ReloadMsg.NewAmmo = FinalAmmo;
					ReloadMsg.MagazineSize = FinalMagSize;
					MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
				}

				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIAmmoMessage AmmoMsg;
					AmmoMsg.WeaponEntityId = static_cast<int64>(Entity.id());
					AmmoMsg.CurrentAmmo = FinalAmmo;
					AmmoMsg.MagazineSize = FinalMagSize;
					MsgSub->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
				}

				UE_LOG(LogTemp, Log, TEXT("WEAPON: Closing complete, single-round reload done (%d rounds)"),
					Weapon.RoundsInsertedThisReload);
				Weapon.RoundsInsertedThisReload = 0;
				break;
			}

			default:
				break;
			}
		});
}
