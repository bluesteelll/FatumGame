# FatumGame - Project Documentation

## KEY DEVELOPMENT PRINCIPLES

### 1. NO WORKAROUNDS
**NEVER use workarounds!** Find and fix the **ROOT CAUSE**.

### 2. FAIL-FAST
Use `check()`, `ensure()`, `checkf()`. Don't silently swallow errors.

### 3. AVOID BOILERPLATE
Extract to functions/templates. Use auto, range-based for, structured bindings.

---

## Quick Reference

| Property | Value |
|----------|-------|
| **Engine** | UE 5.7 |
| **Physics** | Jolt (Barrage) |
| **ECS** | Flecs (FlecsIntegration) |
| **Networking** | Bristlecone (deterministic rollback) |
| **Simulation** | 120Hz Artillery thread |

```
Bristlecone -> Network inputs (UDP)
Cabling     -> Local inputs
Artillery   -> Core simulation (120Hz, separate thread)
Barrage     -> Jolt Physics
Flecs       -> Gameplay data (health, damage, items)
Game Thread -> Cosmetics, rendering
```

---

## Key Files

| File | Purpose |
|------|---------|
| `FlecsCharacter.h/cpp` | Character with Flecs (health, shooting, E/F test) |
| `FlecsEntitySpawner.h/cpp` | **Unified Entity API**: FEntitySpawnRequest, UFlecsEntityLibrary |
| `FlecsEntitySpawnerActor.h/cpp` | **Editor spawner**: drag to level, set EntityDefinition |
| `FlecsEntityDefinition.h` | Unified preset combining all profiles |
| `FlecsStaticComponents.h` | PREFAB components (FHealthStatic, FDamageStatic, etc.) |
| `FlecsInstanceComponents.h/cpp` | Instance components (FHealthInstance, etc.) |
| `FlecsGameTags.h/cpp` | Tags + ENUMs (FTagItem, FTagCharacter) |
| `FlecsGameplayLibrary.h/cpp` | Blueprint API (wrappers over UFlecsEntityLibrary) |
| `FlecsArtillerySubsystem.h/cpp` | Artillery-Flecs bridge, collisions |
| **Plugin:** `FlecsBarrageComponents.h/cpp` | Bridge: FBarrageBody, FISMRender, FCollisionPair |
| **Plugin:** `FBarragePrimitive.h` | Atomic FlecsEntityId for reverse binding |

**Profiles:** PhysicsProfile, RenderProfile, HealthProfile, DamageProfile, ProjectileProfile, ContainerProfile, ItemDefinition

---

## ECS Architecture

### Static/Instance Pattern (Prefab Inheritance)

```
PREFAB (one per type) ──────────────────────────────────────
  FHealthStatic     { MaxHP, Armor, RegenPerSecond, bDestroyOnDeath }
  FDamageStatic     { Damage, DamageType, bAreaDamage, AreaRadius, bDestroyOnHit }
  FProjectileStatic { MaxLifetime, MaxBounces, GracePeriodFrames, MinVelocity }
  FItemStaticData   { TypeId, MaxStack, Weight, GridSize, EntityDefinition* }
  FContainerStatic  { Type, GridWidth, GridHeight, MaxItems, MaxWeight }
  FEntityDefinitionRef { EntityDefinition* }
         ↑ IsA (inheritance)
ENTITY (each instance) ─────────────────────────────────────
  FHealthInstance      { CurrentHP, RegenAccumulator }
  FProjectileInstance  { LifetimeRemaining, BounceCount, GraceFramesRemaining }
  FItemInstance        { Count }
  FContainerInstance   { CurrentWeight, CurrentCount, OwnerEntityId }
  FContainedIn         { ContainerEntityId, GridPosition, SlotIndex }
  FBarrageBody         { SkeletonKey }  // Forward binding
  FISMRender           { Mesh, Scale }
  + Tags: FTagProjectile, FTagCharacter, FTagItem, FTagContainer, FTagDead
```

### Damage System
```
Sources → QueueDamage() → FPendingDamage { TArray<FDamageHit> } → DamageObserver → FHealthInstance
```

