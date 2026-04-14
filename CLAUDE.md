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
Weapon/         — Components/ (Weapon, Projectile, Explosion, Penetration), Systems/ (DamageCollision, Penetration, WeaponSystems, Explosion), Library/ (Damage, Weapon, BulletHit, Hitscan)
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
| `FlecsWeaponComponents.h` | FAimDirection, FPelletRingData, FWeaponStatic, FWeaponInstance, FEquippedBy, EActiveLoadMethod, EWeaponReloadPhase, FChargeShotPayload, EChargeAmmoMode, FTagChargingWeapon |
| `FlecsExplosionComponents.h` | FExplosionStatic, FExplosionContactData, FTagDetonate |
| `FlecsPenetrationComponents.h` | EPenetrationMaterialCategory, GMaterialTable, FSurfaceIntegrity, FPenetrationStatic, FPenetrationInstance, FPenetrationMaterial |
| `FlecsDoorComponents.h` | FDoorStatic, FDoorInstance |
| **Plugin:** `FlecsBarrageComponents.h` | FBarrageBody, FISMRender, FCollisionPair, FTagCollision*, FTagCollisionPenetration |

**Profiles:** PhysicsProfile, RenderProfile, HealthProfile, DamageProfile, ProjectileProfile (+distance falloff), ContainerProfile, ItemDefinition, WeaponProfile (dual FireDelivery: Projectile/Hitscan; +Charge fields), InteractionProfile, QuickLoadProfile, ExplosionProfile

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
  FPenetrationStatic { PenetrationBudget, MaxPenetrations, DamageFalloffFactor, VelocityFalloffFactor, ImpulseTransferFactor }
  FPenetrationMaterial { MaterialCategory, RicochetCosAngleThreshold, bDegradable, BaseDegradeRate, DegradeSpreadFactor }  // on TARGET prefab
  FEntityDefinitionRef { EntityDefinition* }
         ↑ IsA (inheritance)
ENTITY (each instance) ─────────────────────────────────────
  FHealthInstance      { CurrentHP, RegenAccumulator }
  FProjectileInstance  { LifetimeRemaining, BounceCount, GraceFramesRemaining, FuseRemaining }
  FPenetrationInstance { RemainingBudget, PenetrationCount, CurrentDamageMultiplier, LastPenetratedTargetId }
  FSurfaceIntegrity    { Integrity[64], GridOriginU/V, InvCellSizeU/V, ActiveCols/Rows, Projection }  // lazy, per-entity, 148 bytes
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
| FTagCollisionPenetration | PenetrationSystem |
| FTagCollisionDamage | DamageCollisionSystem |
| FTagCollisionBounce | BounceCollisionSystem |
| FTagCollisionPickup | PickupCollisionSystem |
| FTagCollisionDestructible | DestructibleCollisionSystem |

### System Execution Order

```text
DamageObserver (event-driven)
WorldItemDespawnSystem → PickupGraceSystem → ProjectileLifetimeSystem → DebrisLifetimeSystem
PenetrationSystem → DamageCollisionSystem → BounceCollisionSystem → PickupCollisionSystem → DestructibleCollisionSystem
ExplosionSystem
ConstraintBreakSystem → FragmentationSystem → PendingFragmentationSystem
WeaponEquipSystem → WeaponChargeSystem → WeaponTickSystem → WeaponReloadSystem → WeaponFireSystem
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
(Applies when `WeaponProfile.FireDelivery == Projectile`. Hitscan bypasses all of this — no body, instant ray; see Hitscan Weapons section.)

| Bouncing | Gravity | Body Type | Use Case |
|----------|---------|-----------|----------|
| No | 0 | Sensor | Laser - flies straight |
| No | >0 | Dynamic | Grenade - falls, explodes on contact |
| Yes | any | Dynamic | Ricochet - bounces off walls |
| — | — | (none) | Hitscan - instant ray, no entity spawned |

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

## Bullet Penetration System

Projectiles pass through obstacles based on material resistance, thickness, and angle of incidence. CS:GO-style penetration budget model adapted for physics-simulated projectiles.

### Algorithm

```text
Contact → OnBarrageContact captures pre-collision velocity → FTagCollisionPenetration
  → PenetrationSystem (runs BEFORE DamageCollision):
    1. Check projectile has FPenetrationStatic + budget > 0
    2. Material lookup: SubShapeID (compound) or FPenetrationMaterial (single shape)
    3. Incidence angle: cosAngle = |dot(incomingDir, surfaceNormal)|
    4. If cosAngle < ricochetThreshold → skip (too oblique)
    5. Analytical thickness: ray-AABB slab intersection (~10ns, zero allocations)
    6. Surface integrity: EffectiveResistance = Resistance × IntegrityToResistance(cellIntegrity)
    7. EffectiveThickness = physicalThickness × EffectiveResistance / cosAngle
    8. If EffThickness >= RemainingBudget → degrade surface, let DamageCollision handle
    9. PENETRATE: teleport to exit, reduce damage/velocity, decrement budget
       + Degrade surface integrity (half rate for penetrating bullets)
       + FPendingFragmentation on destructibles (impulse = speed × ImpulseTransferFactor)
       + FTagCollisionProcessed → downstream systems skip pair
