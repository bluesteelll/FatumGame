# Glossary

> Key terms used throughout FatumGame documentation. If a term appears without explanation in another page, you'll find it here.

---

## Core Concepts

### Barrage
The Jolt Physics integration plugin. Wraps Jolt's `BodyInterface`, contact listeners, raycasts, and constraints in UE-friendly APIs. The central class is `UBarrageDispatch` (a `UTickableWorldSubsystem`).

### SkeletonKey
A 64-bit entity identifier used across all subsystems (Flecs, Barrage, Render Manager). The top nibble encodes the entity type:

| Nibble | Type | Description |
|--------|------|-------------|
| `0x3` | `GUN_SHOT` | Projectiles |
| `0x4` | `BAR_PRIM` | Generic Barrage primitives (items, destructibles) |
| `0x5` | `ART_ACTS` | Actors (characters) |
| `0xC` | `ITEM` | Item entities |

### FBarragePrimitive
A shared pointer (`TSharedPtr<FBarragePrimitive>`) that represents a single Jolt physics body. Holds the `SkeletonKey` (`KeyIntoBarrage`), an atomic `FlecsEntityId` for reverse binding, and queued position/rotation commands.

### Prefab
A Flecs entity created with `World.prefab()` that holds shared static data (max HP, damage, mesh reference). Instance entities inherit from prefabs via `IsA` relationship, paying zero memory for inherited components.

### Instance (Entity)
A Flecs entity that inherits from a prefab and adds per-entity mutable data (current HP, physics body key, ISM slot). Every projectile, item, and destructible in the world is an instance.

### ISM (Instanced Static Mesh)
UE's `UInstancedStaticMeshComponent`. All ECS entities sharing the same mesh+material are rendered through a single ISM, eliminating per-entity draw call overhead. Managed by `UFlecsRenderManager`.

### MPSC Queue
Multiple-Producer Single-Consumer queue (`TQueue<T, EQueueMode::Mpsc>`). Used for all cross-thread data transfer where ordering matters. Multiple game-thread callers can enqueue simultaneously; the sim thread drains sequentially.

---

## Threading

### Game Thread
The UE main thread. Handles input, camera, ISM rendering, UI, and Niagara VFX. Never mutates Flecs world state directly — all mutations go through `EnqueueCommand`.

### Simulation Thread (Sim Thread)
`FSimulationWorker` — a dedicated `FRunnable` thread ticking at ~60 Hz. Owns the Flecs world and drives Jolt physics. The only thread allowed to call Flecs APIs (except during parallel `progress()`).

### Flecs Worker Threads
Background threads spawned by Flecs during `world.progress()` for parallel system execution. Count = CPU cores - 2. Must call `EnsureBarrageAccess()` before touching Jolt APIs.

### EnqueueCommand
`UFlecsArtillerySubsystem::EnqueueCommand(TFunction<void()>)` — the primary game → sim thread mutation path. Lambdas are drained at the start of each sim tick.

### FLateSyncBridge
A latest-value-wins atomic bridge for data where only the most recent value matters (aim direction, camera position). Overwrite semantics — no queue, no backpressure.

### FSimStateCache
A 16-slot, cache-line-aligned, lock-free cache for sim → game scalar data (health, weapon ammo, ability resources). Packs values into atomic `uint64` for zero-contention reads.

---

## ECS

### Component
A USTRUCT registered with Flecs that holds data attached to entities. Components can be static (on prefabs) or instance (per-entity).

### Tag
A zero-size USTRUCT (`sizeof == 0`) used for classification and filtering. Tags have no data — they serve as boolean markers (e.g., `FTagDead`, `FTagProjectile`).

### System
A Flecs system that runs every tick during `world.progress()`. Registered with `World.system<T>()`. Systems execute in registration order within each phase.

### Observer
A Flecs observer that fires reactively on a specific event (e.g., `flecs::OnSet`). Only `DamageObserver` uses this pattern in FatumGame.

### Collision Pair
A temporary Flecs entity created per physics contact. Contains `FCollisionPair` (EntityA, EntityB IDs) and one or more collision tags. Destroyed by `CollisionPairCleanupSystem` at the end of each tick.

### Deferred Operation
A Flecs mutation (set, add, remove) that is staged during system execution and applied at the next merge point. Source of several [critical bugs](../guidelines/ecs-best-practices.md).

---

## Physics

