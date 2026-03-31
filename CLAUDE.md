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

## Project Structure

Domain-based vertical folder layout under `Source/FatumGame/`:

```
Core/           — Simulation core (FlecsArtillerySubsystem, FSimulationWorker, FLateSyncBridge, FlecsGameTags)
  Components/   — FlecsHealthComponents, FlecsEntityComponents, FlecsInteractionComponents
Definitions/    — ALL Data Assets & Profiles (30 files)
Weapon/         — Components/ (Weapon, Projectile, Explosion), Systems/ (DamageCollision, WeaponSystems, Explosion), Library/ (Damage, Weapon)
Movement/       — Components/ (Movement), Character systems, FlecsCharacterTypes
Character/      — FlecsCharacter + all _*.cpp, FatumMovementComponent, FPostureStateMachine
Abilities/      — Components/ (AbilityTypes, States, Resources), Lifecycle, TickFunctions, CapsuleHelper
Destructible/   — Components/ (Destructible), Systems/ (Fragmentation, DestructibleCollision), Library/ (Constraint)
Door/           — Components/ (Door), Systems/ (DoorSystems)
Item/           — Components/ (Item), Systems/ (PickupCollision), Library/ (Container), ItemRegistry
Interaction/    — InteractionTypes, Library/ (InteractionLibrary)
Spawning/       — FlecsEntitySpawner, SpawnerActor, Library/ (SpawnLibrary)
Rendering/      — FlecsRenderManager, FlecsNiagaraManager
UI/             — FlecsUISubsystem, MessageSubsystem, HUD, Inventory, LootPanel
Input/          — FatumInputConfig, InputComponent, InputTags
Utils/          — FTimeDilationStack, ConeImpulse, ExplosionUtility, LedgeDetector, BarrageSpawnUtils
```

### Component Headers (domain-split)

| Header | Contains |
|--------|----------|
| `FlecsHealthComponents.h` | FHealthStatic, FDamageStatic, FHealthInstance, FDamageHit, FPendingDamage |
| `FlecsEntityComponents.h` | FEntityDefinitionRef, FFocusCameraOverride, FLootStatic |
| `FlecsInteractionComponents.h` | FInteractionStatic, FInteractionInstance, FInteractionAngleOverride |
| `FlecsProjectileComponents.h` | FProjectileStatic, FProjectileInstance |
| `FlecsItemComponents.h` | FItemStaticData, FContainerStatic, FItemInstance, FContainerInstance, FQuickLoadStatic, FTagQuickLoadDevice, etc. |
| `FlecsDestructibleComponents.h` | FDestructibleStatic, FDebrisInstance, FFragmentationData, FPendingFragmentation |
| `FlecsWeaponComponents.h` | FAimDirection, FPelletRingData, FWeaponStatic, FWeaponInstance, FEquippedBy, EActiveLoadMethod, EWeaponReloadPhase |
| `FlecsExplosionComponents.h` | FExplosionStatic, FExplosionContactData, FTagDetonate |
| `FlecsDoorComponents.h` | FDoorStatic, FDoorInstance |
| **Plugin:** `FlecsBarrageComponents.h` | FBarrageBody, FISMRender, FCollisionPair, FTagCollision* |

**Profiles:** PhysicsProfile, RenderProfile, HealthProfile, DamageProfile, ProjectileProfile, ContainerProfile, ItemDefinition, WeaponProfile, InteractionProfile, QuickLoadProfile, ExplosionProfile

---

## ECS Architecture

### Static/Instance Pattern (Prefab Inheritance)