```

### Configuration

**Projectile (UFlecsProjectileProfile):**

| Field | Default | Description |
|-------|---------|-------------|
| `bPenetrating` | false | Enable penetration |
| `MaxPenetrations` | -1 | Max objects to penetrate (-1=unlimited) |
| `PenetrationBudget` | 20.0 | Budget in cm of material |
| `PenetrationDamageFalloff` | 1.0 | Damage loss per budget consumed |
| `PenetrationVelocityFalloff` | 0.5 | Speed loss per budget consumed |
| `PenetrationRicochetAngleDeg` | 70.0 | Min angle for penetration |
| `PenetrationImpulseTransfer` | 0.3 | Energy transfer to destructibles (0=clean pass, 1=full dump) |

**Target Material (UFlecsPhysicsProfile):**

| Field | Default | Description |
|-------|---------|-------------|
| `MaterialCategory` | `Impenetrable` | Material enum → resistance from GMaterialTable |
| `PenetrationRicochetAngleDeg` | 75.0 | Max oblique angle for penetration |
| `bDegradable` | true | Enable cumulative surface degradation |
| `BaseDegradeRate` | 0.08 | Override degrade rate (0.08=default, uses GMaterialTable if unchanged) |
| `DegradeSpreadFactor` | 0.3 | How much degradation bleeds to neighboring cells |
| `SurfaceGridCols` | 0 | Grid columns (0=auto from AABB, 1-8 manual) |
| `SurfaceGridRows` | 0 | Grid rows (0=auto from AABB, 1-8 manual) |
| `CompoundSubShapes[]` | empty | Multi-material compound body (see below) |

### Material Category System

`EPenetrationMaterialCategory` enum + `GMaterialTable[16]` lookup (resistance + degrade rate):

| Category | Resistance | DegradeRate | Hits to Breakthrough |
|----------|-----------|-------------|---------------------|
| Glass | 0.2 | 0.30 | ~3 |
| Wood_Thin | 0.5 | 0.12 | ~9 |
| Wood_Thick | 1.0 | 0.08 | ~13 |
| Sheet_Metal | 1.5 | 0.06 | ~17 |
| Metal | 3.0 | 0.03 | ~34 |
| Concrete | 4.0 | 0.02 | ~50 |
| Armor_Plate | 5.0 | 0.015 | ~67 |

### Compound Shapes (Multi-Material Objects)

Objects with `CompoundSubShapes[]` in PhysicsProfile create a `StaticCompoundShape` with per-sub-shape material. Jolt `SubShapeID` identifies which sub-shape was hit → material from `GetSubShapeUserData()`.

```text
Door = CompoundShape:
  SubShape[0]: Box(frame)  → Wood_Thin (resistance 0.5)
  SubShape[1]: Box(plate)  → Metal (resistance 3.0)