### Collision Pair System
```
OnBarrageContact → FCollisionPair + Tags → Systems process by tag → CollisionPairCleanupSystem
```
| Tag | Processed By |
|-----|--------------|
| FTagCollisionDamage | DamageCollisionSystem |
| FTagCollisionBounce | BounceCollisionSystem |
| FTagCollisionPickup | PickupCollisionSystem |
| FTagCollisionDestructible | DestructibleCollisionSystem |

### System Execution Order
1. WorldItemDespawnSystem → 2. PickupGraceSystem → 3. ProjectileLifetimeSystem
4. DamageCollisionSystem → 5. BounceCollisionSystem → 6. PickupCollisionSystem → 7. DestructibleCollisionSystem
8. DeathCheckSystem → 9. DeadEntityCleanupSystem → 10. CollisionPairCleanupSystem (LAST)

---

## Lock-Free Bidirectional Binding

```cpp
// Forward: Entity → BarrageKey (O(1) via Flecs)
entity.get<FBarrageBody>()->BarrageKey

// Reverse: BarrageKey → Entity (O(1) via atomic in FBarragePrimitive)
FBLet->GetFlecsEntity()

// API (UFlecsArtillerySubsystem)
void BindEntityToBarrage(flecs::entity Entity, FSkeletonKey BarrageKey);
void UnbindEntityFromBarrage(flecs::entity Entity);
flecs::entity GetEntityForBarrageKey(FSkeletonKey BarrageKey) const;
```

---

## Unified Spawn API

**Single source of truth:** `UFlecsEntityLibrary::SpawnEntity()`

```cpp
// C++ Fluent API
FEntitySpawnRequest::FromDefinition(BulletDef, Location)
    .WithVelocity(Direction * Speed)
    .WithOwnerEntity(ShooterId)
    .Spawn(WorldContext);

// Or explicit
FEntitySpawnRequest Request;
Request.EntityDefinition = BulletDef;
Request.Location = SpawnLoc;
Request.InitialVelocity = Velocity;
UFlecsEntityLibrary::SpawnEntity(World, Request);

// Blueprint wrapper (uses unified API internally)
UFlecsGameplayLibrary::SpawnProjectileFromEntityDef(World, Definition, Location, Direction, Speed, OwnerId);
```

### Projectile Physics Types
| Bouncing | Gravity | Body Type | Use Case |
|----------|---------|-----------|----------|
| No | 0 | Sensor | Laser - flies straight |
| No | >0 | Dynamic | Grenade - falls, explodes on contact |
| Yes | any | Dynamic | Ricochet - bounces off walls |

---

## Blueprint API

```cpp
// Damage/Heal (thread-safe, enqueues to Artillery)
UFlecsGameplayLibrary::ApplyDamageByBarrageKey(World, TargetKey, 25.f);
UFlecsGameplayLibrary::HealEntityByBarrageKey(World, TargetKey, 50.f);
UFlecsGameplayLibrary::KillEntityByBarrageKey(World, EntityKey);

// Containers
AddItemToContainerFromDefinition(World, ContainerKey, EntityDef, Count, OutAdded);
RemoveAllItemsFromContainer(World, ContainerKey);
GetContainerItemCount(World, ContainerKey);
```

---

## Quick Start

### Create Projectile
Content Browser → Data Asset → **FlecsEntityDefinition** → `DA_Bullet`
- ProjectileProfile: Speed=5000, Lifetime=10, MaxBounces=0
- PhysicsProfile: GravityFactor=1 (for arc), CollisionRadius=5
- RenderProfile: Mesh, Scale=(0.3,0.3,0.3)
- DamageProfile: Damage=25, bDestroyOnHit=true

### Spawn in Editor (AFlecsEntitySpawner)
1. Place Actors → **FlecsEntitySpawner**
2. Set `EntityDefinition` (Data Asset)
3. Play!

| Property | Default | Description |
|----------|---------|-------------|
| `EntityDefinition` | — | Data Asset с профилями (обязательно) |
| `InitialVelocity` | (0,0,0) | Начальная скорость |
| `bOverrideScale` | false | Переопределить масштаб из RenderProfile |
| `ScaleOverride` | (1,1,1) | Масштаб если bOverrideScale=true |
| `bSpawnOnBeginPlay` | true | Спавнить при старте |
| `bDestroyAfterSpawn` | true | Удалить актор после спавна |
| `bShowPreview` | true | Preview меша в редакторе |

