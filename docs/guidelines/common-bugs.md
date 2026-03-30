# Common Bugs Reference

This is the definitive list of bugs encountered in FatumGame development, their root causes, and their fixes. Every developer must read this before writing ECS, physics, or threading code.

---

## Flecs Bugs

### World.each() with Tags Crashes

**Symptom:** `ecs_field_w_size` assertion failure, `column_index == -1`.

**Cause:** Passing a zero-size tag as a typed `const T&` parameter to `World.each()`. Flecs `iterable<>` preserves the reference, so `is_empty_v<const T&>` evaluates to `false`, and Flecs tries to access a data column that does not exist for tags.

!!! danger "Fix"
    Use `query_builder().with<FTag>().build()` + `.each([](flecs::entity E) {...})`. Access tags via `E.has<T>()` if needed. Note that `system<T>().with<Tag>().each()` is safe because the system builder strips references.

---

### Deferred Operations: 3 Cases

#### Case 1: Between `.run()` Systems

**Symptom:** System B cannot see component changes made by System A in the same tick.

**Cause:** `.run()` systems declare no component access, so Flecs skips the merge between them. `set<T>()` in system A writes to deferred staging; system B reads committed storage.

!!! danger "Fix"
    Use a subsystem-owned buffer (`TArray` member) for inter-system data. Do not rely on Flecs components for passing data between `.run()` systems.

#### Case 2: Within Same System

**Symptom:** `try_get<T>()` returns `nullptr` for a component you just added with `obtain<T>()` in the same callback.

**Cause:** `obtain()` / `set()` write to deferred staging. `try_get()` / `get()` read committed storage. Within the same callback, newly-added components are invisible.

!!! danger "Fix"
    Track data in local variables instead of re-reading from Flecs.

#### Case 3: Cross-Entity Tags

**Symptom:** A tag added to entity B inside a system iterating entity A is invisible to a later system this tick.

**Cause:** `entity.add<FTag>()` on a different entity is deferred. If the writing system doesn't declare FTag access, Flecs won't merge before the next system querying FTag.

!!! danger "Fix"
    Perform immediate side effects (e.g., `SetBodyObjectLayer(DEBRIS)`) instead of relying on a deferred tag being visible to a later system.

    **Real example:** `FragmentationSystem` added `FTagDead` to an intact entity. `DeadEntityCleanupSystem` didn't see it until the next tick. During that extra tick, fragments collided with the still-active intact body and exploded.

---

### Flecs Component Registration Order (Prefab Crash)

**Symptom:** Crash with `0x80000003` (breakpoint) inside Flecs when calling `Prefab.set<T>()`.

**Cause:** The component type `T` was not registered with `World.component<T>()` before being used in a `Prefab.set<T>()` call. Flecs needs the component metadata (size, alignment) to exist before it can store data on any entity, including prefabs.

!!! danger "Fix"
    Register every component type with `World.component<T>()` in `RegisterFlecsComponents()` inside `FlecsArtillerySubsystem_Systems.cpp` BEFORE any prefab or system references it. This is especially easy to forget when adding new Static components for new profiles.

---

### Iterator Drain Rules

#### `.run()` with Query Terms: MUST Drain

**Symptom:** `ECS_LEAK_DETECTED` in `flecs_stack_fini()` on PIE exit.

**Cause:** System has query terms but the callback did not consume all matches.

!!! danger "Fix"
    Always `while (It.next()) { ... }`. For early exit, call `It.fini()` first.

#### `.run()` without Query Terms: Do NOT Fini

**Symptom:** Crash at `flecs_query_iter_fini()`.

**Cause:** Double-fini. Systems with no query terms (`system<>("")`) have `EcsQueryMatchNothing` -- Flecs auto-fini's after `run()`.

!!! danger "Fix"
    Do not call `It.fini()` or `It.next()` on parameterless `.run()` systems.

---

## USTRUCT Bugs

### Aggregate Initialization with GENERATED_BODY()

**Symptom:** Wrong field values, compilation errors, or subtle data corruption.

**Cause:** UHT-generated structs have hidden members injected by `GENERATED_BODY()`. Aggregate initialization `{value}` assigns to the wrong field.

!!! danger "Fix"
    Always use named field assignment:
    ```cpp
    // WRONG
    entity.set<FItemInstance>({ 5 });

    // RIGHT
    FItemInstance Instance;
    Instance.Count = 5;
    entity.set<FItemInstance>(Instance);
    ```

---

### TUniquePtr in UHT-Generated Headers

**Symptom:** C4150 compiler error ("deletion of pointer to incomplete type").