```

UserData stored with +1 offset (0 = no data for non-compound bodies).

### Surface Integrity Grid (Cumulative Degradation)

`FSurfaceIntegrity` — 148 bytes, lazily initialized on first bullet hit. 8×8 max grid of `uint16` integrity values with auto-projection (XZ/XY/YZ based on thinnest AABB axis).

- **Degrade per hit**: `DegradeRate × (BulletDamage / 25) / Resistance`. Penetrating bullets degrade at half rate.
- **Spread**: center cell + 4 neighbors at SpreadFactor (default 0.3)
- **IntegrityToResistance curve**: piecewise linear with knee at 0.3 — material holds, then rapidly collapses
- **Fragmentation trigger**: cell integrity < 5% on destructible → FPendingFragmentation
- **Both paths**: PenetrationSystem (penetrating bullets) AND DamageCollisionSystem (any bullets) degrade surface

### HP-Based Destruction

Destructible objects with `FHealthInstance` don't fragment on first hit:
- `FragmentationSystem` skips objects with HP > 0
- Bullets deal damage → HP drops → HP=0 → `DeathCheckSystem` → `FPendingFragmentation` → `FragmentEntity()`
- **HP** = global durability (10 hits anywhere), **Integrity Grid** = local weakness (concentrated fire in one zone)

### Damage Reduction After Penetration

`DamageCollisionSystem` reads `FPenetrationInstance::CurrentDamageMultiplier` — bullets that passed through obstacles deal reduced damage to subsequent targets.

### Ricochet Integration

- `MaxBounces > 0` + oblique angle → ricochet (existing bounce system)
- `MaxBounces == 0` + oblique angle → bullet stops (no ricochet, no penetrate)
- Normal angle + budget → penetrate

### Spurious Contact Suppression

After penetration, `LastPenetratedTargetId` prevents BounceCollisionSystem/DamageCollisionSystem from killing the bullet on re-contacts with the same target (or no-entity contacts) from the same StepWorld.

### Fragment Penetration

Fragments spawned by `FragmentEntity()` inherit `FPenetrationMaterial` from their `FragDef->PhysicsProfile`. Each fragment can have its own material resistance (wood, metal, glass fragments from same object).

### Key Files

- `Weapon/Public/Components/FlecsPenetrationComponents.h` — EPenetrationMaterialCategory, GMaterialTable, FSurfaceIntegrity, FPenetrationStatic, FPenetrationInstance, FPenetrationMaterial
- `Weapon/Private/Components/FlecsPenetrationComponents.cpp` — FromProfile(), FSurfaceIntegrity methods
- `Weapon/Private/Systems/FlecsArtillerySubsystem_PenetrationSystem.cpp` — PenetrationSystem
- `Weapon/Private/Systems/FlecsArtillerySubsystem_DamageCollision.cpp` — Damage reduction + surface degradation for non-penetrating bullets
- `Core/Private/FlecsArtillerySubsystem_Collision.cpp` — Classification + TryKillNonBouncing mod + SubShapeID routing
- `Core/Private/FlecsArtillerySubsystem_Systems.cpp` — DeathCheckSystem HP→fragmentation trigger
- `Definitions/Public/FlecsPhysicsProfile.h` — EPenetrationMaterialCategory, FSubShapeDefinition, compound + degradation settings

### Barrage Extensions (for penetration)

- `BarrageContactEvent.ProjectileVelocity` — pre-collision velocity captured in HandleContactAdded
- `BarrageContactEvent.SubShapeID1/2` — sub-shape IDs from Jolt ContactManifold
- `FCollisionPair.IncomingVelocity` — forwarded from contact event
- `FCollisionPair.SubShapeID2` — TARGET body's sub-shape ID (routed based on which body is projectile)
- `SetBodyLinearVelocityDirect()` — immediate velocity set (not queued), for teleport+velocity in same tick
- `CreateCompoundBody()` — compound shape from FBCompoundSubShape array with per-sub-shape UserData
- `GetSubShapeUserData()` — read compound sub-shape material via SubShapeID (unwraps decorators)
- `GetJoltBodyID()` — expose Jolt BodyID for body-specific operations
- `CastRayAllHits()` — multi-hit ray (NarrowPhaseQuery + `AllHitsRayCastCollector`), returns `TArray<FBarrageRayHit>` sorted ascending by distance. Decoupled POD result (no Jolt headers leaked to gameplay). Used by hitscan + future raycast queries.

### Shared Bullet-Hit Pipeline

`UFlecsBulletHitLibrary::ApplyBulletHit(World, Barrage, ShooterOrProjectile, Target, FBulletHitContext)` is the **single source of truth** for per-impact resolution: damage + crit + distance falloff + penetration + surface degradation + fragmentation trigger + (optional) impulse. Invoked by both the projectile collision path (`PenetrationSystem` / `DamageCollisionSystem`) and the hitscan path (`FlecsHitscanLibrary::FireHitscan`).

- `FBulletHitContext` (POD, const-ref in) — geometry (ImpactPoint, Normal, IncomingDir, SpawnPosition), bullet state (speed/damage/crit/falloff), impulse toggle, `FPenetrationStatic*` master switch, in-out penetration state pointers (budget/multiplier/count/last-id — all-null or all-non-null), cached AABB.
- `FBulletHitResult` — bTargetKilled, bPenetrated, bRicocheted, AppliedDamage, ExitPoint, PostHitDamageMultiplier.
- Caller remains responsible for: velocity falloff on penetrating projectiles, teleporting body to ExitPoint, `FTagCollisionProcessed`, `FDeathContactPoint`, `FTagDead`/`FTagDetonate` on projectile, tracer VFX.

---

## Hitscan Weapons

Instant ray-cast weapon delivery. Shares the penetration/damage/degradation/fragmentation pipeline with projectile weapons via `UFlecsBulletHitLibrary::ApplyBulletHit`.

### Fire Delivery vs Fire Mode

Two orthogonal axes on `UFlecsWeaponProfile`:

| Enum | Values | Axis |
|------|--------|------|
| `EWeaponFireMode` | SemiAuto / FullAuto / Burst | Trigger behaviour |
| `EWeaponFireDelivery` | Projectile / Hitscan | How the shot reaches the target |

### Algorithm

```text
WeaponFireSystem (per pellet):
  if WeaponStatic.IsHitscan():
    FlecsHitscanLibrary::FireHitscan(World, Barrage, Shooter, Weapon, MuzzlePos, Dir, ...)
      → BodyFilter excludes Shooter's Jolt body (no self-hit)
      → Barrage->CastRayAllHits(Origin, Dir * HitscanRange, ...)   // single BroadPhase traversal
      → for each FBarrageRayHit (sorted near→far):
           build FBulletHitContext (SpawnPosition = Origin → distance falloff active)
           ApplyBulletHit(...) → FBulletHitResult
           if not bPenetrated → stop (first-hit, non-penetrating)
           else: RemainingBudget updated, damage multiplier decays via PostHitDamageMultiplier
      → NiagaraMgr->EnqueueTracer({Start=Muzzle, End=lastHitOrRayEnd, Effect, Thickness, Duration})
  else:
    (existing projectile spawn path — unchanged)
