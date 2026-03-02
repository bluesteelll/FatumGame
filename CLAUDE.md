# FatumGame - Project Documentation

## KEY DEVELOPMENT PRINCIPLES

### 1. NO WORKAROUNDS
**NEVER use workarounds!** Find and fix the **ROOT CAUSE**.

### 2. FAIL-FAST
Use `check()`, `ensure()`, `checkf()`. Don't silently swallow errors.

### 3. AVOID BOILERPLATE
Extract to functions/templates. Use auto, range-based for, structured bindings.

### 4. ARCHITECTURE APPROVAL REQUIRED
**NEVER start implementing non-trivial changes without user approval.** Before writing code, present the architecture/plan and get explicit confirmation. Use plan mode for any feature that touches multiple files or involves design decisions.

### 5. ASK CLARIFYING QUESTIONS
**ALWAYS ask clarifying questions** when a request is ambiguous, has multiple valid approaches, or lacks important details. Don't assume — ask. It's better to ask 3 questions upfront than to rewrite code because of a wrong assumption.

---

## Quick Reference

| Property | Value |
|----------|-------|
| **Engine** | UE 5.7 |
| **Physics** | Jolt (Barrage) |
| **ECS** | Flecs (FlecsIntegration) |
| **Simulation** | 60Hz simulation thread (FSimulationWorker) |

```
FSimulationWorker -> Simulation thread (60Hz)
  DrainCommandQueue -> PrepareCharacterStep(RealDT,DilatedDT)
    -> StackUp -> StepWorld(DilatedDT) -> BroadcastContactEvents -> progress(DilatedDT)
Barrage     -> Jolt Physics
Flecs       -> Gameplay data (health, damage, items)
Game Thread -> Cosmetics, rendering, ISM spawns
Time Dilation -> FTimeDilationStack (game thread) -> atomics -> FSimulationWorker DT splitting
```

---

## Key Files

| File | Purpose |
|------|---------|
| `FlecsCharacter.h/cpp` | Character with Flecs (health, shooting, interaction, E/F test) |
| `FlecsEntitySpawner.h/cpp` | **Unified Entity API**: FEntitySpawnRequest, UFlecsEntityLibrary |
| `FlecsEntitySpawnerActor.h/cpp` | **Editor spawner**: drag to level, set EntityDefinition |
| `FlecsEntityDefinition.h` | Unified preset combining all profiles |
| `FlecsStaticComponents.h` | PREFAB components (FHealthStatic, FDamageStatic, etc.) |
| `FlecsInstanceComponents.h/cpp` | Instance components (FHealthInstance, etc.) |
| `FlecsGameTags.h/cpp` | Tags + ENUMs (FTagItem, FTagCharacter) |
| `FlecsDamageLibrary.h/cpp` | Blueprint API: damage, heal, health queries |
| `FlecsWeaponLibrary.h/cpp` | Blueprint API: weapon control (fire, reload, aim) |
| `FlecsContainerLibrary.h/cpp` | Blueprint API: container & item operations |
| `FlecsConstraintLibrary.h/cpp` | Blueprint API: physics constraints |
| `FlecsSpawnLibrary.h/cpp` | Blueprint API: spawn (projectiles, items, destructibles, groups) |
| `FlecsLibraryHelpers.h` | Shared inline helpers for library classes |
| `FlecsArtillerySubsystem.h/cpp` | Simulation subsystem: sim thread, collisions, binding |
| `FSimulationWorker.h/cpp` | Simulation thread (~60Hz, lock-free, time dilation atomics) |
| `FTimeDilationStack.h` | Time dilation priority stack (min-wins, per-source config) |
| `BarrageSpawnUtils.h/cpp` | Barrage spawn utilities |
| `FlecsRenderManager.h/cpp` | ISM component manager |
| **Plugin:** `FlecsBarrageComponents.h/cpp` | Bridge: FBarrageBody, FISMRender, FCollisionPair |
| **Plugin:** `FBarragePrimitive.h` | Atomic FlecsEntityId for reverse binding |

**Profiles:** PhysicsProfile, RenderProfile, HealthProfile, DamageProfile, ProjectileProfile, ContainerProfile, ItemDefinition, WeaponProfile, InteractionProfile

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
  FInteractionStatic { MaxRange, bSingleUse }
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
  + Tags: FTagProjectile, FTagCharacter, FTagItem, FTagContainer, FTagDead, FTagInteractable
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
8. WeaponTickSystem → 9. WeaponReloadSystem → 10. WeaponFireSystem
11. DeathCheckSystem → 12. DeadEntityCleanupSystem → 13. CollisionPairCleanupSystem (LAST)

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
UFlecsSpawnLibrary::SpawnProjectileFromEntityDef(World, Definition, Location, Direction, Speed, OwnerId);
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
// Damage/Heal (thread-safe, enqueues to simulation thread)
UFlecsDamageLibrary::ApplyDamageByBarrageKey(World, TargetKey, 25.f);
UFlecsDamageLibrary::HealEntityByBarrageKey(World, TargetKey, 50.f);
UFlecsDamageLibrary::KillEntityByBarrageKey(World, EntityKey);

