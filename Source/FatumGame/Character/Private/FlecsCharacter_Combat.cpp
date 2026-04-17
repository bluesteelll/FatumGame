// Combat implementation for AFlecsCharacter.
// Health queries/mutations, projectile firing (legacy direct spawn), weapon visual + ECS weapon testing.

#include "FlecsCharacter.h"
#include "FatumMovementComponent.h"
#include "FlecsDamageLibrary.h"
#include "FlecsWeaponLibrary.h"
#include "FlecsMeleeLibrary.h"
#include "FlecsSpawnLibrary.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsRenderProfile.h"
#include "FlecsProjectileProfile.h"
#include "FSimStateCache.h"
#include "FlecsWeaponProfile.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsWeaponComponents.h"
#include "FlecsItemComponents.h"
#include "FlecsMagazineProfile.h"
#include "FlecsAmmoTypeDefinition.h"
#include "FlecsVitalsComponents.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsHUDWidget.h"
#include "FBarragePrimitive.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"

// ═══════════════════════════════════════════════════════════════════════════
// HEALTH
// ═══════════════════════════════════════════════════════════════════════════

float AFlecsCharacter::GetCurrentHealth() const
{
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return CachedHealth;

	FHealthSnapshot Snap;
	if (FlecsSubsystem->GetSimStateCache().ReadHealth(GetCharacterEntityId(), Snap))
		return Snap.CurrentHP;
	return CachedHealth;
}

float AFlecsCharacter::GetHealthPercent() const
{
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return 0.f;

	FHealthSnapshot Snap;
	if (FlecsSubsystem->GetSimStateCache().ReadHealth(GetCharacterEntityId(), Snap))
		return Snap.MaxHP > 0.f ? Snap.CurrentHP / Snap.MaxHP : 0.f;
	return 0.f;
}

bool AFlecsCharacter::IsAlive() const
{
	return GetCurrentHealth() > 0.f;
}

void AFlecsCharacter::ApplyDamage(float Damage)
{
	if (Damage <= 0.f) return;
	UFlecsDamageLibrary::ApplyDamageByBarrageKey(this, GetEntityKey(), Damage);
}

void AFlecsCharacter::Heal(float Amount)
{
	if (Amount <= 0.f) return;
	UFlecsDamageLibrary::HealEntityByBarrageKey(this, GetEntityKey(), Amount);
}

void AFlecsCharacter::CheckHealthChanges()
{
	float CurrentHealth = GetCurrentHealth();

	if (!FMath::IsNearlyEqual(CurrentHealth, CachedHealth, 0.01f))
	{
		float Delta = CurrentHealth - CachedHealth;

		if (Delta < 0.f)
		{
			// Took damage
			OnDamageTaken(-Delta, CurrentHealth);

			// Force cancel active interaction on damage
			if (Interact.State != EInteractionState::Gameplay)
			{
				ForceCancelInteraction();
			}
		}
		else
		{
			// Healed
			OnHealed(Delta, CurrentHealth);
		}

		// Check for death
		if (CurrentHealth <= 0.f && CachedHealth > 0.f)
		{
			HandleDeath();
		}

		CachedHealth = CurrentHealth;
	}
}

void AFlecsCharacter::HandleDeath()
{
	UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: %s died!"), *GetName());
	OnDeath();
}