```
PREFAB (one per type) ──────────────────────────────────────
  FHealthStatic     { MaxHP, Armor, RegenPerSecond, bDestroyOnDeath }
  FDamageStatic     { Damage, DamageType, bAreaDamage, AreaRadius, bDestroyOnHit }
  FProjectileStatic { MaxLifetime, MaxBounces, GracePeriodFrames, MinVelocity, FuseTime }
  FItemStaticData   { TypeId, MaxStack, Weight, GridSize, EntityDefinition* }
  FContainerStatic  { Type, GridWidth, GridHeight, MaxItems, MaxWeight }
  FInteractionStatic { MaxRange, bSingleUse }
  FQuickLoadStatic   { DeviceType, RoundsHeld, CaliberId, AmmoTypeDefinition*, InsertTime }
  FExplosionStatic   { Radius, BaseDamage, ImpulseStrength, DamageFalloff, ImpulseFalloff, VerticalBias, EpicenterLift, bDamageOwner }
  FEntityDefinitionRef { EntityDefinition* }
         ↑ IsA (inheritance)
ENTITY (each instance) ─────────────────────────────────────
  FHealthInstance      { CurrentHP, RegenAccumulator }
  FProjectileInstance  { LifetimeRemaining, BounceCount, GraceFramesRemaining, FuseRemaining }
  FItemInstance        { Count }
  FContainerInstance   { CurrentWeight, CurrentCount, OwnerEntityId }
  FContainedIn         { ContainerEntityId, GridPosition, SlotIndex }
  FBarrageBody         { SkeletonKey }  // Forward binding
  FISMRender           { Mesh, Scale }
  + Tags: FTagProjectile, FTagCharacter, FTagItem, FTagContainer, FTagDead, FTagInteractable, FTagDetonate
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

```text
DamageObserver (event-driven)
WorldItemDespawnSystem → PickupGraceSystem → ProjectileLifetimeSystem → DebrisLifetimeSystem
DamageCollisionSystem → BounceCollisionSystem → PickupCollisionSystem → DestructibleCollisionSystem
ExplosionSystem
ConstraintBreakSystem → FragmentationSystem → PendingFragmentationSystem
WeaponEquipSystem → WeaponTickSystem → WeaponReloadSystem → WeaponFireSystem
DoorSystems → StealthUpdateSystem → VitalsSystems
DeathCheckSystem → DeadEntityCleanupSystem → CollisionPairCleanupSystem (LAST)
```

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

## Quick-Load Devices (Stripper Clips, Speedloaders)

Batch-insert reload for internal magazines. Devices are pre-loaded inventory items that load multiple rounds at once.

- **Speedloader**: loads all chambers, requires empty mag, consumed on use
- **Stripper Clip**: loads N rounds (configurable), needs free slots, consumed on use

```text
Reload Start → Scan inventory (Speedloader > StripperClip > LooseRound)
  → Opening (OpenTimeDevice if device)
  → InsertingRound:
      Device? → BatchInsertTime → Push N rounds → Consume device → Re-scan or fallback
      Loose?  → InsertRoundTime → Push 1 round (existing behavior)
  → Closing (CloseTimeDevice if device used) → Idle
```

- **Batch non-cancellable**: cancel deferred until batch timer expires
- **Auto-fallback**: device depleted → switches to loose rounds automatically
- **Config**: WeaponProfile `bAcceptSpeedloaders`/`bAcceptStripperClips`, `OpenTimeDevice`, `CloseTimeDevice`
- **Data Asset**: `UFlecsQuickLoadProfile` on `UFlecsEntityDefinition::QuickLoadProfile`
- **ECS**: `FQuickLoadStatic` (prefab), `FTagQuickLoadDevice` (tag), `EActiveLoadMethod` (weapon instance state)
- **Key file**: `WeaponReloadSystem.cpp` — `ScanForQuickLoadDevice()` + batch insert logic

---

## Shotgun Pellet Spread (Ring-Based)

Fixed concentric rings with random rotation per shot (Overwatch-style Technique G).

```text
Config: FPelletRing { PelletCount, RadiusDecidegrees, AngularJitterDecidegrees, RadialJitterDecidegrees }
  → FWeaponStatic::PelletRings[4] (radians, via FromProfile)
  → WeaponFireSystem: bloom once → rotate pattern → per-pellet dual-axis jitter → spawn