**Cause:** `TUniquePtr<ForwardDeclaredType>` in a UCLASS header fails because UHT-generated `.gen.cpp` files instantiate the constructor/destructor, which need the complete type.

!!! danger "Fix"
    Use a raw pointer with manual `new` in `Initialize()` and `delete` in `Deinitialize()`.

---

## Physics (Barrage/Jolt) Bugs

### Barrage Thread Registration

**Symptom:** Crash or assertion when accessing Barrage from a Flecs worker thread.

**Cause:** Any thread touching Barrage must call `GrantClientFeed()` to register with Jolt's thread system.

!!! danger "Fix"
    Call `EnsureBarrageAccess()` at the start of any Flecs system that touches Barrage. It uses a `thread_local` guard for efficiency.

---

### Self-Damage / Projectile Spawn Race

**Symptom:** Projectile damages its own shooter on spawn.

**Cause:** The physics body is created and collides BEFORE the Flecs entity is fully set up. The `OnBarrageContact` callback fires for a body whose `FlecsId` is still 0.

!!! danger "Fix"
    In `OnBarrageContact`, require `FlecsId != 0` for both bodies before processing the collision.

---

### Owner Check in Collision Systems

**Symptom:** Projectile damages or bounces off its own shooter.

**Cause:** `DamageCollisionSystem` and `BounceCollisionSystem` were not checking the `OwnerEntityId` on projectiles.

!!! danger "Fix"
    Both systems must skip collisions where one body's `OwnerEntityId` matches the other body's entity ID.

---

### Aim Raycast Hitting Projectiles

**Symptom:** Aim raycast hits the player's own projectiles, causing erratic crosshair behavior.

**Cause:** The `CAST_QUERY` layer collides with the `PROJECTILE` layer.

!!! danger "Fix"
    Use `FastExcludeObjectLayerFilter({PROJECTILE, ENEMYPROJECTILE, DEBRIS})` for aim raycasts.

---

### SetPosition Breaks Trajectory

**Symptom:** Projectile goes in wrong direction after being teleported.

**Cause:** Physics body was created at `SimMuzzle` with velocity computed from `SimMuzzle`. Teleporting the body via `SetPosition` without adjusting velocity breaks bounce angles and trajectory.

!!! danger "Fix"
    Never `SetPosition` on a projectile without correcting its velocity to match the new position.

---

### FBarragePrimitive::SetPosition is QUEUED

**Symptom:** Position change is not visible until the next tick.

**Cause:** `SetPosition()` and `ApplyRotation()` enqueue to `ThreadAcc.Queue` and are applied during the next `DrainCommandQueue`.

!!! warning "Fix"
    When position must be committed immediately (e.g., before constraint `bAutoDetectAnchor`), use `SetBodyPositionDirect()` or `SetBodyRotationDirect()` which go through `body_interface` directly.

---

### MoveKinematicBody vs SetBodyPositionDirect

**Symptom:** Constraint anchor body lags behind its target position.

**Cause:** `SetBodyPositionDirect()` teleports the body but leaves velocity at zero. Jolt's constraint solver only applies Baumgarte stabilization (~5%/tick), causing massive lag.

!!! danger "Fix"
    Use `MoveKinematicBody(Key, TargetPos, DT)` which calls `body_interface->MoveKinematic()`. This sets velocity to `(target - current) / dt`, so the solver sees motion and can match it.

---

### FromJoltCoordinatesD is for Positions, Not Forces

**Symptom:** Constraints break instantly with impossibly large forces.

**Cause:** `CoordinateUtils::FromJoltCoordinatesD()` multiplies by 100 (meters to centimeters). Forces in Newtons only need axis swap (Y <-> Z), NOT the 100x multiplier.

!!! danger "Fix"
    Use a dedicated `JoltForceToUE` conversion (axis swap only) for forces. Reserve `FromJoltCoordinatesD` for position conversions.

---

### GetBarrageKeyFromSkeletonKey vs GetShapeRef

**Symptom:** Cannot find pool body by SkeletonKey.

**Cause:** Two different lookup tables:

| API | Lookup Table | Populated By |
|-----|-------------|--------------|
| `GetBarrageKeyFromSkeletonKey()` | `TranslationMapping` | `BindEntityToBarrage()` |
| `GetShapeRef()` | Body tracking | `CreatePrimitive()` |

Pool bodies are created via `CreatePrimitive` (in body tracking) but NOT bound via `BindEntityToBarrage` (not in `TranslationMapping`).