// ═══════════════════════════════════════════════════════════════════════════
// RESOURCE UI (poll SimStateCache for resource changes)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::UpdateResourceUI()
{
	if (CachedResourcePoolCount == 0 || !HUDWidget) return;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	int64 EntityId = GetCharacterEntityId();
	if (EntityId == 0) return;

	FResourceSnapshot Snap;
	if (!FlecsSubsystem->GetSimStateCache().ReadResources(EntityId, Snap)) return;

	ensureMsgf(Snap.PoolCount == CachedResourcePoolCount,
		TEXT("UpdateResourceUI: Snap.PoolCount=%d != CachedResourcePoolCount=%d"),
		Snap.PoolCount, CachedResourcePoolCount);

	// Detect changes (threshold < one quantization step of 1/255 ≈ 0.00392)
	bool bChanged = false;
	for (int32 p = 0; p < CachedResourcePoolCount; ++p)
	{
		if (FMath::Abs(Snap.Ratios[p] - CachedResourceRatios[p]) > 0.003f)
		{
			bChanged = true;
			break;
		}
	}

	if (!bChanged) return;

	// Update cache from snapshot (all pools at once for consistency)
	for (int32 p = 0; p < CachedResourcePoolCount; ++p)
		CachedResourceRatios[p] = Snap.Ratios[p];

	// Build array for Blueprint directly from snapshot
	TArray<FResourceBarData> Resources;
	Resources.Reserve(CachedResourcePoolCount);
	for (int32 p = 0; p < CachedResourcePoolCount; ++p)
	{
		FResourceBarData Entry;
		Entry.ResourceType = ResourcePoolTypes[p];
		Entry.Ratio = Snap.Ratios[p];
		Entry.Max = ResourcePoolMaxValues[p];
		Entry.Current = FMath::RoundToFloat(Entry.Ratio * Entry.Max);
		Resources.Add(Entry);
	}

	HUDWidget->OnResourcesUpdated(Resources);

	// Fire per-type convenience events
	for (const FResourceBarData& R : Resources)
	{
		if (R.ResourceType == 1) // Mana
			HUDWidget->OnManaChanged(R.Current, R.Max, R.Ratio);
	}
}

void AFlecsCharacter::UpdateVitalsUI()
{
	if (!bHasVitals || !HUDWidget) return;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	int64 EntityId = GetCharacterEntityId();
	if (EntityId == 0) return;

	FVitalsSnapshot Snap;
	if (!FlecsSubsystem->GetSimStateCache().ReadVitals(EntityId, Snap)) return;

	// Detect changes (threshold < one quantization step of 1/65535 ≈ 0.000015, use 0.005 for visible change)
	const bool bChanged =
		FMath::Abs(Snap.HungerPercent - CachedVitalsHunger) > 0.005f ||
		FMath::Abs(Snap.ThirstPercent - CachedVitalsThirst) > 0.005f ||
		FMath::Abs(Snap.WarmthPercent - CachedVitalsWarmth) > 0.005f;

	if (!bChanged) return;

	CachedVitalsHunger = Snap.HungerPercent;
	CachedVitalsThirst = Snap.ThirstPercent;
	CachedVitalsWarmth = Snap.WarmthPercent;

	HUDWidget->OnVitalsUpdated(Snap.HungerPercent, Snap.ThirstPercent, Snap.WarmthPercent);
}

// ═══════════════════════════════════════════════════════════════════════════
// PROJECTILE (legacy direct spawn)
// ═══════════════════════════════════════════════════════════════════════════

FVector AFlecsCharacter::GetMuzzleLocation() const
{
	// Prefer weapon mesh socket if available (follows animation: recoil, sway, etc.)
	if (WeaponMeshComponent && WeaponMeshComponent->GetSkeletalMeshAsset())
	{
		static const FName MuzzleSocketName(TEXT("Muzzle"));
		if (WeaponMeshComponent->DoesSocketExist(MuzzleSocketName))
		{
			return WeaponMeshComponent->GetSocketLocation(MuzzleSocketName);
		}
	}

	// Fallback: camera position + aim-relative offset.
	// Must match sim thread computation (CharacterPosition + AimQuat * MuzzleOffset).
	// Using camera (not ActorLocation) eliminates parallax with crosshair.
	FVector CameraPos = FollowCamera ? FollowCamera->GetComponentLocation() : GetActorLocation();
	FVector AimDir = GetFiringDirection();
	FQuat AimQuat = FRotationMatrix::MakeFromX(AimDir).ToQuat();
	FTransform MuzzleTransform(AimQuat, CameraPos);
	return MuzzleTransform.TransformPosition(MuzzleOffset);
}