```

### Configuration (UFlecsWeaponProfile, Hitscan-only fields)

| Field | Default | Description |
|-------|---------|-------------|
| `FireDelivery` | `Projectile` | Projectile spawn or Hitscan ray |
| `HitscanRange` | 100000 | Max ray length (cm) |
| `HitscanImpulseScale` | 300 | Multiplier on impulse applied to dynamic targets |
| `TracerEffect` | nullptr | Niagara system — **must** expose `User.BeamStart` (Vector), `User.BeamEnd` (Vector), `User.BeamThickness` (float) |
| `TracerThickness` | 1.0 | Uploaded to `User.BeamThickness` |
| `TracerDuration` | 0.06 | Seconds before pooled component is released |

### Damage Distance Falloff

Added to `UFlecsProjectileProfile` (reused by both projectile and hitscan paths since projectile profile supplies damage behaviour for the ammo type):

| Field | Default | Description |
|-------|---------|-------------|
| `DamageFalloffStart` | 5000 | Full damage inside this radius (cm) |
| `DamageFalloffEnd` | 20000 | Reaches `MinDamageMultiplier` at/after this distance |
| `MinDamageMultiplier` | 0.3 | Floor; set 1.0 to disable |

Mirrored on `FProjectileStatic` with precomputed `InvFalloffRange = 1/(End-Start)` (0 = disabled). `FProjectileInstance::SpawnPosition` is stamped at spawn by `FlecsEntitySpawner`; hitscan passes `Origin` directly as SpawnPosition into `FBulletHitContext`. ApplyBulletHit reads `Ctx.SpawnPosition` → distance = (ImpactPoint - SpawnPosition).Size → lerp factor → multiplier.

### Impulse

Hitscan has no physics body to transfer momentum, so `ApplyBulletHit` synthesises an impulse along `IncomingDir` scaled by `Ctx.ImpulseStrength` when `bApplyImpulse=true`. Projectile path leaves this false — Jolt's contact response already supplied momentum.

### Shooter Exclusion

`FireHitscan` builds a `JPH::BodyFilter` (specifically `IgnoreSingleBodyFilter`) around the shooter's Jolt body ID. Avoids self-hit when the character's capsule sits on the ray origin.

### Tracer Pool (FlecsNiagaraManager)

- 32 pre-allocated `UNiagaraComponent` slots, parented to a lazily-spawned `TracerHostActor` (game thread, on first enqueue drain).
- `EnqueueTracer(FPendingNiagaraTracer)` is MPSC-safe from sim thread; `ProcessPendingTracers()` runs on game thread from `FlecsArtillerySubsystem`.
- Slot claim: `AcquireTracerSlot(Now)` scans for `ReleaseTimeSeconds == 0.0` (free); sets `User.BeamStart/BeamEnd` + `BeamThickness`, re-activates component, schedules release at `Now + Duration`.
- **Exhaustion fallback:** `UNiagaraFunctionLibrary::SpawnSystemAtLocation` with `bAutoDestroy=true` + Verbose log (non-fatal; 32 slots × 0.06 s ≈ 530 shots/s headroom).

### Key Files

- `Source/FatumGame/Weapon/Public/Library/FlecsHitscanLibrary.h` — `FireHitscan()` entrypoint
- `Source/FatumGame/Weapon/Private/Library/FlecsHitscanLibrary.cpp` — raycast + per-hit loop + tracer enqueue
- `Source/FatumGame/Weapon/Public/Library/FlecsBulletHitLibrary.h` — `FBulletHitContext`, `FBulletHitResult`, `ApplyBulletHit()`
- `Source/FatumGame/Weapon/Private/Library/FlecsBulletHitLibrary.cpp` — unified damage/pen/degrade/impulse resolution
- `Source/FatumGame/Weapon/Private/Systems/WeaponFireSystem.cpp` — per-pellet Projectile/Hitscan branch
- `Source/FatumGame/Rendering/Public/FlecsNiagaraManager.h` + cpp — `FPendingNiagaraTracer`, `EnqueueTracer`, `ProcessPendingTracers`, pooled `UNiagaraComponent[32]`
- `Plugins/Barrage/Source/Barrage/Public/FBarrageRayHit.h` — POD ray hit result
- `Plugins/Barrage/Source/Barrage/Public/CollisionDetectionFilters/AllHitsRayCastCollector.h` — multi-hit Jolt collector
- `Plugins/Barrage/Source/Barrage/Public/BarrageDispatch.h` / `FWorldSimOwner.h` — `CastRayAllHits()`

---

## Charge Weapons

Hold-to-charge trigger behaviour: fire input accumulates a charge meter; on release (or at max) a single charged shot emits with scaled stats and optional multi-ammo burst. Mutually exclusive with `bEnableTriggerPull`.

### Trigger Flow

```text
Fire pressed (mag > 0) → bIsCharging=true, FTagChargingWeapon added
  ChargeAccumulator += DT (clamped to MaxChargeTime)
  if Accum >= Max && bAutoFireAtMaxCharge → emit, bPendingAutoRestart (until release)
  Fire released:
    Accum < MinChargeTime → cancel (no shot, no ammo)
    Accum >= MinChargeTime → t = Accum/Max → shapedT = Curve(t) or t
       build FChargeShotPayload{mults, AmmoCount} → WeaponInstance.PendingPayload
       bIsCharging=false, FTagChargingWeapon removed