**Для ручного спавна:** `bSpawnOnBeginPlay=false`, затем вызвать `SpawnEntity()` из Blueprint/C++.

### Create Character
Blueprint Class → **FlecsCharacter** → `BP_Player`
- ProjectileDefinition = DA_Bullet
- Add: SpringArm + Camera

**Note:** Gravity is in PhysicsProfile.GravityFactor (0=laser, 1=grenade)

---

## Critical Patterns

### Entity Destruction (CORRECT)
```cpp
Prim->ClearFlecsEntity();
CachedBarrageDispatch->SetBodyObjectLayer(Prim->KeyIntoBarrage, Layers::DEBRIS);  // Instant collision disable
CachedBarrageDispatch->SuggestTombstone(Prim);  // Safe deferred destroy (~19 sec)
// NEVER use FinalizeReleasePrimitive() - causes crash on PIE exit!
```

### Subsystem Init
```cpp
void UMySubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Collection.InitializeDependency<UArtilleryDispatch>();  // BEFORE Super!
    Super::Initialize(Collection);
}
void UMySubsystem::OnWorldBeginPlay(UWorld& InWorld) {
    Super::OnWorldBeginPlay(InWorld);  // REQUIRED!
}
```

### Thread Safety
Game thread → `EnqueueCommand()` → Artillery executes before `progress()`

---

## Flecs API Reference

| Method | Returns | If Missing | Use When |
|--------|---------|------------|----------|
| `try_get<T>()` | `const T*` | `nullptr` | Read, might be missing |
| `get<T>()` | `const T&` | **ASSERT** | Read, guaranteed exists |
| `try_get_mut<T>()` | `T*` | `nullptr` | Write, might be missing |
| `get_mut<T>()` | `T&` | **ASSERT** | Write, guaranteed exists |
| `obtain<T>()` | `T&` | **Creates** | Write, create if missing |
| `set<T>(val)` | `entity&` | **Creates** | Set value |
| `add<T>()` | `entity&` | **Creates** | Add tag/component |

**USTRUCT Gotcha:** No aggregate init with GENERATED_BODY()
```cpp
// WRONG: entity.set<FItemInstance>({ 5 });
// CORRECT:
FItemInstance Instance;
Instance.Count = 5;
entity.set<FItemInstance>(Instance);
```

---

## SkeletonKey Types

| Nibble | Type |
|--------|------|
| 0x3 | GUN_SHOT (Projectiles) |
| 0x4 | BAR_PRIM (Barrage primitives) |
| 0x5 | ART_ACTS (Actors) |
| 0xC | ITEM |

---

## Known Issues

### Physics Body Not Deleted
**Symptom:** Collision remains after deletion.
**Fix:** Use `SetBodyObjectLayer(DEBRIS)` before `SuggestTombstone()`.

### Crash on PIE Exit (BodyManager::DestroyBodies)
**Cause:** `FinalizeReleasePrimitive()` corrupts Jolt state.
**Fix:** NEVER call it. Use DEBRIS layer + tombstone.

### Distance Constraint Spring Not Working
**Fix:** Delete Binaries/Intermediate folders, rebuild. Check log shows `Frequency=... Hz`.

---

## AFlecsCharacter Test Mode

| Property | Purpose |
|----------|---------|
| TestContainerDefinition | Container to spawn (E) |
| TestItemDefinition | Item to add to container (E) |
| TestEntityDefinition | Entity to spawn (E) if no container set |

**Controls:** E = spawn/add, F = destroy/remove all

---

## Build Requirements
- Visual Studio 2022 (VC++ 14.44, Win11 SDK 22621)
- CMake 3.30.2+
- Git LFS

## Detailed Docs
- `.claude/ARTILLERY_BARRAGE_DOCUMENTATION.md`
- `.claude/BARRAGE_BLUEPRINT_INTEGRATION.md`

## Broken Assets (cleanup needed)
- `Content/BP_MyArtilleryCharacter.uasset`
- `Content/DA_EnaceCont.uasset`
- `Content/DA_MyItemDef.uasset`
