
#include "FlecsDamageLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsHealthComponents.h"
#include "FlecsGameTags.h"
#include "FSimStateCache.h"

// ═══════════════════════════════════════════════════════════════
// ENTITY LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsDamageLibrary::KillEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey]()
	{
		flecs::entity Entity = FlecsLibrary::GetEntityForKey(Subsystem, CapturedKey);
		if (Entity.is_valid() && Entity.is_alive())
		{
			Entity.add<FTagDead>();
		}
	});
}

// ═══════════════════════════════════════════════════════════════
// DAMAGE & HEALING (game-thread safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsDamageLibrary::ApplyDamageByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid() || Damage <= 0.f) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Damage]()
	{
		Subsystem->QueueDamageByKey(CapturedKey, Damage);
	});
}

void UFlecsDamageLibrary::ApplyDamageWithType(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage,
                                                 FGameplayTag DamageType, bool bIgnoreArmor)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid() || Damage <= 0.f) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Damage, DamageType, bIgnoreArmor]()
	{
		flecs::entity Target = Subsystem->GetEntityForBarrageKey(CapturedKey);
		if (Target.is_valid())
		{
			Subsystem->QueueDamage(Target, Damage, 0, DamageType, FVector::ZeroVector, bIgnoreArmor);
		}
	});
}

void UFlecsDamageLibrary::HealEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Amount)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid() || Amount <= 0.f) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Amount]()
	{
		Heal_ArtilleryThread(Subsystem, CapturedKey, Amount);
	});
}

// ═══════════════════════════════════════════════════════════════
// SIMULATION THREAD API
// ═══════════════════════════════════════════════════════════════

bool UFlecsDamageLibrary::ApplyDamage_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Damage)
{
	return Subsystem->QueueDamageByKey(BarrageKey, Damage);
}

void UFlecsDamageLibrary::Heal_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Amount)
{
	flecs::entity Entity = FlecsLibrary::GetEntityForKey(Subsystem, BarrageKey);
	if (!Entity.is_valid() || !Entity.is_alive()) return;

	FHealthInstance* HealthInst = Entity.try_get_mut<FHealthInstance>();
	if (!HealthInst) return;

	const FHealthStatic* HealthStatic = Entity.try_get<FHealthStatic>();
	float MaxHP = HealthStatic ? HealthStatic->MaxHP : 100.f;

	HealthInst->CurrentHP = FMath::Min(HealthInst->CurrentHP + Amount, MaxHP);

	// Update sim→game state cache
	Subsystem->GetSimStateCache().WriteHealth(static_cast<int64>(Entity.id()), HealthInst->CurrentHP, MaxHP);
}

bool UFlecsDamageLibrary::IsAlive_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey)
{
	flecs::entity Entity = FlecsLibrary::GetEntityForKey(Subsystem, BarrageKey);
	if (!Entity.is_valid() || !Entity.is_alive()) return false;

	const FHealthInstance* HealthInst = Entity.try_get<FHealthInstance>();
	return HealthInst ? HealthInst->IsAlive() : Entity.is_alive();
}

bool UFlecsDamageLibrary::GetHealth_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey,
	float& OutCurrentHP, float& OutMaxHP)
{
	flecs::entity Entity = FlecsLibrary::GetEntityForKey(Subsystem, BarrageKey);
	if (!Entity.is_valid() || !Entity.is_alive()) return false;

	const FHealthInstance* HealthInst = Entity.try_get<FHealthInstance>();
	if (!HealthInst) return false;

	const FHealthStatic* HealthStatic = Entity.try_get<FHealthStatic>();

	OutCurrentHP = HealthInst->CurrentHP;
	OutMaxHP = HealthStatic ? HealthStatic->MaxHP : 100.f;
	return true;
}

// ═══════════════════════════════════════════════════════════════
// QUERY (game-thread) — reads from lock-free FSimStateCache
// ═══════════════════════════════════════════════════════════════

float UFlecsDamageLibrary::GetEntityHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Sub || !BarrageKey.IsValid()) return -1.f;

	flecs::entity Entity = Sub->GetEntityForBarrageKey(BarrageKey);
	if (Entity.id() == 0) return -1.f;

	FHealthSnapshot Snap;
	if (Sub->GetSimStateCache().ReadHealth(static_cast<int64>(Entity.id()), Snap))
		return Snap.CurrentHP;
	return -1.f;
}

float UFlecsDamageLibrary::GetEntityMaxHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Sub || !BarrageKey.IsValid()) return -1.f;

	flecs::entity Entity = Sub->GetEntityForBarrageKey(BarrageKey);
	if (Entity.id() == 0) return -1.f;

	FHealthSnapshot Snap;
	if (Sub->GetSimStateCache().ReadHealth(static_cast<int64>(Entity.id()), Snap))
		return Snap.MaxHP;
	return -1.f;
}

bool UFlecsDamageLibrary::IsEntityAlive(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Sub || !BarrageKey.IsValid()) return false;

	flecs::entity Entity = Sub->GetEntityForBarrageKey(BarrageKey);
	if (Entity.id() == 0) return false;

	FHealthSnapshot Snap;
	if (Sub->GetSimStateCache().ReadHealth(static_cast<int64>(Entity.id()), Snap))
		return Snap.CurrentHP > 0.f;
	return false;
}