FVector AFlecsCharacter::GetFiringDirection() const
{
	if (Controller)
	{
		return Controller->GetControlRotation().Vector();
	}
	return GetActorForwardVector();
}

FSkeletonKey AFlecsCharacter::FireProjectile()
{
	return FireProjectileInDirection(GetFiringDirection());
}

FSkeletonKey AFlecsCharacter::FireProjectileInDirection(FVector Direction)
{
	if (!ProjectileDefinition || !ProjectileDefinition->RenderProfile || !ProjectileDefinition->RenderProfile->Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::FireProjectile - No ProjectileDefinition or RenderProfile with mesh!"));
		return FSkeletonKey();
	}

	if (!ProjectileDefinition->ProjectileProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::FireProjectile - No ProjectileProfile!"));
		return FSkeletonKey();
	}

	Direction.Normalize();

	// Get owner entity ID for friendly fire prevention
	int64 OwnerEntityId = 0;
	if (UFlecsArtillerySubsystem* Subsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>())
	{
		flecs::entity OwnerEntity = Subsystem->GetEntityForBarrageKey(GetEntityKey());
		if (OwnerEntity.is_valid())
		{
			OwnerEntityId = static_cast<int64>(OwnerEntity.id());
		}
	}

	return UFlecsSpawnLibrary::SpawnProjectileFromEntityDef(
		GetWorld(),
		ProjectileDefinition,
		GetMuzzleLocation(),
		Direction,
		ProjectileSpeedOverride,
		OwnerEntityId
	);
}

TArray<FSkeletonKey> AFlecsCharacter::FireProjectileSpread(int32 Count, float SpreadAngle)
{
	TArray<FSkeletonKey> Keys;

	if (!ProjectileDefinition || !ProjectileDefinition->RenderProfile || !ProjectileDefinition->RenderProfile->Mesh)
	{
		return Keys;
	}

	if (!ProjectileDefinition->ProjectileProfile)
	{
		return Keys;
	}

	// Get owner entity ID for friendly fire prevention
	int64 OwnerEntityId = 0;
	if (UFlecsArtillerySubsystem* Subsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>())
	{
		flecs::entity OwnerEntity = Subsystem->GetEntityForBarrageKey(GetEntityKey());
		if (OwnerEntity.is_valid())
		{
			OwnerEntityId = static_cast<int64>(OwnerEntity.id());
		}
	}

	FVector Direction = GetFiringDirection();
	Direction.Normalize();
	FRotator BaseRotation = Direction.Rotation();
	FVector SpawnLocation = GetMuzzleLocation();

	for (int32 i = 0; i < Count; ++i)
	{
		float AngleOffset = (i - (Count - 1) * 0.5f) * (SpreadAngle / FMath::Max(1, Count - 1));
		FRotator SpreadRotation = BaseRotation;
		SpreadRotation.Yaw += AngleOffset;
		SpreadRotation.Pitch += FMath::RandRange(-SpreadAngle * 0.25f, SpreadAngle * 0.25f);

		FSkeletonKey Key = UFlecsSpawnLibrary::SpawnProjectileFromEntityDef(
			GetWorld(),
			ProjectileDefinition,
			SpawnLocation,
			SpreadRotation.Vector(),
			ProjectileSpeedOverride,
			OwnerEntityId
		);

		if (Key.IsValid())
		{
			Keys.Add(Key);
		}
	}

	return Keys;
}