```

- Legacy fallback: weapons without PelletRings use VRandCone per pellet
- `TArray<FVector, TInlineAllocator<16>>` for stack-allocated pellet directions
- **Key file**: `WeaponFireSystem.cpp`, `FlecsWeaponProfile.h` (FPelletRing USTRUCT)

---

## Explosion/Blast System

Radial damage + impulse for grenades, rockets, and other explosive projectiles. Hybrid architecture: `ApplyExplosion()` standalone utility (like ConeImpulse) + `ExplosionSystem` ECS wrapper.

### Detonation Triggers

```text
Impact (MaxBounces=0)  ──┐
Fuse timer (FuseTime>0) ─┤── FTagDetonate ── ExplosionSystem ── ApplyExplosion() ── FTagDead
Lifetime expiry         ──┘
```

- **Impact**: DamageCollisionSystem/BounceCollisionSystem adds `FTagDetonate` instead of `FTagDead` for projectiles with `FExplosionStatic`
- **Fuse**: `ProjectileLifetimeSystem` counts down `FuseRemaining`, adds `FTagDetonate` on expiry
- **Lifetime**: `ProjectileLifetimeSystem` adds `FTagDetonate` instead of `FTagDead` when `LifetimeRemaining` expires for explosive projectiles

### Data Flow

```text
FTagDetonate added → ExplosionSystem queries (FTagDetonate + FBarrageBody + FExplosionStatic)
  → Read epicenter from Barrage body position + EpicenterLift along contact normal
  → ApplyExplosion(Epicenter, FExplosionParams):
      SphereSearch (Barrage) → candidate bodies in Radius
      Per-candidate: CastRay (LOS check) → walls block damage/impulse
      Distance falloff: Factor = 1 - (Distance/Radius)^Exponent
      Queue FPendingDamage (BaseDamage * DamageFalloff factor)
      Apply impulse (ImpulseStrength * ImpulseFalloff factor + VerticalBias)
      Set FPendingFragmentation on destructible entities in radius (triggers PendingFragmentationSystem)
  → Enqueue explosion VFX (NiagaraManager MPSC)
  → Add FTagDead to projectile
```

### Configuration (UFlecsExplosionProfile)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `Radius` | `float` | 500 | Blast radius (cm) |
| `BaseDamage` | `float` | 100 | Damage at epicenter |
| `ImpulseStrength` | `float` | 5000 | Impulse at epicenter |
| `DamageFalloff` | `float` | 2.0 | Damage falloff exponent (higher = sharper drop) |
| `ImpulseFalloff` | `float` | 1.0 | Impulse falloff exponent |
| `VerticalBias` | `float` | 0.3 | Upward impulse bias [0,1] |
| `EpicenterLift` | `float` | 10.0 | Epicenter offset along contact normal (cm) |
| `bDamageOwner` | `bool` | true | Whether explosion can damage its owner |
| `ExplosionEffect` | `UNiagaraSystem*` | — | VFX system to spawn |
| `ExplosionEffectScale` | `float` | 1.0 | VFX scale |
| `DamageType` | `FGameplayTag` | — | Damage classification tag |

### ECS Components

- **FExplosionStatic** (prefab): All config fields above, populated via `FromProfile()`
- **FExplosionContactData** (transient, per-entity): `ContactNormal` — stored at collision time for epicenter lift direction
- **FTagDetonate** (tag): Marks entity for detonation this tick

### Key Files

- `Weapon/Public/Components/FlecsExplosionComponents.h` — FExplosionStatic, FExplosionContactData, FTagDetonate
- `Definitions/Public/FlecsExplosionProfile.h` — UFlecsExplosionProfile data asset
- `Utils/Public/ExplosionUtility.h` — FExplosionParams, ApplyExplosion()
- `Utils/Private/ExplosionUtility.cpp` — SphereSearch + LOS + damage + impulse logic
- `Weapon/Private/Systems/FlecsArtillerySubsystem_ExplosionSystems.cpp` — ExplosionSystem

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

### Fragmentation (Reusable Core)

```cpp
// FragmentEntity() — extracted core fragmentation logic in UFlecsArtillerySubsystem
// Called by both collision-based FragmentationSystem and explosion-based PendingFragmentationSystem
// FPendingFragmentation { ImpactPoint, ImpactDirection, ImpactImpulse } — set by ApplyExplosion() on destructibles
```

### CreateBouncingSphere

`CreateBouncingSphere()` accepts `Mass` and `AngularDamping` parameters from `PhysicsProfile`. Previously hardcoded to 0.1 kg. `WeaponFireSystem` and `FlecsEntitySpawner` both pass these values.

### Duplicate ItemTypeId Check

`GetOrCreateEntityPrefab()` crashes with `checkf()` if two different `UFlecsEntityDefinition` assets produce the same `ItemTypeId`. Prevents silent prefab overwrite that caused magazine mix-ups.

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
