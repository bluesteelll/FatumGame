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
#include "FlecsWeaponProfile.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsWeaponComponents.h"
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
	return MaxHealth > 0.f ? GetCurrentHealth() / MaxHealth : 0.f;
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
		if (Static)
		{
			FWeaponInstance Instance;
			Instance.CurrentAmmo = Static->MagazineSize;
			Instance.ReserveAmmo = Static->MaxReserveAmmo;
			WeaponEntity.set<FWeaponInstance>(Instance);
		}

		// Equip to character
		FEquippedBy Equipped;
		Equipped.CharacterEntityId = CharEntityId;
		Equipped.SlotId = 0;
		WeaponEntity.set<FEquippedBy>(Equipped);

		// Store weapon entity ID (thread-safe assignment via main thread later)
		int64 WeaponId = static_cast<int64>(WeaponEntity.id());

		// Register weapon in sim→game state cache with initial values
		if (Static)
		{
			FlecsSubsystem->GetSimStateCache().Register(WeaponId);
			FlecsSubsystem->GetSimStateCache().WriteWeapon(
				WeaponId, Static->MagazineSize, Static->MagazineSize, Static->MaxReserveAmmo, false);
		}

		// Send initial ammo state to HUD (we're on sim thread, use EnqueueMessage)
		if (Static && UFlecsMessageSubsystem::SelfPtr)
		{
			FUIAmmoMessage AmmoMsg;
			AmmoMsg.WeaponEntityId = WeaponId;
			AmmoMsg.CurrentAmmo = Static->MagazineSize;
			AmmoMsg.MagazineSize = Static->MagazineSize;
			AmmoMsg.ReserveAmmo = Static->MaxReserveAmmo;
			UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
		}

		// Signal game thread via atomics (processed in Tick).
		// AsyncTask(GameThread) can execute during post-tick component update → crash.
		PendingWeaponEquip.WeaponId.store(WeaponId, std::memory_order_release);
		PendingWeaponEquip.bPending.store(true, std::memory_order_release);
	});
}

void AFlecsCharacter::StartFiringWeapon()
{
	if (TestWeaponEntityId == 0) return;

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
	UFlecsWeaponLibrary::ReloadWeapon(this, TestWeaponEntityId);
}