// ═══════════════════════════════════════════════════════════════════════════
// WEAPON VISUAL (game thread only — cosmetic representation)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::AttachWeaponVisual(USkeletalMesh* InMesh, const FTransform& AttachOffset)
{
	check(IsInGameThread());
	UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] AttachWeaponVisual: InMesh=%s AttachOffset loc=(%s) rot=(%s) scale=(%s)"),
		InMesh ? *InMesh->GetName() : TEXT("NULL"),
		*AttachOffset.GetLocation().ToString(),
		*AttachOffset.GetRotation().Rotator().ToString(),
		*AttachOffset.GetScale3D().ToString());

	if (!InMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] AttachWeaponVisual: null mesh → EARLY RETURN (visual NOT attached)"));
		return;
	}
	if (!WeaponMeshComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] AttachWeaponVisual: WeaponMeshComponent is NULL → WILL CHECK() CRASH"));
	}
	check(WeaponMeshComponent);

	// Dump pre-attach state
	const FString PreAttachParent = WeaponMeshComponent->GetAttachParent() ?
		WeaponMeshComponent->GetAttachParent()->GetName() : TEXT("NONE");
	const FName PreAttachSocket = WeaponMeshComponent->GetAttachSocketName();
	UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] AttachWeaponVisual: PRE-attach parent='%s' socket='%s' visible=%d worldLoc=(%s)"),
		*PreAttachParent, *PreAttachSocket.ToString(),
		WeaponMeshComponent->IsVisible() ? 1 : 0,
		*WeaponMeshComponent->GetComponentLocation().ToString());

	WeaponMeshComponent->SetSkeletalMesh(InMesh);
	WeaponMeshComponent->SetRelativeTransform(AttachOffset);
	BaseWeaponTransform = AttachOffset;  // cache for inertia reset each frame
	ComputeADSTransform();
	WeaponMeshComponent->SetVisibility(true);

	// TEMP (Phase 9): invalidate the procedural-anim cached rest transform so the
	// next Idle observation re-captures from the fresh attach offset. Cheap, no
	// allocation. TODO(MIGRATE): delete with UpdateMeleeProceduralAnim.
	bMeleeRestTransformCached = false;
	MeleePhaseElapsed         = 0.f;
	MeleePrevPhase            = static_cast<EMeleeAttackPhase>(0); // Idle (avoids include)

	// Dump post-attach state
	const FBoxSphereBounds Bounds = WeaponMeshComponent->Bounds;
	UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] AttachWeaponVisual: POST-attach visible=%d worldLoc=(%s) boundsOrigin=(%s) boundsExtent=(%s) mesh='%s'"),
		WeaponMeshComponent->IsVisible() ? 1 : 0,
		*WeaponMeshComponent->GetComponentLocation().ToString(),
		*Bounds.Origin.ToString(),
		*Bounds.BoxExtent.ToString(),
		*InMesh->GetName());

	// Check sockets on the new mesh (critical for melee sweep)
	const bool bHasHiltSocket  = WeaponMeshComponent->DoesSocketExist(TEXT("hilt_base"));
	const bool bHasBladeSocket = WeaponMeshComponent->DoesSocketExist(TEXT("blade_tip"));
	UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] AttachWeaponVisual: sockets hilt_base=%d blade_tip=%d (required for melee sweep)"),
		bHasHiltSocket ? 1 : 0, bHasBladeSocket ? 1 : 0);
}

void AFlecsCharacter::DetachWeaponVisual()
{
	check(IsInGameThread());
	if (!WeaponMeshComponent) return;
	WeaponMeshComponent->SetSkeletalMesh(nullptr);
	WeaponMeshComponent->SetVisibility(false);

	// TEMP (Phase 9): drop cached rest pose — different weapon next equip will
	// have a different rest transform. TODO(MIGRATE): delete with procedural anim.
	bMeleeRestTransformCached = false;
	MeleePhaseElapsed         = 0.f;
	MeleePrevPhase            = static_cast<EMeleeAttackPhase>(0); // Idle (avoids include)
}

