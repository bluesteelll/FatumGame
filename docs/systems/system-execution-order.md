# System Execution Order

> All Flecs systems run during `world.progress()` on the simulation thread. They execute in the order they are registered in `SetupFlecsSystems()`. This page lists every system with its purpose, inputs, outputs, and dependencies.

---

## Registration Order

Systems are registered in `FlecsArtillerySubsystem_Systems.cpp::SetupFlecsSystems()`. The registration order **is** the execution order.

```mermaid
graph TD
    DamageObs["DamageObserver<br/>(reactive — OnSet)"]

    S1["1. WorldItemDespawnSystem"]
    S2["2. PickupGraceSystem"]
    S3["3. ProjectileLifetimeSystem"]
    S4["4. DebrisLifetimeSystem"]
    S5["5. DamageCollisionSystem"]
    S6["6. BounceCollisionSystem"]
    S7["7. PickupCollisionSystem"]
    S8["8. DestructibleCollisionSystem"]
    S9["9. ConstraintBreakSystem"]
    S10["10. FragmentationSystem"]
    S11["11. TriggerUnlockSystem"]
    S12["12. DoorTickSystem"]
    S13["13. WeaponTickSystem"]
    S14["14. WeaponReloadSystem"]
    S15["15. WeaponFireSystem"]
    S16["16. DeathCheckSystem"]
    S17["17. DeadEntityCleanupSystem"]
    S18["18. CollisionPairCleanupSystem"]

    S1 --> S2 --> S3 --> S4
    S4 --> S5 --> S6 --> S7 --> S8
    S8 --> S9 --> S10
    S10 --> S11 --> S12
    S12 --> S13 --> S14 --> S15
    S15 --> S16 --> S17 --> S18

    DamageObs -.->|"fires during S5"| S16
```

---

## System Details

### 1. WorldItemDespawnSystem

| Property | Value |
|----------|-------|
| **Queries** | `FWorldItemInstance`, `FTagItem`, without `FTagDead` |
| **Does** | Counts down `DespawnTimer`. Adds `FTagDead` when expired. |
| **Why first** | Items should despawn before collision systems process them. |

### 2. PickupGraceSystem

| Property | Value |
|----------|-------|
| **Queries** | `FWorldItemInstance` |
| **Does** | Counts down `PickupGraceTimer`. Freshly-dropped items can't be picked up until timer reaches 0. |
| **Why here** | Must run before `PickupCollisionSystem` checks `CanBePickedUp()`. |

### 3. ProjectileLifetimeSystem

| Property | Value |
|----------|-------|
| **Queries** | `FProjectileInstance`, `FTagProjectile`, without `FTagDead` |
| **Does** | Decrements `LifetimeRemaining`. Checks minimum velocity. Adds `FTagDead` if expired or too slow. |
| **Grace period** | `GraceFramesRemaining` prevents premature velocity kill right after spawn. |

### 4. DebrisLifetimeSystem

| Property | Value |
|----------|-------|
| **Queries** | `FDebrisInstance`, `FTagDebrisFragment` |
| **Does** | Counts down debris fragment lifetime. On expiry: recycles body to `FDebrisPool`, removes ISM instance. |

### 5. DamageCollisionSystem

| Property | Value |
|----------|-------|
| **Queries** | `FCollisionPair`, `FTagCollisionDamage` |
| **Does** | Reads `FDamageStatic` from projectile. Owner check. `obtain<FPendingDamage>().AddHit()`. Kills non-bouncing projectile. |
| **Triggers** | `DamageObserver` (via `modified<FPendingDamage>()`). |
| **Setup** | `SetupDamageCollisionSystems()` |

### 6. BounceCollisionSystem

| Property | Value |
|----------|-------|
| **Queries** | `FCollisionPair`, `FTagCollisionBounce` |
| **Does** | Increments `FProjectileInstance.BounceCount`. Kills if over `MaxBounces`. |
| **Setup** | `SetupDamageCollisionSystems()` |

### 7. PickupCollisionSystem

| Property | Value |
|----------|-------|
| **Queries** | `FCollisionPair`, `FTagCollisionPickup` |
| **Does** | Identifies character and item entities. Checks `CanBePickedUp()`. Calls `PickupWorldItem()`. |
| **Setup** | `SetupPickupCollisionSystems()` |

### 8. DestructibleCollisionSystem

| Property | Value |
|----------|-------|
| **Queries** | `FCollisionPair`, `FTagCollisionDestructible` |
| **Does** | Adds `FTagDead` to the destructible entity. |
| **Setup** | `SetupDestructibleCollisionSystems()` |

### 9. ConstraintBreakSystem

| Property | Value |
|----------|-------|
| **Queries** | `FFlecsConstraintData` |
| **Does** | Pass 1: Polls Jolt for broken constraints. Pass 2: BFS to find disconnected fragment groups. Pass 3: Door constraint break. |
| **Why before Fragmentation** | Must process existing constraint breaks before new fragments are created. |
| **Setup** | `SetupDestructibleCollisionSystems()` |

### 10. FragmentationSystem

