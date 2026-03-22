// Combat implementation for AFlecsCharacter.
// Health queries/mutations, projectile firing (legacy direct spawn), weapon visual + ECS weapon testing.

#include "FlecsCharacter.h"
#include "FatumMovementComponent.h"
#include "FlecsDamageLibrary.h"
#include "FlecsWeaponLibrary.h"
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
#include "FSimStateCache.h"
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
	if (!InMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("AttachWeaponVisual: null mesh"));
		return;
	}
	check(WeaponMeshComponent);
	WeaponMeshComponent->SetSkeletalMesh(InMesh);
	WeaponMeshComponent->SetRelativeTransform(AttachOffset);
	BaseWeaponTransform = AttachOffset;  // cache for inertia reset each frame
	ComputeADSTransform();
	WeaponMeshComponent->SetVisibility(true);
	UE_LOG(LogTemp, Log, TEXT("WEAPON VISUAL: Attached '%s'"), *InMesh->GetName());
}

void AFlecsCharacter::DetachWeaponVisual()
{
	check(IsInGameThread());
	if (!WeaponMeshComponent) return;
	WeaponMeshComponent->SetSkeletalMesh(nullptr);
	WeaponMeshComponent->SetVisibility(false);
}

// ═══════════════════════════════════════════════════════════════════════════
// WEAPON TESTING (ECS weapon system)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::SpawnAndEquipTestWeapon()
{
	if (!TestWeaponDefinition || !TestWeaponDefinition->WeaponProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: TestWeaponDefinition is null or has no WeaponProfile!"));
		return;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	// Get character's BarrageKey (from KeyCarry component)
	FSkeletonKey CharKey = GetEntityKey();
	if (!CharKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: Character has no valid entity key!"));
		return;
	}

	// Capture visual data on game thread (UObject pointers not safe across threads).
	// Store in PendingWeaponEquip so Tick() can safely attach the visual.
	PendingWeaponEquip.Mesh = TestWeaponDefinition->WeaponProfile->EquippedMesh;
	PendingWeaponEquip.AttachOffset = TestWeaponDefinition->WeaponProfile->AttachOffset;

	// Create weapon entity via EnqueueCommand
	// IMPORTANT: Look up CharEntityId INSIDE the command, on simulation thread,
	// to ensure BeginPlay's entity binding has already been processed.
	FlecsSubsystem->EnqueueCommand([this, FlecsSubsystem, CharKey]()
	{
		flecs::world* World = FlecsSubsystem->GetFlecsWorld();
		if (!World) return;

		// Look up character entity on simulation thread (after BeginPlay binding)
		flecs::entity CharEntity = FlecsSubsystem->GetEntityForBarrageKey(CharKey);
		if (!CharEntity.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: No Flecs entity for character key %llu!"),
				static_cast<uint64>(CharKey));
			return;
		}
		int64 CharEntityId = static_cast<int64>(CharEntity.id());
		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Found character entity %lld for key %llu"),
			CharEntityId, static_cast<uint64>(CharKey));

		// Get or create prefab for weapon
		flecs::entity Prefab = FlecsSubsystem->GetOrCreateEntityPrefab(TestWeaponDefinition);
		if (!Prefab.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: Failed to create weapon prefab!"));
			return;
		}

		// Create weapon entity from prefab
		flecs::entity WeaponEntity = World->entity()
			.is_a(Prefab)
			.add<FTagWeapon>();

		// Initialize FWeaponInstance
		const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>();
		FWeaponInstance Instance;
		Instance.CurrentBloom = 0.f;  // bloom starts at 0; BaseSpread is added separately

		// Spawn test magazines if available
		if (Static && !Static->bUnlimitedAmmo && TestMagazineDefinition && TestMagazineDefinition->MagazineProfile)
		{
			const UFlecsMagazineProfile* MagProfile = TestMagazineDefinition->MagazineProfile;

			flecs::entity MagPrefab = FlecsSubsystem->GetOrCreateEntityPrefab(TestMagazineDefinition);
			if (MagPrefab.is_valid())
			{
				for (int32 i = 0; i < TestMagazineCount; ++i)
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
						UE_LOG(LogTemp, Log, TEXT("WEAPON: First magazine %lld inserted into weapon (%d rounds)"),
							Instance.InsertedMagazineId, MagInst.AmmoCount);
					}
					else
					{
						// Remaining magazines go to inventory
						// Read InventoryEntityId from Flecs entity (sim-thread safe), not from game-thread member
						int64 InvId = 0;
						const FCharacterInventoryRef* InvRefPtr = CharEntity.try_get<FCharacterInventoryRef>();
						if (InvRefPtr) InvId = InvRefPtr->InventoryEntityId;
						UE_LOG(LogTemp, Log, TEXT("WEAPON: Placing mag in inventory, InvId=%lld (game-thread=%lld)"), InvId, InventoryEntityId);
						FContainedIn Contained;
						Contained.ContainerEntityId = InvId;
						Contained.SlotIndex = -1;
						MagEntity.set<FContainedIn>(Contained);

						FItemInstance ItemInst;
						ItemInst.Count = 1;
						MagEntity.set<FItemInstance>(ItemInst);

						UE_LOG(LogTemp, Log, TEXT("WEAPON: Magazine %lld added to inventory (%d rounds)"),
							static_cast<int64>(MagEntity.id()), MagInst.AmmoCount);
					}
				}
			}
		}

		WeaponEntity.set<FWeaponInstance>(Instance);

		// Equip to character
		FEquippedBy Equipped;
		Equipped.CharacterEntityId = CharEntityId;
		Equipped.SlotId = 0;
		WeaponEntity.set<FEquippedBy>(Equipped);

		// Store weapon entity ID (thread-safe assignment via main thread later)
		int64 WeaponId = static_cast<int64>(WeaponEntity.id());

		// Get ammo info from inserted magazine for UI
		int32 InitAmmo = 0;
		int32 InitMagSize = 0;
		if (Instance.InsertedMagazineId != 0)
		{
			flecs::entity MagE = World->entity(static_cast<flecs::entity_t>(Instance.InsertedMagazineId));
			if (MagE.is_valid())
			{
				const FMagazineInstance* MI = MagE.try_get<FMagazineInstance>();
				const FMagazineStatic* MS = MagE.try_get<FMagazineStatic>();
				if (MI) InitAmmo = MI->AmmoCount;
				if (MS) InitMagSize = MS->Capacity;
			}
		}

		// Register weapon in sim→game state cache with initial values
		FlecsSubsystem->GetSimStateCache().Register(WeaponId);
		FlecsSubsystem->GetSimStateCache().WriteWeapon(WeaponId, InitAmmo, InitMagSize, 0, false);

		// Send initial ammo state to HUD (we're on sim thread, use EnqueueMessage)
		if (UFlecsMessageSubsystem::SelfPtr)
		{
			FUIAmmoMessage AmmoMsg;
			AmmoMsg.WeaponEntityId = WeaponId;
			AmmoMsg.CurrentAmmo = InitAmmo;
			AmmoMsg.MagazineSize = InitMagSize;
			AmmoMsg.ReserveAmmo = 0;
			UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
		}

		// Signal game thread via atomics (processed in Tick).
		// AsyncTask(GameThread) can execute during post-tick component update → crash.
		PendingWeaponEquip.WeaponId.store(WeaponId, std::memory_order_release);
		PendingWeaponEquip.bPending.store(true, std::memory_order_release);
	});
}

bool AFlecsCharacter::IsFireBlocked() const
{
	if (!StateAtomics) return false;
	return StateAtomics->MantleActive.Read() || StateAtomics->ClimbActive.Read();
}

void AFlecsCharacter::StartFiringWeapon()
{
	if (TestWeaponEntityId == 0) return;

	// Block firing during mantle/climb/ledge hang
	if (IsFireBlocked()) return;

	// Cancel sprint when firing — sprint resumes on fire release
	if (FatumMovement && FatumMovement->IsSprinting())
	{
		if (InputAtomics) InputAtomics->Sprinting.Write(false);
		FatumMovement->RequestSprint(false);
		bSprintSuppressedByFire = true;
	}

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
	UFlecsWeaponLibrary::StartFiring(this, TestWeaponEntityId);
}

void AFlecsCharacter::StopFiringWeapon()
{
	if (TestWeaponEntityId == 0) return;
	UFlecsWeaponLibrary::StopFiring(this, TestWeaponEntityId);
}

void AFlecsCharacter::ReloadTestWeapon()
{
	if (TestWeaponEntityId == 0) return;
	UFlecsWeaponLibrary::ToggleReload(this, TestWeaponEntityId);
}