// Containers
UFlecsContainerLibrary::AddItemToContainer(World, ContainerKey, EntityDef, Count, OutAdded);
UFlecsContainerLibrary::RemoveAllItemsFromContainer(World, ContainerKey);
UFlecsContainerLibrary::GetContainerItemCount(World, ContainerKey);
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
| `EntityDefinition` | — | Data Asset with profiles (required) |
| `InitialVelocity` | (0,0,0) | Initial velocity |
| `bOverrideScale` | false | Override scale from RenderProfile |
| `ScaleOverride` | (1,1,1) | Scale if bOverrideScale=true |
| `bSpawnOnBeginPlay` | true | Spawn on play start |
| `bDestroyAfterSpawn` | true | Destroy actor after spawning entity |
| `bShowPreview` | true | Show mesh preview in editor |

**Manual spawn:** Set `bSpawnOnBeginPlay=false`, then call `SpawnEntity()` from Blueprint/C++.

### Create Character
Blueprint Class → **FlecsCharacter** → `BP_Player`
- ProjectileDefinition = DA_Bullet
- Add: SpringArm + Camera

**Note:** Gravity is in PhysicsProfile.GravityFactor (0=laser, 1=grenade)

---

## Interaction System

Barrage raycast-based interaction with Flecs entities (chests, items, switches).
UE line traces can't see ECS entities (ISM-rendered, no UE actors). Uses Jolt physics queries instead.

```
Game Thread (10 Hz timer):
  Camera → SphereCast via Barrage → BodyID → BarrageKey → Flecs entity
    → has<FTagInteractable>()? → CurrentInteractionTarget

E Key (OnSpawnItem):
  if CurrentTarget valid → EnqueueCommand → sim thread:
    FTagPickupable + FTagItem → pickup (add FTagDead)
    FTagContainer → open container (TODO: UI)
    else → generic use
  else → existing test spawn behavior
```

| Component | Location | Purpose |
|-----------|----------|---------|
| `FInteractionStatic` | Prefab | MaxRange, bSingleUse |
| `FTagInteractable` | Entity | Tag for raycast filtering |
| `UFlecsInteractionProfile` | Data Asset | InteractionPrompt, InteractionRange, bSingleUse |

**Prompt text** is NOT stored in ECS — read via `FEntityDefinitionRef → EntityDefinition → InteractionProfile → InteractionPrompt`.

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
    Collection.InitializeDependency<UBarrageDispatch>();  // BEFORE Super!
    Super::Initialize(Collection);
}
void UMySubsystem::OnWorldBeginPlay(UWorld& InWorld) {
    Super::OnWorldBeginPlay(InWorld);  // REQUIRED!
}
```

### Thread Safety
Game thread → `EnqueueCommand()` → Simulation thread executes before `progress()`

### Time Dilation System

Reusable multi-source time dilation. Scales DeltaTime in both sim thread (physics/ECS) and game thread (animations/VFX).

```text
FTimeDilationStack (game thread, AFlecsCharacter::DilationStack)
  ├─ Push({Tag, DesiredScale, Duration, bPlayerFullSpeed, EntrySpeed, ExitSpeed})
  ├─ Remove(Tag)  // auto-captures ExitSpeed
  └─ Min-wins resolution: lowest DesiredScale among active entries wins

Game thread → atomics → Sim thread:
  DesiredTimeScale, bPlayerFullSpeed, TransitionSpeed
Sim thread → atomic → Game thread:
  ActiveTimeScalePublished (smoothed via FInterpTo)

FSimulationWorker::Run():
  RealDT (wall-clock) → × ActiveTimeScale → DilatedDT
  PrepareCharacterStep(RealDT, DilatedDT, TimeScale, bPlayerFullSpeed)
  StepWorld(DilatedDT), ProgressWorld(DilatedDT)
```

**Player compensation** (`bPlayerFullSpeed=true`): `VelocityScale = 1/TimeScale` on locomotion, jump, gravity. Jolt integrates `V×VelocityScale` with `DilatedDT` → net displacement = `V×RealDT`.

**CRITICAL:** `GetLinearVelocity()` returns SCALED velocity from previous frame. MUST undo scaling before locomotion smoothing: `CurH *= (1/VelocityScale)`. Otherwise VelocityScale compounds each frame → runaway acceleration.

```cpp
// Usage: push from any game-thread code
Character->DilationStack.Push({
    .Tag = "FreezeFrame", .DesiredScale = 0.03f,
    .Duration = 0.1f, .bPlayerFullSpeed = false
});
```

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
- `.claude/BARRAGE_BLUEPRINT_INTEGRATION.md`

## Broken Assets (cleanup needed)
- `Content/BP_MyArtilleryCharacter.uasset`
- `Content/DA_EnaceCont.uasset`
- `Content/DA_MyItemDef.uasset`
