# FatumGame - Project Documentation

## KEY DEVELOPMENT PRINCIPLES

### 1. NO WORKAROUNDS

**NEVER use workarounds or hacks!**

Always find and fix the **ROOT CAUSE** of the problem:
- BAD: "Remove ISM if body not found" - workaround hiding a leak
- GOOD: "Why is the body destroyed before ISM?" - find the root cause

**If you see a symptom - dig deeper until you find the root cause!**

### 2. FAIL-FAST

**Errors should manifest immediately and loudly!**

- Use `check()`, `ensure()`, `checkf()` for invariants
- Validate input data at the start of functions
- Don't silently swallow errors - log and crash/return error
- BAD: `if (!Ptr) return;` - silently hides a bug
- GOOD: `check(Ptr);` or `if (!Ptr) { UE_LOG(LogTemp, Error, TEXT("...")); return; }`

**The earlier an error is detected - the easier it is to find and fix!**

### 3. AVOID BOILERPLATE

**Code should be concise and expressive!**

- Don't duplicate code - extract to functions/templates
- Use auto, range-based for, structured bindings
- Prefer declarative style over imperative
- BAD: Copy-pasting the same logic in 5 places
- GOOD: One function/macro reused everywhere

```cpp
// BAD - Boilerplate
for (int32 i = 0; i < Array.Num(); ++i) { DoSomething(Array[i]); }

// GOOD - Concise
for (auto& Item : Array) { DoSomething(Item); }
```

**Less code = fewer bugs = easier maintenance!**

---

## Quick Reference

| Property | Value |
|----------|-------|
| **Engine** | Unreal Engine 5.7 |
| **Physics** | Jolt (via Barrage) |
| **ECS** | Flecs (via FlecsIntegration) |
| **Networking** | Bristlecone (deterministic rollback) |
| **Simulation** | 120Hz on Artillery thread |

### Architecture
```
Bristlecone   -> Network inputs (UDP, deterministic)
Cabling       -> Local inputs & keymapping
Artillery     -> Core simulation (~120Hz, separate thread)
Barrage       -> Jolt Physics
Flecs         -> Gameplay data (health, damage, items)
Game Thread   -> Cosmetics, rendering (non-deterministic)
```

---

## QUICK START

### 1. Create Projectile Data Asset
Content Browser -> RMB -> Data Asset -> **FlecsProjectileDefinition** -> `DA_Bullet`

### 2. Create Character Blueprint
Content Browser -> Blueprint Class -> **FlecsCharacter** -> `BP_Player`
- Projectile Definition = `DA_Bullet`
- Max Health = 100
- Add: Spring Arm + Camera

### 3. Configure Input (Project Settings -> Input)
```
Axis: MoveForward (W/S), MoveRight (A/D), Turn (Mouse X), LookUp (Mouse Y)
Action: Fire (LMB), Jump (Space)
```

### 4. Create GameMode
- Default Pawn = `BP_Player`
- Player Controller = `ABarragePlayerController`

### 5. On Map
- World Settings -> GameMode Override
- Add Player Start

**Play!** WASD works automatically via UBarrageCharacterMovement.

---

## Key Files (Source/FatumGame/)

| File | Purpose |
|------|---------|
| `FlecsCharacter.h/cpp` | Character with Flecs (health, damage, shooting, E/F spawn test) |
| `FlecsEntitySpawner.h/cpp` | **Unified Entity API**: FEntitySpawnRequest, UFlecsEntityLibrary |
| `FlecsEntityDefinition.h` | **Unified preset** combining all profiles |
| `FlecsPhysicsProfile.h` | Physics profile (mass, collision, layer) |
| `FlecsRenderProfile.h` | Render profile (mesh, material, scale) |
| `FlecsHealthProfile.h` | Health profile (HP, armor, regen) |
| `FlecsDamageProfile.h` | Damage profile (damage, area, crit) |
| `FlecsProjectileProfile.h` | Projectile profile (lifetime, bounces, speed) |
| `FlecsContainerProfile.h` | Container profile (Grid/Slot/List) |
| `FlecsItemDefinition.h` | Item definition (stacking, actions) |
| **`FlecsStaticComponents.h`** | **PREFAB components** (FHealthStatic, FDamageStatic, FProjectileStatic, FItemStaticData) |
| **`FlecsInstanceComponents.h/cpp`** | **Instance components** (FHealthInstance, FProjectileInstance, FItemInstance) |
| `FlecsComponents.h/cpp` | Game-specific tags (FTagItem, FTagCharacter, etc.) |
| `FlecsGameplayLibrary.h/cpp` | Blueprint API: SpawnProjectile, ApplyDamage, Heal |
| `FlecsArtillerySubsystem.h/cpp` | Artillery-Flecs bridge, lock-free bidirectional binding, collisions |

**Plugins/FlecsBarrage (NEW):**
| File | Purpose |
|------|---------|
| `FlecsBarrageComponents.h/cpp` | Bridge components: FBarrageBody, FISMRender, FCollisionPair, collision tags, constraints |

**Plugins/Barrage:** `FBarragePrimitive.h` - atomic `FlecsEntityId` for reverse binding

---

## Core Systems

### Artillery (120Hz Simulation)
- `UArtilleryDispatch` - World subsystem, 120Hz tick
- `FArtilleryBusyWorker` - Worker thread
- `FArtilleryGun` - Gun/ability instances