// ═══════════════════════════════════════════════════════════════════════════
// WEAPON SLOTS (ECS weapon system)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::SpawnWeaponIntoSlot(int32 SlotIndex, UFlecsEntityDefinition* WeaponDef,
	UFlecsEntityDefinition* MagDef, int32 MagCount)
{
	check(WeaponDef && WeaponDef->WeaponProfile);
	// Upper bound is enforced by the weapon-slot container's grid/slot capacity (data-driven
	// via WeaponInventoryDefinition's ContainerProfile); negative indices remain invalid.
	checkf(SlotIndex >= 0, TEXT("SpawnWeaponIntoSlot: SlotIndex %d must be non-negative"), SlotIndex);

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	FSkeletonKey CharKey = GetEntityKey();
	if (!CharKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: Character has no valid entity key!"));
		return;
	}

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, CharKey, SlotIndex, WeaponDef, MagDef, MagCount]()
	{
		flecs::world* World = FlecsSubsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity CharEntity = FlecsSubsystem->GetEntityForBarrageKey(CharKey);
		if (!CharEntity.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnWeaponIntoSlot: No Flecs entity for character key %llu!"),
				static_cast<uint64>(CharKey));
			return;
		}

		// Resolve weapon slot container from FWeaponSlotState
		const FWeaponSlotState* SlotState = CharEntity.try_get<FWeaponSlotState>();
		if (!SlotState || SlotState->WeaponSlotContainerId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnWeaponIntoSlot: No weapon slot container on character!"));
			return;
		}
		flecs::entity ContainerEntity = World->entity(
			static_cast<flecs::entity_t>(SlotState->WeaponSlotContainerId));
		if (!ContainerEntity.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnWeaponIntoSlot: Weapon slot container entity invalid!"));
			return;
		}

		FContainerSlotsInstance* Slots = ContainerEntity.try_get_mut<FContainerSlotsInstance>();
		check(Slots);

		// Check if target slot is already occupied
		if (!Slots->IsSlotEmpty(SlotIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnWeaponIntoSlot: Slot %d already occupied!"), SlotIndex);
			return;
		}

		// Create weapon entity from prefab
		flecs::entity Prefab = FlecsSubsystem->GetOrCreateEntityPrefab(WeaponDef);
		if (!Prefab.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnWeaponIntoSlot: Failed to create weapon prefab!"));
			return;
		}

		flecs::entity WeaponEntity = World->entity()
			.is_a(Prefab)
			.add<FTagWeapon>();

		const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>();
		FWeaponInstance Instance;
		Instance.CurrentBloom = 0.f;

		// Spawn magazines if available
		if (Static && !Static->bUnlimitedAmmo && MagDef && MagDef->MagazineProfile)
		{
			const UFlecsMagazineProfile* MagProfile = MagDef->MagazineProfile;

			flecs::entity MagPrefab = FlecsSubsystem->GetOrCreateEntityPrefab(MagDef);
			if (MagPrefab.is_valid())
			{
				for (int32 i = 0; i < MagCount; ++i)
				{
					flecs::entity MagEntity = World->entity()
						.is_a(MagPrefab)
						.add<FTagMagazine>()
						.add<FTagItem>();

					// Initialize ammo stack (fill with default ammo type)
					FMagazineInstance MagInst;
					if (MagProfile->DefaultAmmoType)
					{
						const FMagazineStatic* MagStatic = MagEntity.try_get<FMagazineStatic>();
						if (MagStatic)
						{
							int32 AmmoIdx = MagStatic->FindAmmoTypeIndex(MagProfile->DefaultAmmoType);
							if (AmmoIdx >= 0)
							{
								for (int32 r = 0; r < MagStatic->Capacity; ++r)
								{
									MagInst.Push(static_cast<uint8>(AmmoIdx));
								}
							}
						}
					}
					MagEntity.set<FMagazineInstance>(MagInst);

					if (i == 0)
					{
						// First magazine goes into the weapon
						Instance.InsertedMagazineId = static_cast<int64>(MagEntity.id());

						// Chamber first round from magazine (if weapon has chamber)
						if (Static->bHasChamber && MagInst.AmmoCount > 0)
						{
							int32 Idx = MagInst.Pop();
							Instance.bChambered = true;
							Instance.ChamberedAmmoTypeIdx = static_cast<uint8>(Idx);
							MagEntity.set<FMagazineInstance>(MagInst);
						}

						UE_LOG(LogTemp, Log, TEXT("WEAPON SLOT %d: First magazine %lld inserted (%d rounds, chambered=%d)"),
							SlotIndex, Instance.InsertedMagazineId, MagInst.AmmoCount, Instance.bChambered);
					}
					else
					{
						// Remaining magazines go to general inventory
						int64 InvId = 0;
						const FCharacterInventoryRef* InvRefPtr = CharEntity.try_get<FCharacterInventoryRef>();
						if (InvRefPtr) InvId = InvRefPtr->InventoryEntityId;

						FContainedIn Contained;
						Contained.ContainerEntityId = InvId;
						Contained.SlotIndex = -1;
						MagEntity.set<FContainedIn>(Contained);

						FItemInstance ItemInst;
						ItemInst.Count = 1;
						MagEntity.set<FItemInstance>(ItemInst);

						UE_LOG(LogTemp, Log, TEXT("WEAPON SLOT %d: Magazine %lld added to inventory (%d rounds)"),
							SlotIndex, static_cast<int64>(MagEntity.id()), MagInst.AmmoCount);
					}
				}
			}
		}

		WeaponEntity.set<FWeaponInstance>(Instance);

		// Place weapon into slot container (NOT equipped — just slotted)
		int64 WeaponId = static_cast<int64>(WeaponEntity.id());
		Slots->SetSlot(SlotIndex, WeaponId);

		FContainedIn Contained;
		Contained.ContainerEntityId = SlotState->WeaponSlotContainerId;
		Contained.SlotIndex = SlotIndex;
		WeaponEntity.set<FContainedIn>(Contained);

		// Update container counters
		FContainerInstance* ContInst = ContainerEntity.try_get_mut<FContainerInstance>();
		if (ContInst)
		{
			ContInst->CurrentCount += 1;
		}

		UE_LOG(LogTemp, Log, TEXT("WEAPON SLOT %d: Weapon %lld placed (not equipped)"), SlotIndex, WeaponId);
	});
}