WeaponFireSystem: Pending → Latched, fire once (multi-ammo loop), clear Latched
```

### FireMode × Charge Matrix

| FireMode | bAutoFireAtMaxCharge | bAutoRestartCharge | Behaviour |
|----------|----------------------|--------------------|-----------|
| SemiAuto | any | any | Charge/release per trigger press |
| FullAuto | **true** | false | Auto-emits at max; must release+repress to recharge |
| FullAuto | **true** | true | Auto-emits at max, auto-restarts while held (cycling) |
| FullAuto | false | — | **FORBIDDEN** — `checkf` in `FWeaponStatic::FromProfile` |
| Burst    | any | any | Treated like SemiAuto for charge purposes |

### Data Model

- `FChargeShotPayload` (plain struct, no GENERATED_BODY): `bValid`, `ShapedT`, per-dimension multipliers, `AmmoCount`. `bValid=false` = empty.
- **Two slots on `FWeaponInstance`** with strict ownership:
  - `PendingPayload` — owned by `WeaponChargeSystem` (writes on emit; clears on cancel). `WeaponFireSystem` only reads/consumes.
  - `LatchedPayload` — owned by `WeaponFireSystem` (Pending→Latched copy at tick-top; cleared after successful fire OR by reload-initiation site). `WeaponChargeSystem` never touches it.
- `FTagChargingWeapon` — added/removed around active charge (query optimization / VFX hook).
- Sim-thread fields on `FWeaponInstance`: `bIsCharging`, `ChargeAccumulator`, `bWasFireRequestedLastTick`, `bPendingAutoRestart`, `LastShotAmmoTypeIdx`.

### Configuration (UFlecsWeaponProfile, Charge-only)

| Field | Default | Description |
|-------|---------|-------------|
| `bEnableCharge` | false | Master enable. All below ignored when false. |
| `MinChargeTime` | 0.2 | Minimum hold (s) for release to produce a shot |
| `MaxChargeTime` | 1.5 | Saturation point (s); must exceed Min |
| `bAutoFireAtMaxCharge` | false | Auto-fire when full (no release needed) |
| `bAutoRestartCharge` | false | With AutoFire, restart charge while held |
| `ChargeCurve` | nullptr | `UCurveFloat` remapping [0,1] → [0,1] (null = linear) |
| `DamageMaxMultiplier` | 1.0 | Damage at t=1 |
| `ProjectileSpeedMaxMultiplier` | 1.0 | Projectile speed at t=1 |
| `PenetrationMaxMultiplier` | 1.0 | Penetration budget at t=1 |
| `SpreadMaxMultiplier` | 1.0 | Spread at t=1 (<1 tightens) |
| `BloomMaxMultiplier` | 1.0 | SpreadPerShot scalar on charged shot |
| `RecoilMaxMultiplier` | 1.0 | **Reserved** — not wired |
| `ImpulseMaxMultiplier` | 1.0 | **Reserved** — not wired |
| `ChargeAmmoMode` | `Fixed` | `Fixed` = `AmmoPerShot`; `ScalesWithCharge` = lerp |
| `MaxAmmoAtFullCharge` | 1 | Upper end of ammo lerp when `ScalesWithCharge` |

### Scaling Application

Applied at **6 local-copy mutation sites** inside `WeaponFireSystem` (no new override components): Projectile Speed, Spread, Hitscan Damage, Hitscan Penetration, Projectile Damage, Projectile Penetration. Bloom multiplier scales `SpreadPerShot` once for the burst.

### Multi-Ammo Burst

One "charged shot" may discharge N rounds in one tick:
- `Fixed` mode: `N = AmmoPerShot` (legacy behaviour).
- `ScalesWithCharge`: `N = round(lerp(AmmoPerShot, MaxAmmoAtFullCharge, shapedT))`.

Loop is **inside `WeaponFireSystem`** (not a separate system). Bloom growth + `FireCooldownRemaining` update apply **ONCE per whole burst** (outside loop). Per-round: ammo pop + projectile/hitscan emit. `ShotsFiredTotal` increments per round for pattern indexing.

### Cancel Hooks

- **Reload start** (`WeaponReloadSystem`) — clears `LatchedPayload` (FireSystem ownership).
- **Holster** (`WeaponEquipSystem`) — clears all charge state.
- **Inventory drag-out** (`FlecsContainerLibrary`) — mirror clear.
- **Any non-Idle reload phase** — `WeaponChargeSystem` step 2 cancels Pending + `bIsCharging` if `IsReloading()`, `bCycling`, or `ReloadPhase != Idle`.
- **Empty mag** blocks charge start.

### Empty-Latched Abort

If `LatchedPayload.bValid && mag empty at fire time` → latch is consumed with **NO cooldown, NO recoil, NO bloom**. Shot effectively never happened. Prevents locked-out weapon state.

### Key Files

- `Source/FatumGame/Weapon/Private/Systems/WeaponChargeSystem.cpp` — NEW 7-step state machine
- `Source/FatumGame/Weapon/Public/Components/FlecsWeaponComponents.h` — `FChargeShotPayload`, `EChargeAmmoMode`, `FTagChargingWeapon`, charge fields on Static/Instance
- `Source/FatumGame/Weapon/Private/Components/FlecsWeaponComponents.cpp` — `FromProfile` charge copy + validation `checkf`s
- `Source/FatumGame/Definitions/Public/FlecsWeaponProfile.h` — Charge config block
- `Source/FatumGame/Weapon/Private/Systems/WeaponFireSystem.cpp` — multi-ammo loop + multiplier application + charge-gated fire decision
- `Source/FatumGame/Weapon/Private/Systems/WeaponReloadSystem.cpp` — clears `LatchedPayload` on reload initiation
- `Source/FatumGame/Weapon/Private/Systems/WeaponEquipSystem.cpp` — clears all charge state on holster
- `Source/FatumGame/Item/Private/Library/FlecsContainerLibrary.cpp` — mirror clear on inventory-drag unequip
- `Source/FatumGame/Core/Private/FlecsArtillerySubsystem_Systems.cpp` — registers `FTagChargingWeapon`
- `Source/FatumGame/Weapon/Private/Systems/FlecsArtillerySubsystem_WeaponSystems.cpp` — `SetupWeaponChargeSystem()` between Equip and Tick

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