### Jolt
The physics engine behind Barrage. Handles rigid body simulation, collision detection, raycasts, and constraints. All Jolt calls go through Barrage's API, not direct Jolt APIs.

### Object Layer
Jolt's collision filtering mechanism. Each body belongs to a layer that determines which other layers it collides with:

| Layer | Used For |
|-------|---------|
| `MOVING` | Active dynamic bodies (fragments, doors) |
| `NON_MOVING` | Static world geometry, intact destructibles |
| `PROJECTILE` | Player projectiles |
| `ENEMYPROJECTILE` | Enemy projectiles |
| `DEBRIS` | Dead/dormant bodies (no collision response) |
| `CHARACTER` | Player and NPC physics capsules |
| `SENSOR` | Trigger volumes, pickup zones |
| `CAST_QUERY` | Raycast-only geometry |

### Tombstone
The safe destruction pattern for physics bodies. Instead of deleting immediately (which crashes Jolt during `StepWorld`), set the layer to `DEBRIS` (instant collision disable) and call `SuggestTombstone()` (deferred cleanup after ~19 seconds).

### FDebrisPool
A ring-buffer pool of pre-allocated Barrage dynamic box bodies. Used by the destructible system to avoid runtime body allocation during fragmentation. Bodies cycle between `DEBRIS` (dormant) and `MOVING` (active) layers.

### Constraint
A Jolt physics constraint linking two bodies. Types used: Fixed (destructible fragments), Hinge (doors), Distance (chains). Created via `FBarrageConstraintSystem` or `UFlecsConstraintLibrary`.

---

## Data Assets

### UFlecsEntityDefinition
The master data asset. References all profiles that define an entity type. Created in the Content Browser and assigned to spawners.

### Profile
A `UObject` subclass (EditInlineNew) embedded in `UFlecsEntityDefinition`. Each profile contributes specific data to the entity prefab (e.g., `UFlecsHealthProfile` → `FHealthStatic`, `UFlecsPhysicsProfile` → Barrage body config).

### UFlecsItemDefinition
A standalone data asset for item metadata (display name, icon, stack size, grid size, rarity). Referenced by `UFlecsEntityDefinition` for items.

---

## Rendering

### Render Interpolation
The technique of lerping between the previous and current physics positions using a sub-tick alpha, producing smooth visuals at any framerate despite the 60 Hz physics tick rate.

### Alpha (Interpolation)
`Alpha = (CurrentTime - LastSimTickTime) / SimDeltaTime`. A value in `[0, 1]` that represents how far between two sim ticks the current render frame is. Alpha = 0 means use the previous position; Alpha = 1 means use the current position.

### PivotOffset
A correction vector applied to ISM transforms. UE meshes may not have their pivot at the bounding box center, so `PivotOffset = -Bounds.Origin` is rotated and added to each instance position.

### FEntityTransformState
Per-entity render state: `PrevPosition`, `CurrPosition`, `PrevRotation`, `CurrRotation`, `LastUpdateTick`, `bJustSpawned`. Used by the render manager for interpolation.

---

## Time Dilation

### FTimeDilationStack
A game-thread priority stack that manages multiple time dilation sources. Resolution rule: min-wins (the lowest `DesiredScale` among active entries takes effect).

### RealDT
Wall-clock delta time: `FPlatformTime::Seconds()` difference between ticks. Unaffected by time dilation.

### DilatedDT
`RealDT * ActiveTimeScale`. Used by physics (`StepWorld`) and ECS (`progress`).

### VelocityScale
`1.0 / ActiveTimeScale` when `bPlayerFullSpeed = true`. Applied to character locomotion so the player moves at real-time speed while the world runs in slow-motion.

---

## UI

### FlecsUI Plugin
A plugin providing base classes for lock-free, model/view UI panels: `UFlecsUIPanel` (CommonUI activatable), `UFlecsUIWidget` (plain UUserWidget), `UFlecsContainerModel`, `UFlecsValueModel`.

### Triple Buffer (TTripleBuffer)
A lock-free triple-buffering primitive. The sim thread writes snapshots via `WriteAndSwap()`; the game thread reads the latest complete snapshot. Used for container state sync.

### UFlecsActionRouter
A custom input router that manages cursor visibility, mouse capture, and look/move input blocking when UI panels are active. Replaces the need for CommonUI's `ActionDomainTable`.

### Optimistic Drag-Drop
The inventory UI pattern where items are moved visually immediately on drag, without waiting for sim-thread confirmation. If the sim thread rejects the operation, the UI reverts.