void AFlecsCharacter::StartFiringWeapon()
{
	if (ActiveWeaponEntityId == 0) return;

	// ─── MELEE BRANCH ────────────────────────────────────────────────
	// Melee weapons reuse the Fire (LMB) binding but drive FMeleeWeaponInstance.
	// bAttackRequested instead of FWeaponInstance.bFireRequested. Skip the ranged-
	// only aim-update / recoil-retraction gates — they don't apply to swings.
	if (IsActiveWeaponMelee())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] StartFiringWeapon: melee path → SetMeleeAttackRequested(true) on %lld"),
			ActiveWeaponEntityId);
		UFlecsMeleeLibrary::SetMeleeAttackRequested(this, ActiveWeaponEntityId, true);
		return;
	}

	// Firing blocks and sprint cancel are handled by SetGameBit(Firing) in StartFire.
	// IsFireBlocked() is checked there via rule table. Sprint cancel via CanceledOnEntry.

	// Block firing when weapon is retracted into wall-ready pose
	if (RecoilState.CachedProfile && RecoilState.CachedProfile->CollisionFireBlockThreshold > 0.f)
	{
		if (RecoilState.CollisionCurrentAlpha >= RecoilState.CachedProfile->CollisionFireBlockThreshold)
			return;
	}

	// Update aim direction via lock-free bridge (ensures fresh data for first shot)
	if (auto* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>())
	{
		if (auto* Bridge = FlecsSubsystem->GetLateSyncBridge())
		{
			FAimDirection Aim;
			Aim.Direction = GetFiringDirection();
			Aim.CharacterPosition = FollowCamera ? FollowCamera->GetComponentLocation() : GetActorLocation();
			Aim.MuzzleWorldPosition = GetMuzzleLocation();
			Bridge->WriteAimDirection(GetCharacterEntityId(), Aim);
		}
	}

	// Start firing
	UFlecsWeaponLibrary::StartFiring(this, ActiveWeaponEntityId);
}