!!! danger "Fix"
    For pool bodies, use `GetShapeRef(Key)->KeyIntoBarrage`.

---

### Jolt Static Bodies Have No MotionProperties

**Symptom:** Crash on `GetMotionProperties()` -- returns nullptr.

**Cause:** Bodies created as `EMotionType::Static` / `NON_MOVING` do NOT allocate `MotionProperties`. Changing to `SetMotionType(Dynamic)` only changes a flag -- does NOT allocate MotionProperties retroactively.

!!! danger "Fix"
    Create the body as `Dynamic` / `MOVING` from the start if you need mass, damping, or velocity control. Use `AllowedDOFs = 0x3F` to bypass MOVING layer RotationY restriction. If constraint creation is in the same `EnqueueCommand` (before `StepWorld`), there is no 1-tick gravity fall.

---

### JPH Non-Copyable Filter

**Symptom:** C2280 compiler error ("attempting to reference a deleted function").

**Cause:** Ternary operator with `IgnoreSingleBodyFilter` tries to copy a non-copyable Jolt type.

!!! danger "Fix"
    Use if/else blocks instead of ternary when working with Jolt filter types.

---

### Close-Range Random Bullets

**Symptom:** Bullets go in random directions at close range.

**Cause:** Barrel parallax -- the muzzle position and the aim point create a divergent angle at short distances.

!!! danger "Fix"
    Enforce `MinEngagementDist = 300u` and add a dot product safety check (if `< 0.85`, use the aim direction instead of the muzzle-to-target direction).

---

## Character & Rendering Bugs

### GetMuzzleLocation Fallback Must Use Camera

**Symptom:** Shots fire from the wrong position when weapon mesh is not available.

**Cause:** Fallback used `GetActorLocation()` (capsule center, ~60 units below camera) instead of camera position, causing parallax.

!!! danger "Fix"
    Fallback must use `FollowCamera->GetComponentLocation()`.

---

### Death VFX Position

**Symptom:** Death VFX spawns at the wrong location.

**Cause:** By the time `DeadEntityCleanupSystem` runs, the physics body has bounced away from the collision point.

!!! danger "Fix"
    Store collision position in `FDeathContactPoint` at collision time. Use it in `DeadEntityCleanupSystem` for VFX placement.

---

### VelocityScale Compounding (Double Scaling)

**Symptom:** Player accelerates uncontrollably during time dilation with `bPlayerFullSpeed = true`.

**Cause:** `mLocomotionUpdate = SmoothedH * VelocityScale`. Next tick, `GetLinearVelocity()` returns the SCALED velocity. Smoothing reads this as `CurH` and multiplies by `VelocityScale` again. Compounds each frame.

!!! danger "Fix"
    Before locomotion smoothing, undo previous scaling:
    ```cpp
    if (VelocityScale > 1.001f)
    {
        CurH *= (1.f / VelocityScale);
    }
    ```
    Same applies to any `GetLinearVelocity()` readback used for speed checks (e.g., slide entry speed).

---

## UI & Widget Bugs

### TTripleBuffer: Write vs WriteAndSwap

**Symptom:** UI never updates despite writing new data.

**Cause:** `Write()` does not set the dirty flag. Only `WriteAndSwap()` publishes the new data to the read side.

!!! danger "Fix"
    Always use `WriteAndSwap()`, never bare `Write()`.

---

### ISM Orphan on Tombstone

**Symptom:** Invisible collision with a phantom object; ISM mesh remains after entity destruction.

**Cause:** `TombstonePrimitive()` destroyed the physics body without removing the corresponding ISM instance.

!!! danger "Fix"
    ISM cleanup was added to `TombstonePrimitive()` for `SFIX_BAR_PRIM` and `SFIX_GUN_SHOT` types.

---

## Lifecycle Bugs

### Deinitialize Race Condition (Use-After-Free)

**Symptom:** Crash on PIE exit inside Flecs `progress()` or Barrage code.

**Cause:** Game thread `Deinitialize()` destroys the Flecs world while the sim thread is still inside `progress()`.

!!! danger "Fix"
    Use atomic barriers: `bDeinitializing` + `bInArtilleryTick`. Deinitialize sets the flag and spin-waits until the sim thread exits its current tick.

---

### SelfPtr Race

**Symptom:** Crash in Flecs worker thread accessing `UBarrageDispatch::SelfPtr` during shutdown.

**Cause:** Deinitialize nulls `SelfPtr` while a worker thread is mid-system.

!!! danger "Fix"
    Cache the pointer as `CachedBarrageDispatch` at system registration time. Clear after sim thread has stopped.