### Barrage (Jolt Physics)
- `UBarrageDispatch` - Physics world subsystem
- `UBarrageCharacterMovement` - Auto movement (bAutoProcessInput=true)
- `OnBarrageContactAddedDelegate` - Collision events (120Hz)
- `SetBodyObjectLayer(FBarrageKey, uint8)` - Change collision layer (use for entity cleanup!)
- `SuggestTombstone(FBLet)` - Safe deferred destruction (~19 sec)

### Flecs ECS
**Direct flecs::world** created in `FlecsArtillerySubsystem`, bypassing UnrealFlecs plugin ticks.

**Lock-free bidirectional binding** - see [Lock-Free Bidirectional Binding](#lock-free-bidirectional-binding-january-2025)

```cpp
// ═══════════════════════════════════════════════════════════════
// STATIC COMPONENTS (FlecsStaticComponents.h) - Live in PREFAB
// Immutable data shared by all entities of the same type
// ═══════════════════════════════════════════════════════════════
FHealthStatic     { MaxHP, Armor, RegenPerSecond, bDestroyOnDeath }
FDamageStatic     { Damage, DamageType, bAreaDamage, AreaRadius, bDestroyOnHit }
FProjectileStatic { MaxLifetime, MaxBounces, GracePeriodFrames, MinVelocity }
FLootStatic       { MinDrops, MaxDrops, LootTableId, DropChance }
FItemStaticData   { TypeId, MaxStack, Weight, GridSize, EntityDefinition*, ItemDefinition* }
FContainerStatic  { Type, GridWidth, GridHeight, MaxItems, MaxWeight }
FEntityDefinitionRef { EntityDefinition* }

// ═══════════════════════════════════════════════════════════════
// INSTANCE COMPONENTS (FlecsInstanceComponents.h) - Live on ENTITY
// Mutable per-entity data, static data comes from prefab via IsA
// ═══════════════════════════════════════════════════════════════
FHealthInstance   { CurrentHP, RegenAccumulator }
FProjectileInstance { LifetimeRemaining, BounceCount, GraceFramesRemaining }
FItemInstance     { Count }
FContainerInstance { CurrentWeight, CurrentCount, OwnerEntityId }
FContainerGridInstance { OccupancyMask }
FContainerSlotsInstance { SlotToItemEntity }
FWorldItemInstance { DespawnTimer, PickupGraceTimer, DroppedByEntityId }
FContainedIn      { ContainerEntityId, GridPosition, SlotIndex }

// ═══════════════════════════════════════════════════════════════
// BRIDGE COMPONENTS (FlecsBarrageComponents.h - Plugins/FlecsBarrage)
// ═══════════════════════════════════════════════════════════════
FBarrageBody      { SkeletonKey }  // Forward binding: Entity -> BarrageKey
FISMRender        { Mesh, Scale }  // ISM render instance
FConstraintLink   { ConstraintKey, OtherEntityKey, BreakForce, BreakTorque }
FFlecsConstraintData { Constraints[] }

// Collision Pair System
FCollisionPair    { EntityId1, EntityId2, Key1, Key2, ContactPoint, bBody1/2IsProjectile }

// Collision Tags (zero-size): FTagCollisionDamage, FTagCollisionBounce, FTagCollisionPickup, FTagCollisionDestructible

// ═══════════════════════════════════════════════════════════════
// GAME-SPECIFIC TAGS (FlecsComponents.h - Source/FatumGame)
// ═══════════════════════════════════════════════════════════════
// Entity Tags (zero-size): FTagItem, FTagContainer, FTagDestructible, FTagDead, FTagProjectile, FTagCharacter
```

**Systems (execution order):**
1. `WorldItemDespawnSystem` - World item despawn timer
2. `PickupGraceSystem` - Item pickup grace period
3. `ProjectileLifetimeSystem` - Lifetime, velocity check with grace period
4. `DamageCollisionSystem` - Apply damage from collision pairs
5. `BounceCollisionSystem` - Handle projectile bounces
6. `PickupCollisionSystem` - Handle item pickup
7. `DestructibleCollisionSystem` - Destroy destructibles
8. `DeathCheckSystem` - HP <= 0 -> FTagDead
9. `DeadEntityCleanupSystem` - Cleanup dead entities (ISM, physics, Flecs)
10. `CollisionPairCleanupSystem` - Destroy collision pairs (LAST)

### SkeletonKey (Entity Identity)
64-bit keys: `[Type:4][Metadata:28][Hash:32]`

| Nibble | Type |
|--------|------|
| 0x3 | GUN_SHOT (Projectiles) |
| 0x4 | BAR_PRIM (Barrage primitives) |
| 0x5 | ART_ACTS (Actors) |
| 0xC | ITEM |

---

## Blueprint API (UFlecsGameplayLibrary)

```cpp
// Spawn projectile (thread-safe)
UFlecsGameplayLibrary::SpawnProjectile(World, Definition, Location, Direction, Speed);

// Damage/Heal (thread-safe, enqueues to Artillery)
UFlecsGameplayLibrary::ApplyDamageByBarrageKey(World, TargetKey, 25.f);
UFlecsGameplayLibrary::HealEntityByBarrageKey(World, TargetKey, 50.f);
UFlecsGameplayLibrary::KillEntityByBarrageKey(World, EntityKey);
```

---

## AFlecsCharacter

**Features:**
- Flecs entity on BeginPlay (FHealthStatic/FHealthInstance, FTagCharacter)
- Auto damage from projectiles via collision
- Barrage physics movement
- **Test entity spawning** (E = spawn, F = destroy)
- **Container testing** (E = spawn container / add item, F = remove all items)

**Blueprint Events:**
- `OnDamageTaken(float Damage, float NewHealth)`
- `OnDeath()`
- `OnHealed(float Amount, float NewHealth)`

**Properties:**
- `ProjectileDefinition` - UFlecsProjectileDefinition Data Asset
- `TestEntityDefinition` - UFlecsEntityDefinition for spawn test (E/F)
- `TestContainerDefinition` - UFlecsEntityDefinition with ContainerProfile for container test
- `TestItemDefinition` - UFlecsEntityDefinition with ItemDefinition to add to container
- `MaxHealth`, `Armor`

**Input Actions (Enhanced Input):**
- `MoveAction`, `LookAction`, `JumpAction`, `FireAction`
- `SpawnItemAction` (E) - spawn TestEntityDefinition OR container/add item
- `DestroyItemAction` (F) - destroy last spawned OR clear container

**E/F Mode:**
- If `TestContainerDefinition` + `TestItemDefinition` are set -> container mode
- Otherwise -> entity spawn mode

---

## UFlecsProjectileDefinition (Data Asset)

| Property | Description |
|----------|-------------|
| Mesh, Material, VisualScale | Visuals |
| CollisionRadius, bIsBouncing, Restitution | Physics |
| DefaultSpeed | Movement |
| Damage, bAreaDamage, AreaRadius | Damage |
| LifetimeSeconds, MaxBounces | Lifetime |

---

## Collision Pair System (February 2025)

**Data-driven collision handling via Flecs ECS pattern.**

### Architecture

```
+------------------------------------------------------------------+
| BARRAGE COLLISION EVENT (OnBarrageContact)                       |
|   |                                                              |
|   v                                                              |
| Creates COLLISION PAIR ENTITY:                                   |
|   FCollisionPair { EntityId1, EntityId2, ContactPoint, ... }     |
|   + Classification tags based on entity types                    |
+------------------------------------------------------------------+
           | progress()
           v
+------------------------------------------------------------------+
| COLLISION SYSTEMS (query pairs by tags, process, mark done)      |
|                                                                  |
| DamageCollisionSystem:     FTagCollisionDamage                   |
| BounceCollisionSystem:     FTagCollisionBounce                   |
| PickupCollisionSystem:     FTagCollisionPickup                   |
| DestructibleCollisionSystem: FTagCollisionDestructible           |
+------------------------------------------------------------------+
           | end of tick
           v
+------------------------------------------------------------------+
| CollisionPairCleanupSystem: destroy all FCollisionPair entities  |
+------------------------------------------------------------------+
```

### Collision Pair Component

```cpp
struct FCollisionPair
{
    uint64 EntityId1, EntityId2;    // Flecs entity IDs (0 if not tracked)
    FSkeletonKey Key1, Key2;        // Barrage physics keys
    FVector ContactPoint;           // World contact point (from PointIfAny)
    bool bBody1IsProjectile;        // Barrage projectile flag
    bool bBody2IsProjectile;

    // Helpers
    uint64 GetProjectileEntityId() const;
    uint64 GetTargetEntityId() const;
    FSkeletonKey GetProjectileKey() const;
};
```

### Classification Tags

| Tag | When Added | Processed By |
|-----|------------|--------------|
| `FTagCollisionDamage` | FDamageStatic hits FHealthInstance | DamageCollisionSystem |
| `FTagCollisionBounce` | FTagProjectile entity collides | BounceCollisionSystem |
| `FTagCollisionPickup` | FTagCharacter touches FTagPickupable + FTagItem | PickupCollisionSystem |
| `FTagCollisionDestructible` | Projectile hits FTagDestructible | DestructibleCollisionSystem |
| `FTagCollisionCharacter` | Two FTagCharacter entities collide | (future: knockback) |
| `FTagCollisionProcessed` | System finished processing | Prevents double-processing |

### Collision Systems

**DamageCollisionSystem:**
- Queries: `FCollisionPair, FTagCollisionDamage, !FTagCollisionProcessed`
- Applies FDamageStatic damage to FHealthInstance (respects FHealthStatic.Armor)
- Kills non-bouncing projectiles after hit

**BounceCollisionSystem:**
- Queries: `FCollisionPair, FTagCollisionBounce, !FTagCollisionProcessed`
- Resets `FProjectileInstance.GraceFramesRemaining`
- Increments bounce count, kills if MaxBounces exceeded

**PickupCollisionSystem:**
- Queries: `FCollisionPair, FTagCollisionPickup, !FTagCollisionProcessed`
- Checks `FWorldItemInstance.CanBePickedUp()` grace period
- TODO: Transfer item to character inventory

**DestructibleCollisionSystem:**
- Queries: `FCollisionPair, FTagCollisionDestructible, !FTagCollisionProcessed`
- Adds FTagDead to destructible entities

### Adding New Collision Types

1. **Add classification tag** in `FlecsBarrageComponents.h` (Plugins/FlecsBarrage):
   ```cpp
   struct FTagCollisionMyType {};
   ```

2. **Register component** in `SetupFlecsSystems()`:
   ```cpp
   World.component<FTagCollisionMyType>();
   ```

3. **Add classification logic** in `OnBarrageContact()`:
   ```cpp
   if (/* my conditions */)
   {
       PairEntity.add<FTagCollisionMyType>();
   }
   ```

4. **Create processing system** in `SetupFlecsSystems()`:
   ```cpp
   World.system<const FCollisionPair>("MyCollisionSystem")
       .with<FTagCollisionMyType>()
       .without<FTagCollisionProcessed>()
       .each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
       {
           // Process collision
           PairEntity.add<FTagCollisionProcessed>();
       });
   ```

### System Execution Order

1. **DamageCollisionSystem** - Apply damage before death check
2. **BounceCollisionSystem** - Reset grace periods
3. **PickupCollisionSystem** - Handle item pickup
4. **DestructibleCollisionSystem** - Mark destructibles dead
5. **DeathCheckSystem** - HP <= 0 -> FTagDead
6. **DeadEntityCleanupSystem** - Cleanup dead entities
7. **CollisionPairCleanupSystem** - Destroy collision pairs (LAST)

---

## Threading Model

| Thread | Frequency | Purpose |
|--------|-----------|---------|
| Artillery BusyWorker | ~120Hz | Deterministic simulation |
| Ticklites | ~120Hz | Lightweight mechanics |
| StateTrees | Variable | AI |
| Game Thread | ~60Hz+ | Rendering, cosmetics |

**Thread Safety:** Game thread -> `EnqueueCommand()` -> Artillery executes before `progress()`

---

## Lock-Free Bidirectional Binding (January 2025)

**Architecture for Entity <-> Physics mapping WITHOUT locks:**

```
Forward lookup (Entity -> BarrageKey): O(1)
+-- Flecs sparse set: entity.get<FBarrageBody>()->BarrageKey

Reverse lookup (BarrageKey -> Entity): O(1)
+-- libcuckoo map: UBarrageDispatch::GetShapeRef(Key) -> FBLet
+-- atomic load:   FBLet->GetFlecsEntity()
```

### API (UFlecsArtillerySubsystem)

```cpp
// Bind/Unbind (sets both directions atomically)
void BindEntityToBarrage(flecs::entity Entity, FSkeletonKey BarrageKey);
void UnbindEntityFromBarrage(flecs::entity Entity);

// Lookups (lock-free O(1))
flecs::entity GetEntityForBarrageKey(FSkeletonKey BarrageKey) const;
FSkeletonKey GetBarrageKeyForEntity(flecs::entity Entity) const;
bool HasEntityForBarrageKey(FSkeletonKey BarrageKey) const;

// Flecs Stages (for future multi-threaded collision processing)
flecs::world GetStage(int32 ThreadIndex = 0) const;
```

### Implementation

**FBarragePrimitive.h** - Added atomic for reverse binding:
```cpp
std::atomic<uint64> FlecsEntityId{0};
void SetFlecsEntity(uint64 Id);
uint64 GetFlecsEntity() const;
void ClearFlecsEntity();
bool HasFlecsEntity() const;
```

**FlecsBarrageComponents.h** (Plugins/FlecsBarrage) - FBarrageBody is forward binding:
```cpp
struct FBarrageBody
{
    FSkeletonKey BarrageKey;
    bool IsValid() const { return BarrageKey.IsValid(); }
};
```

### Usage

```cpp
// Creating entities (use in EnqueueCommand lambda)
FHealthInstance HealthInstance;
HealthInstance.CurrentHP = 100.f;

flecs::entity Entity = FlecsWorld->entity()
    .is_a(Prefab)                           // Inherit FHealthStatic from prefab
    .set<FHealthInstance>(HealthInstance)
    .add<FTagCharacter>();
Subsystem->BindEntityToBarrage(Entity, Key);

// Collision handlers (direct O(1) lookup)
uint64 FlecsId = Body->GetFlecsEntity();  // atomic read
if (FlecsId != 0) {
    flecs::entity Entity = FlecsWorld->entity(FlecsId);
    // ...
}

// Cleanup
Subsystem->UnbindEntityFromBarrage(Entity);
Entity.destruct();
```

### Flecs Stages (Future Multi-Threading)

Stages are thread-local command queues for safe multi-threaded writes:
```cpp
// Each collision thread will use its own stage
flecs::world Stage = Subsystem->GetStage(ThreadIndex);
Stage.defer([&]() {
    Entity.add<FTagDead>();  // Deferred, merged at sync point
});
```

Currently all processing runs on Artillery thread (stage 0). Infrastructure ready for future parallel collision processing in Barrage.

---

## Static/Instance Component Architecture (February 2025)

**Status: Fully Migrated** ✅

All systems, spawner, and Blueprint API use the new Static/Instance architecture. Legacy components have been removed.

Components are split into **Static** (prefab) and **Instance** (per-entity) for:
- **Memory efficiency**: Static data stored once per type, not per entity
- **Easy balancing**: Change prefab → all entities updated
- **Better cache locality**: Instance components are smaller
- **Clear separation**: "What should be" (Static) vs "Current state" (Instance)

### File Structure

| File | Contents |
|------|----------|
| `FlecsStaticComponents.h` | Static (prefab) components: FHealthStatic, FDamageStatic, FProjectileStatic, etc. |
| `FlecsInstanceComponents.h/cpp` | Instance (per-entity) components: FHealthInstance, FProjectileInstance, etc. |
| `FlecsComponents.h/cpp` | Game-specific tags (FTagItem, FTagCharacter, etc.) |
| `FlecsBarrageComponents.h/cpp` (Plugin) | Bridge components: FBarrageBody, FISMRender, FCollisionPair, collision tags |

### Architecture

```
+------------------------------------------------------------+
| PREFAB (one per entity type)                               |
|   FHealthStatic { MaxHP, Armor, RegenPerSecond }           |
|   FDamageStatic { Damage, DamageType, bAreaDamage }        |
|   FProjectileStatic { MaxLifetime, MaxBounces }            |
|   FItemStaticData { TypeId, MaxStack, Weight, ... }        |
|   FEntityDefinitionRef { EntityDefinition* }               |
+------------------------------------------------------------+
           ^ IsA (inheritance)
           |
+------------------------------------------------------------+
| ENTITY (each instance)                                     |
|   (IsA, Prefab)         <- Inherits all Static components  |
|   FHealthInstance { CurrentHP }                            |
|   FProjectileInstance { LifetimeRemaining, BounceCount }   |
|   FItemInstance { Count }                                  |
|   FBarrageBody { BarrageKey }                              |
|   + Tags (FTagProjectile, FTagCharacter, etc.)             |
+------------------------------------------------------------+
```

### Memory Comparison (1000 enemies)

| Approach | Memory |
|----------|--------|
| OLD: FHealthData per entity | 1000 × 12 bytes = 12 KB |
| NEW: FHealthStatic (1) + FHealthInstance (1000) | 16 bytes + 1000 × 4 bytes = 4 KB |

### API for Static/Instance

```cpp
// ═══════════════════════════════════════════════════════════════
// SPAWNING WITH PREFAB
// ═══════════════════════════════════════════════════════════════

// Get or create prefab for entity type
flecs::entity Prefab = Subsystem->GetOrCreateEntityPrefab(EntityDefinition);

// Create entity with prefab inheritance
const FHealthStatic* Static = Prefab.try_get<FHealthStatic>();

FHealthInstance Instance;
Instance.CurrentHP = Static->MaxHP;  // Initialize from prefab

flecs::entity Entity = FlecsWorld->entity()
    .is_a(Prefab)                     // Inherit all Static components
    .set<FHealthInstance>(Instance)   // Add Instance data
    .set<FBarrageBody>({ Key })
    .add<FTagCharacter>();

// ═══════════════════════════════════════════════════════════════
// ACCESSING DATA
// ═══════════════════════════════════════════════════════════════

// Static data (from prefab via IsA - read-only)
const FHealthStatic* Static = Entity.try_get<FHealthStatic>();
float MaxHP = Static->MaxHP;
float Armor = Static->Armor;

// Instance data (from entity - mutable)
FHealthInstance* Instance = Entity.get_mut<FHealthInstance>();
Instance->CurrentHP -= Damage;

// ═══════════════════════════════════════════════════════════════
// APPLYING DAMAGE (example)
// ═══════════════════════════════════════════════════════════════

void ApplyDamage(flecs::entity Entity, float Damage)
{
    const FHealthStatic* Static = Entity.try_get<FHealthStatic>();
    FHealthInstance* Instance = Entity.get_mut<FHealthInstance>();
    if (!Static || !Instance) return;

    float EffectiveDamage = FMath::Max(0.f, Damage - Static->Armor);
    Instance->CurrentHP -= EffectiveDamage;

    if (Instance->CurrentHP <= 0.f && Static->bDestroyOnDeath)
    {
        Entity.add<FTagDead>();
    }
}
```

### Item Prefab API (UFlecsArtillerySubsystem)

```cpp
// Create/get prefab for item type
flecs::entity GetOrCreateItemPrefab(UFlecsEntityDefinition* EntityDef);

// Get prefab by TypeId
flecs::entity GetItemPrefab(int32 TypeId) const;

// Get Definition from item entity (via prefab)
UFlecsEntityDefinition* GetEntityDefinitionForItem(flecs::entity ItemEntity) const;
UFlecsItemDefinition* GetItemDefinitionForItem(flecs::entity ItemEntity) const;
```

### Flecs API Gotchas

**IMPORTANT when working with Flecs:**

```cpp
// WRONG: get<>() returns const T&, not pointer
const FItemStaticData* Data = entity.get<FItemStaticData>();  // ERROR!

// CORRECT: try_get<>() returns pointer (nullptr if missing)
const FItemStaticData* Data = entity.try_get<FItemStaticData>();

// WRONG: Aggregate init doesn't work with USTRUCT
entity.set<FItemInstance>({ 5 });  // COMPILE ERROR!

// CORRECT: Explicit initialization
FItemInstance Instance;
Instance.Count = 5;
entity.set<FItemInstance>(Instance);
```

---

## Critical Patterns

### Subsystem OnWorldBeginPlay
```cpp
void UMySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);  // REQUIRED!
    // ...
}
```

### Subsystem Dependencies
```cpp
void UMySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UArtilleryDispatch>();  // BEFORE Super!
    Super::Initialize(Collection);
}
```

### Entity Destruction

**Correct approach - DEBRIS layer + tombstone:**

```cpp
// Get primitive and Jolt key
FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
FBarrageKey BarrageKey = Prim->KeyIntoBarrage;

// Clear Flecs binding
Prim->ClearFlecsEntity();

// Move to DEBRIS layer - IMMEDIATELY disables collision with gameplay entities
// (DEBRIS only collides with NON_MOVING static geometry)
CachedBarrageDispatch->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);

// Tombstone for safe deferred destruction (~19 seconds)
CachedBarrageDispatch->SuggestTombstone(Prim);
```

**Why this works:**
1. `SetBodyObjectLayer(DEBRIS)` - instantly disables collision with players, projectiles, enemies
2. `SuggestTombstone()` - schedules safe destruction when all Jolt internal refs are cleared
3. No crash risk - Jolt handles the body lifecycle properly

**WRONG approach (causes crash on PIE exit):**
```cpp
// DON'T DO THIS - corrupts Jolt state, crashes during character cleanup:
CachedBarrageDispatch->FinalizeReleasePrimitive(BarrageKey);  // CRASH RISK!
```

### Flecs World
Direct `flecs::world` in FlecsArtillerySubsystem, NOT UFlecsWorld wrapper:
```cpp
FlecsWorld = MakeUnique<flecs::world>();
FlecsWorld->set_threads(0);  // Artillery is only executor
```

---

## Input System

- **Single-player:** Standard UE Input -> UBarrageCharacterMovement (bAutoProcessInput=true)
- **Multiplayer rollback:** Artillery Input -> Cabling -> Bristlecone -> IArtilleryLocomotionInterface
- Current AFlecsCharacter uses **standard UE Input**

---

## Build Requirements

- Visual Studio 2022 (VC++ 14.44, Win11 SDK 22621)
- CMake 3.30.2+ (for Jolt)
- Git LFS

---

## Detailed Documentation

| Document | Purpose |
|----------|---------|
| `.claude/ARTILLERY_BARRAGE_DOCUMENTATION.md` | Full architecture |
| `.claude/BARRAGE_BLUEPRINT_INTEGRATION.md` | Blueprint API |
| `.claude/BARRAGE_INTEGRATION_SETUP.md` | Setup guide |
| `.claude/BARRAGE_AUTO_MOVEMENT_GUIDE.md` | Movement docs |

---

## Unified Entity Spawning System (February 2025)

**Status: Fully Migrated** ✅ - Uses prefab architecture

### Architecture

All entities (items, projectiles, containers, characters) are spawned through a **unified API** with profile composition.

```
UFlecsEntityDefinition (unified preset)
    +-- UFlecsItemDefinition      (item logic)
    +-- UFlecsPhysicsProfile      (collision, mass)
    +-- UFlecsRenderProfile       (mesh, material)
    +-- UFlecsHealthProfile       (HP, armor)
    +-- UFlecsDamageProfile       (contact damage)
    +-- UFlecsProjectileProfile   (lifetime, bounces)
    +-- UFlecsContainerProfile    (inventory)
```

### Profiles (Data Assets with EditInlineNew)

Profiles can be:
1. **Created separately** as Data Asset and reused
2. **Created inline** directly inside EntityDefinition (Instanced)

| Profile | Purpose |
|---------|---------|
| `UFlecsPhysicsProfile` | CollisionRadius, Mass, Restitution, Friction, Layer, bIsSensor |
| `UFlecsRenderProfile` | Mesh, MaterialOverride, Scale, bCastShadow |
| `UFlecsHealthProfile` | MaxHealth, Armor, RegenPerSecond, bDestroyOnDeath |
| `UFlecsDamageProfile` | Damage, DamageType, bAreaDamage, AreaRadius, bDestroyOnHit |
| `UFlecsProjectileProfile` | DefaultSpeed, Lifetime, MaxBounces, GracePeriod |
| `UFlecsContainerProfile` | ContainerType (Grid/Slot/List), GridWidth/Height, MaxWeight |
| `UFlecsItemDefinition` | ItemTypeId, MaxStackSize, GridSize, Weight, Actions |

### UFlecsEntityDefinition (unified preset)

```cpp
UCLASS(BlueprintType)
class UFlecsEntityDefinition : public UPrimaryDataAsset
{
    // Profiles (Instanced - can create inline or select existing)
    TObjectPtr<UFlecsItemDefinition> ItemDefinition;
    TObjectPtr<UFlecsPhysicsProfile> PhysicsProfile;
    TObjectPtr<UFlecsRenderProfile> RenderProfile;
    TObjectPtr<UFlecsHealthProfile> HealthProfile;
    TObjectPtr<UFlecsDamageProfile> DamageProfile;
    TObjectPtr<UFlecsProjectileProfile> ProjectileProfile;
    TObjectPtr<UFlecsContainerProfile> ContainerProfile;

    // Tags
    bool bPickupable, bDestructible, bHasLoot, bIsCharacter;

    // Defaults
    int32 DefaultItemCount = 1;
    float DefaultDespawnTime = -1.f;
};
```

### Blueprint API (UFlecsEntityLibrary)

```cpp
// Spawn from unified definition
FSkeletonKey SpawnEntityFromDefinition(World, Definition, Location, Rotation);

// Spawn from request (full control)
FSkeletonKey SpawnEntity(World, FEntitySpawnRequest);

// Batch spawn
TArray<FSkeletonKey> SpawnEntities(World, TArray<FEntitySpawnRequest>);

// Destruction
void DestroyEntity(World, EntityKey);
void DestroyEntities(World, TArray<EntityKeys>);

// Health
bool ApplyDamage(World, TargetKey, Damage);
bool Heal(World, TargetKey, Amount);
void Kill(World, TargetKey);
float GetHealth(World, EntityKey);
float GetMaxHealth(World, EntityKey);

// Container operations (IMPLEMENTED February 2025)
bool AddItemToContainerFromDefinition(World, ContainerKey, EntityDef, Count, OutAdded);  // Prefab-based (recommended)
bool AddItemToContainer(World, ContainerKey, ItemDef, Count, OutAdded);  // Legacy (no EntityDef reference)
int32 RemoveAllItemsFromContainer(World, ContainerKey);                   // Working
int32 GetContainerItemCount(World, ContainerKey);                         // Working
bool RemoveItemFromContainer(World, ContainerKey, ItemEntityId, Count);   // Working

// Items (NOT YET IMPLEMENTED - need EntityDefinition from prefab for physics/render)
bool PickupItem(World, WorldItemKey, ContainerKey, OutPickedUp);          // TODO
FSkeletonKey DropItem(World, ContainerKey, ItemEntityId, Location, Count); // TODO (use GetEntityDefinitionForItem)
```

### FEntitySpawnRequest (C++ fluent builder)

```cpp
// Fluent API
FSkeletonKey Key = FEntitySpawnRequest::At(Location)
    .WithDefinition(DA_Bullet)         // or individual profiles:
    .WithPhysics(DA_SmallPhysics)
    .WithRender(DA_BulletMesh)
    .WithDamage(DA_BulletDamage)
    .WithProjectile(DA_FastProjectile)
    .WithVelocity(Direction * Speed)
    .Spawn(WorldContext);

// From definition
FSkeletonKey Key = FEntitySpawnRequest::FromDefinition(DA_HealthPotion, Location)
    .Pickupable()
    .WithDespawn(30.f)
    .Spawn(WorldContext);
```

### Composition Examples

| Entity | Profiles |
|--------|----------|
| Item in world | Item + Physics + Render + Pickupable |
| Projectile | Physics + Render + Damage + Projectile |
| Destructible box | Physics + Render + Health + Destructible |
| Chest | Physics + Render + Container |
| Player inventory | Container only (no world presence) |
| Trigger zone | Physics(Sensor) only |

### In-Game Testing

**AFlecsCharacter** has built-in test:
- `TestEntityDefinition` - assign EntityDefinition
- **E** - spawns entity in front of character
- **F** - destroys last spawned

**Input Actions (need to create):**
- `IA_SpawnItem` -> E
- `IA_DestroyItem` -> F

### Creating Test Entity

1. Content Browser -> Data Asset -> **FlecsEntityDefinition** -> `DA_TestCube`
2. In editor:
   - Physics Profile -> select type `FlecsPhysicsProfile` -> configure inline
   - Render Profile -> select type `FlecsRenderProfile` -> select Mesh
   - bPickupable = true
3. In BP_Player:
   - Test Entity Definition = `DA_TestCube`
   - Spawn Item Action = `IA_SpawnItem`
   - Destroy Item Action = `IA_DestroyItem`
4. Play -> E spawns cubes, F destroys

### Container Testing (February 2025)

**Creating Data Assets:**

1. **DA_TestContainer** (FlecsEntityDefinition):
   - Container Profile -> create `FlecsContainerProfile` inline -> ContainerType = `List`
   - Physics Profile -> create `FlecsPhysicsProfile` inline
   - Render Profile -> create `FlecsRenderProfile` inline -> select Mesh (cube/chest)

2. **DA_TestItem** (FlecsEntityDefinition):
   - Item Definition -> create `FlecsItemDefinition` inline -> ItemName = "TestItem"

**Character Setup (BP_Player):**
```
Test Container Definition = DA_TestContainer
Test Item Definition = DA_TestItem
Spawn Item Action = IA_SpawnItem (E)
Destroy Item Action = IA_DestroyItem (F)
```

**Controls:**
| Key | Action |
|-----|--------|
| **E** (1st time) | Spawns container in front of character |
| **E** (after) | Adds item to container |
| **F** | Removes ALL items from container |

**On-screen messages:**
- `Container spawned: 12345` (green)
- `Added item: TestItem (Container now has 3 items)` (cyan)
- `Removed all items from container (3 items removed)` (yellow)

**How it works under the hood:**
1. Container is created as Flecs entity with `FContainerStatic` (prefab) + `FContainerInstance` + `FTagContainer`
2. Items are created as separate Flecs entities with `FItemStaticData` (prefab) + `FItemInstance` + `FContainedIn`
3. `FContainedIn.ContainerEntityId` links item to container
4. On removal - query by `FContainedIn` finds all container items

---

## Known Issues / Debugging Tips

### Distance Constraint Spring Not Working (no elasticity)

**Symptom:** Distance Constraint spring doesn't bring bodies closer/apart, or does so very slowly.

**Cause:** Old compiled code used wrong conversion `SpringFrequency -> Stiffness` (freq * 10000), making spring ~54x stiffer than needed.

**Diagnosis:** Check log:
- OLD code: `DistanceConstraint SPRING: Stiffness=46400 N/m, Damping=311`
- NEW code: `DistanceConstraint SPRING: Frequency=15.00 Hz, Damping=0.31`

**Solution:**
1. Close Unreal Editor and Rider/VS
2. Delete folders: `Binaries/`, `Intermediate/`, `Plugins/Barrage/Binaries/`, `Plugins/Barrage/Intermediate/`
3. Rebuild Solution
4. Verify log shows `Frequency=... Hz`

**Spring parameters:**
- `SpringFrequency`: 1-5 Hz (soft), 10-15 Hz (medium), 20+ Hz (stiff)
- `SpringDamping`: 0 (endless oscillation), 0.3-0.5 (damped), 1.0 (no oscillation)

### Physics Body Not Deleted (collision remains after deletion)

**Symptom:** When deleting entity, mesh/ISM is removed but physics body continues to participate in collisions.

**Cause:** Using only `SuggestTombstone()` creates **deferred deletion ~19 seconds**. Body remains in Jolt simulation.

**Solution:** Move body to DEBRIS layer (doesn't collide with gameplay):
```cpp
// CORRECT - instant collision disable:
Prim->ClearFlecsEntity();
FBarrageKey BarrageKey = Prim->KeyIntoBarrage;
CachedBarrageDispatch->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);  // Disable collision
CachedBarrageDispatch->SuggestTombstone(Prim);                          // Safe deferred destroy

// WRONG - causes crash on PIE exit:
CachedBarrageDispatch->FinalizeReleasePrimitive(BarrageKey);  // DON'T DO THIS!
```

**Diagnosis:** Log should show:
- `PROJ_DEBUG Physics moved to DEBRIS layer: BarrageKey=...` - success

### Crash on PIE Exit (BodyManager::DestroyBodies)

**Symptom:** Crash in `JPH::BodyManager::DestroyBodies()` on PIE exit. Stack: `~UBarrageDispatch` -> `~FWorldSimOwner` -> `~CharacterVirtual` -> crash.

**Cause:** Calling `FinalizeReleasePrimitive()` during gameplay corrupts Jolt internal state. On shutdown, when CharacterVirtual tries to destroy its inner body, Jolt crashes.

**Why this happens:**
- `FinalizeReleasePrimitive()` calls `body_interface->RemoveBody()` + `DestroyBody()`
- Jolt holds internal references to bodies (contact cache, broad phase, constraints)
- Immediate deletion leaves dangling references
- Tombstone system (~19 sec) gives Jolt time to clear all references

**Solution:** NEVER call `FinalizeReleasePrimitive()` directly! Use:
1. `SetBodyObjectLayer(DEBRIS)` - instantly disables gameplay collisions
2. `SuggestTombstone()` - safe deferred deletion

**API for layer change:**
```cpp
// In FWorldSimOwner.h and BarrageDispatch:
void SetBodyObjectLayer(FBarrageKey BarrageKey, uint8 NewLayer);
```

**Collision layers (EPhysicsLayer.h):**
- `DEBRIS` - collides ONLY with `NON_MOVING` (static geometry)
- Doesn't collide with: MOVING, PROJECTILE, HITBOX, ENEMY, CHARACTER

### Flecs API Gotchas (February 2025)

**Symptom:** Compile errors when working with Flecs components.

**Problem 1: `get<>()` vs `try_get<>()`**
```cpp
// WRONG: get<>() returns const T&, not const T*
const FItemStaticData* Data = entity.get<FItemStaticData>();  // ERROR!

// CORRECT: try_get<>() returns pointer
const FItemStaticData* Data = entity.try_get<FItemStaticData>();
```

**Problem 2: Aggregate initialization with USTRUCT**
```cpp
// WRONG: USTRUCT with GENERATED_BODY() doesn't support aggregate init
entity.set<FItemInstance>({ 5 });           // COMPILE ERROR!
entity.set<FContainedIn>({ id, pos, -1 });  // COMPILE ERROR!

// CORRECT: Explicit initialization
FItemInstance Instance;
Instance.Count = 5;
entity.set<FItemInstance>(Instance);

FContainedIn Contained;
Contained.ContainerEntityId = id;
Contained.GridPosition = pos;
Contained.SlotIndex = -1;
entity.set<FContainedIn>(Contained);
```

**Note:** Plain C++ structs (without GENERATED_BODY) support aggregate init, USTRUCT does not.

---

## Broken Assets (need cleanup)

- `Content/BP_MyArtilleryCharacter.uasset` - broken Enace refs
- `Content/DA_EnaceCont.uasset` - orphaned
- `Content/DA_MyItemDef.uasset` - orphaned

---

## Credits

- **Artillery/Barrage/Bristlecone:** Hedra, Breach Dogs, Oversized Sun Inc.
- **Jolt Physics:** Jorrit Rouwe
- **Mass ECS Sample:** Karl Mavko, Alvaro Jover