void AFlecsCharacter::StopFiringWeapon()
{
	if (ActiveWeaponEntityId == 0) return;
	if (IsActiveWeaponMelee())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] StopFiringWeapon: melee path → SetMeleeAttackRequested(false) on %lld"),
			ActiveWeaponEntityId);
		UFlecsMeleeLibrary::SetMeleeAttackRequested(this, ActiveWeaponEntityId, false);
		return;
	}
	UFlecsWeaponLibrary::StopFiring(this, ActiveWeaponEntityId);
}

void AFlecsCharacter::RequestReload()
{
	if (ActiveWeaponEntityId == 0) return;
	UFlecsWeaponLibrary::ToggleReload(this, ActiveWeaponEntityId);
}

// ═══════════════════════════════════════════════════════════════════════════
// WEAPON SLOT SWITCHING
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::RequestWeaponSwitch(int32 SlotIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] RequestWeaponSwitch(slot=%d) ActiveSlot=%d"),
		SlotIndex, ActiveWeaponSlotIndex);

	// -1 = holster; positive index is bound by the weapon-slot container's capacity
	// (data-driven), validated when reading from FContainerSlotsInstance below.
	checkf(SlotIndex >= -1, TEXT("RequestWeaponSwitch: SlotIndex %d must be >= -1"), SlotIndex);

	// Same slot as active — ignore
	if (SlotIndex == ActiveWeaponSlotIndex && SlotIndex >= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] RequestWeaponSwitch: same as active slot → IGNORED"));
		return;
	}

	// Try to enter WeaponSwitching state
	if (!SetGameBit(ActionBit::WeaponSwitching))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] RequestWeaponSwitch: SetGameBit(WeaponSwitching) FAILED (already switching or blocked) → ABORT"));
		return;
	}

	// Sync sprint cancel to sim thread
	if (!HasBit(GameActionState.load(std::memory_order_relaxed), ActionBit::Sprinting))
	{
		if (InputAtomics) InputAtomics->Sprinting.Write(false);
		if (FatumMovement) FatumMovement->RequestSprint(false);
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] RequestWeaponSwitch: FlecsSubsystem NULL → ABORT"));
		ClearGameBit(ActionBit::WeaponSwitching);
		return;
	}

	// Helper: signal game thread to clear WeaponSwitching (unequip/no-op)
	auto SignalAbort = [FlecsSubsystem](flecs::entity CharEntity)
	{
		FlecsSubsystem->EnqueueWeaponEquipSignal(CharEntity, 0, -1, nullptr, nullptr, FTransform::Identity);
	};

	FSkeletonKey Key = CharacterKey;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key, SlotIndex, SignalAbort]()
	{
		flecs::world* World = FlecsSubsystem->GetFlecsWorld();
		if (!World) { UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: FlecsWorld NULL")); return; }

		flecs::entity CharEntity = FlecsSubsystem->GetEntityForBarrageKey(Key);
		if (!CharEntity.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: CharEntity INVALID (key→entity failed)"));
			return;
		}

		FWeaponSlotState* SlotState = CharEntity.try_get_mut<FWeaponSlotState>();
		if (!SlotState)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: FWeaponSlotState MISSING on character → ABORT"));
			SignalAbort(CharEntity);
			return;
		}
		if (SlotState->WeaponSlotContainerId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: WeaponSlotContainerId == 0 (WeaponInventory NOT CREATED) → ABORT"));
			SignalAbort(CharEntity);
			return;
		}

		flecs::entity Container = World->entity(
			static_cast<flecs::entity_t>(SlotState->WeaponSlotContainerId));
		if (!Container.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: Container entity %lld INVALID → ABORT"),
				SlotState->WeaponSlotContainerId);
			SignalAbort(CharEntity);
			return;
		}

		const FContainerSlotsInstance* Slots = Container.try_get<FContainerSlotsInstance>();
		if (!Slots)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: Container has NO FContainerSlotsInstance (ContainerType != Slot?) → ABORT"));
			SignalAbort(CharEntity);
			return;
		}

		int64 TargetWeaponId = (SlotIndex >= 0) ? Slots->GetItemInSlot(SlotIndex) : 0;
		UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: slot=%d TargetWeaponId=%lld ActiveSlotIndex=%d Phase=%d"),
			SlotIndex, TargetWeaponId, SlotState->ActiveSlotIndex, (int32)SlotState->EquipPhase);

		// Empty slot pressed + no active weapon — nothing to do
		if (TargetWeaponId == 0 && SlotState->ActiveSlotIndex < 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: target slot EMPTY + no active weapon → ABORT"));
			SignalAbort(CharEntity);
			return;
		}

		// If already switching, interrupt with new target
		if (SlotState->IsSwitching())
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: already switching, retarget Pending=%d"),
				(TargetWeaponId != 0) ? SlotIndex : -1);
			SlotState->PendingSlotIndex = (TargetWeaponId != 0) ? SlotIndex : -1;
			return;
		}

		// Currently holding a weapon — start holstering
		if (SlotState->ActiveSlotIndex >= 0)
		{
			int64 CurrentWeaponId = Slots->GetItemInSlot(SlotState->ActiveSlotIndex);
			float HolsterTime = 0.2f;
			if (CurrentWeaponId != 0)
			{
				flecs::entity CurWeapon = World->entity(static_cast<flecs::entity_t>(CurrentWeaponId));
				if (CurWeapon.is_valid())
				{
					const FWeaponStatic* WS = CurWeapon.try_get<FWeaponStatic>();
					if (WS) HolsterTime = WS->EquipTime * 0.5f;
				}
			}

			SlotState->PendingSlotIndex = (TargetWeaponId != 0) ? SlotIndex : -1;
			SlotState->EquipPhase = EWeaponEquipPhase::Holstering;
			SlotState->EquipTimer = HolsterTime;
			UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: START HOLSTER (pending=%d, timer=%.3f)"),
				SlotState->PendingSlotIndex, HolsterTime);
		}
		else
		{
			// Unarmed — go directly to Drawing
			if (TargetWeaponId != 0)
			{
				flecs::entity NewWeapon = World->entity(static_cast<flecs::entity_t>(TargetWeaponId));
				const FWeaponStatic* WS = NewWeapon.is_valid() ? NewWeapon.try_get<FWeaponStatic>() : nullptr;
				float DrawTime = WS ? WS->EquipTime * 0.5f : 0.25f;

				SlotState->PendingSlotIndex = SlotIndex;
				SlotState->EquipPhase = EWeaponEquipPhase::Drawing;
				SlotState->EquipTimer = DrawTime;
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] sim-lambda: UNARMED → DRAW directly (slot=%d, timer=%.3f, weaponId=%lld)"),
					SlotIndex, DrawTime, TargetWeaponId);
			}
		}
	});
}

void AFlecsCharacter::OnWeaponSlot1(const FInputActionValue& Value)
{
	RequestWeaponSwitch(0);
}

void AFlecsCharacter::OnWeaponSlot2(const FInputActionValue& Value)
{
	RequestWeaponSwitch(1);
}

void AFlecsCharacter::OnWeaponSlot3(const FInputActionValue& Value)
{
	UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] OnWeaponSlot3 pressed → RequestWeaponSwitch(2)"));
	RequestWeaponSwitch(2);
}