| Property | Value |
|----------|-------|
| **Queries** | `FCollisionPair`, `FTagCollisionFragmentation` |
| **Does** | Spawns debris fragments from `FDebrisPool`. Creates Jolt constraints per adjacency. World anchors for bottom fragments. Enqueues `FPendingFragmentSpawn`. |
| **Immediately** | Invalidates `FDestructibleStatic.Profile`, moves body to DEBRIS layer (no deferred wait). |
| **Setup** | `SetupDestructibleCollisionSystems()` |

### 11. TriggerUnlockSystem

| Property | Value |
|----------|-------|
| **Queries** | `FDoorTriggerLink`, `FTagDoorTrigger` |
| **Does** | Resolves trigger → door linkage. Sets `FDoorInstance.bUnlocked = true`. |
| **Why before DoorTick** | Door must know it's unlocked before the state machine ticks. |
| **Setup** | `SetupDoorSystems()` |

### 12. DoorTickSystem

| Property | Value |
|----------|-------|
| **Queries** | `FDoorStatic`, `FDoorInstance` |
| **Does** | 5-state machine: Locked → Closed → Opening → Open → Closing. Controls constraint motor. Auto-close timer. |
| **Setup** | `SetupDoorSystems()` |

### 13. WeaponTickSystem

| Property | Value |
|----------|-------|
| **Queries** | `FWeaponStatic`, `FWeaponInstance` |
| **Does** | Fire cooldown decay. Burst cooldown. Semi-auto trigger reset. Bloom decay. |
| **Setup** | `SetupWeaponSystems()` |

### 14. WeaponReloadSystem

| Property | Value |
|----------|-------|
| **Queries** | `FWeaponInstance`, with `bReloading == true` |
| **Does** | Reload timer countdown. Ammo transfer (reserve → magazine). UI notification. |
| **Setup** | `SetupWeaponSystems()` |

### 15. WeaponFireSystem

| Property | Value |
|----------|-------|
| **Queries** | `FWeaponStatic`, `FWeaponInstance`, `FAimDirection` |
| **Does** | Aim raycast. Bloom spread. Barrage body creation. Flecs entity creation (inline). Enqueue spawn + shot events. |
| **Why after reload** | Reload must complete before fire system checks ammo. |
| **Setup** | `SetupWeaponSystems()` |

### 16. DeathCheckSystem

| Property | Value |
|----------|-------|
| **Queries** | `FHealthInstance`, without `FTagDead` |
| **Does** | Adds `FTagDead` if `CurrentHP ≤ 0`. |
| **Why here** | All damage sources (collision systems, observers) have processed by this point. |

### 17. DeadEntityCleanupSystem

| Property | Value |
|----------|-------|
| **Queries** | `FTagDead` |
| **Does** | Tombstone body. Cleanup constraints. Remove ISM. Trigger death VFX. Release to pool. `entity.destruct()`. |
| **Why second-to-last** | Must process after all systems that may add `FTagDead`. |

### 18. CollisionPairCleanupSystem

| Property | Value |
|----------|-------|
| **Queries** | `FCollisionPair` |
| **Does** | `entity.destruct()` on every collision pair. |
| **Why LAST** | Must run after ALL collision processing systems. No collision pair may survive to the next tick. |

---

## DamageObserver (Reactive)

| Property | Value |
|----------|-------|
| **Event** | `flecs::OnSet` on `FPendingDamage` |
| **Does** | Applies all `FDamageHit` entries to `FHealthInstance.CurrentHP`. Removes `FPendingDamage`. |
| **When fires** | Immediately when `modified<FPendingDamage>()` is called (typically during `DamageCollisionSystem`). |
| **Not in order** | Observer fires during the calling system, not at a scheduled slot. |

---

## Setup Methods

Systems are grouped by domain. Each domain has a setup method called from `SetupFlecsSystems()`:

```cpp
void SetupFlecsSystems()
{
    RegisterFlecsComponents();          // All ~50 components

    SetupDamageObserver();              // Reactive
    SetupLifetimeSystems();             // WorldItemDespawn, PickupGrace, ProjectileLifetime, DebrisLifetime
    SetupDamageCollisionSystems();      // DamageCollision, BounceCollision
    SetupPickupCollisionSystems();      // PickupCollision
    SetupDestructibleCollisionSystems();// Destructible, ConstraintBreak, Fragmentation
    SetupDoorSystems();                 // TriggerUnlock, DoorTick
    SetupWeaponSystems();              // WeaponTick, WeaponReload, WeaponFire
    SetupDeathSystems();               // DeathCheck, DeadEntityCleanup
    SetupCleanupSystems();             // CollisionPairCleanup (ALWAYS LAST)
}
```

---

## Ordering Constraints

| Rule | Reason |
|------|--------|
| Lifetime systems before collision systems | Expired entities should be dead before collision processing |
| PickupGrace before PickupCollision | Grace timer must be checked before allowing pickup |
| ConstraintBreak before Fragmentation | Existing breaks must be processed before new fragments are created |
| TriggerUnlock before DoorTick | Door must know it's unlocked before state machine ticks |
| WeaponReload before WeaponFire | Reload must complete before fire checks ammo |
| All damage sources before DeathCheck | All hits in a tick must be applied before checking for death |
| DeadEntityCleanup before CollisionPairCleanup | Dead entities must be cleaned up while collision data still exists |
| CollisionPairCleanup LAST always | All systems must finish processing pairs first |
