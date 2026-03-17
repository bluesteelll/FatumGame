# Blueprint Libraries

> All Blueprint-callable functions for interacting with the ECS simulation from Blueprints or game-thread C++. Every function uses `EnqueueCommand` internally for thread safety — they can be called from any game-thread context.

---

## UFlecsEntityLibrary

**Header:** `Spawning/Public/FlecsEntitySpawner.h`

Entity lifecycle management.

| Function | Returns | Description |
|----------|---------|-------------|
| `SpawnEntity(WorldContext, Request)` | `FSkeletonKey` | Spawn entity from `FEntitySpawnRequest` |
| `SpawnEntityFromDefinition(WorldContext, Def, Location, Rotation)` | `FSkeletonKey` | Convenience: spawn from definition + transform |
| `SpawnEntities(WorldContext, Requests)` | `TArray<FSkeletonKey>` | Batch spawn |
| `DestroyEntity(WorldContext, EntityKey)` | `void` | Add `FTagDead` → cleanup next tick |
| `DestroyEntities(WorldContext, EntityKeys)` | `void` | Batch destroy |
| `IsEntityAlive(WorldContext, EntityKey)` | `bool` | Check if entity exists and is not dead |
| `GetEntityId(WorldContext, EntityKey)` | `int64` | Get Flecs entity ID from SkeletonKey |

---

## UFlecsDamageLibrary

**Header:** `Weapon/Public/Library/FlecsDamageLibrary.h`

Damage, healing, and health queries.

| Function | Returns | Description |
|----------|---------|-------------|
| `ApplyDamageByBarrageKey(WorldContext, BarrageKey, Damage)` | `void` | Queue damage hit |
| `ApplyDamageWithType(WorldContext, BarrageKey, Damage, DamageType, bIgnoreArmor)` | `void` | Queue typed damage |
| `HealEntityByBarrageKey(WorldContext, BarrageKey, Amount)` | `void` | Add HP (clamped to max) |
| `KillEntityByBarrageKey(WorldContext, BarrageKey)` | `void` | Instant kill (bypasses damage, adds `FTagDead`) |
| `GetEntityHealth(WorldContext, BarrageKey)` | `float` | Current HP (reads `FSimStateCache`) |
| `GetEntityMaxHealth(WorldContext, BarrageKey)` | `float` | Max HP |
| `IsEntityAlive(WorldContext, BarrageKey)` | `bool` | HP > 0 and not dead |

---

## UFlecsWeaponLibrary

**Header:** `Weapon/Public/Library/FlecsWeaponLibrary.h`

Weapon control and queries.

| Function | Returns | Description |
|----------|---------|-------------|
| `StartFiring(WorldContext, WeaponEntityId)` | `void` | Set `bFireInputActive = true` |
| `StopFiring(WorldContext, WeaponEntityId)` | `void` | Set `bFireInputActive = false` |
| `ReloadWeapon(WorldContext, WeaponEntityId)` | `void` | Begin reload |
| `SetAimDirection(WorldContext, CharacterId, Direction, Position)` | `void` | Update aim (prefer FLateSyncBridge) |
| `GetWeaponAmmo(WorldContext, WeaponEntityId)` | `int32` | Current magazine ammo |
| `GetWeaponAmmoInfo(WorldContext, WeaponEntityId, OutCurrent, OutMag, OutReserve)` | `bool` | Full ammo state |
| `IsWeaponReloading(WorldContext, WeaponEntityId)` | `bool` | Currently reloading |

---

## UFlecsContainerLibrary

**Header:** `Item/Public/Library/FlecsContainerLibrary.h`

Item and container operations.

| Function | Returns | Description |
|----------|---------|-------------|
| `AddItemToContainer(WorldContext, ContainerId, EntityDef, Count, OutAdded, bAutoStack)` | `bool` | Add items to container |
| `RemoveItemFromContainer(WorldContext, ContainerId, ItemEntityId, Count)` | `bool` | Remove items |
| `RemoveAllItemsFromContainer(WorldContext, ContainerId)` | `int32` | Remove all, returns count |
| `TransferItem(WorldContext, SourceId, DestId, ItemEntityId, DestGridPos)` | `bool` | Move between containers |
| `PickupItem(WorldContext, WorldItemKey, ContainerId, OutPickedUp)` | `bool` | Pick up world item |
| `DropItem(WorldContext, ContainerId, ItemEntityId, DropLocation, Count)` | `FSkeletonKey` | Drop item into world |
| `GetContainerItemCount(WorldContext, ContainerId)` | `int32` | Current item count |
| `SetItemDespawnTimer(WorldContext, BarrageKey, Timer)` | `void` | Set world item despawn time |

---

## UFlecsInteractionLibrary

**Header:** `Interaction/Public/Library/FlecsInteractionLibrary.h`

Interaction execution and queries.

| Function | Returns | Description |
|----------|---------|-------------|
| `ExecuteInteraction(WorldContext, Action, TargetKey, InventoryId, EventTag)` | `void` | Execute instant action on target |
| `ApplySingleUseIfNeeded(WorldContext, TargetKey)` | `void` | Remove `FTagInteractable` if single-use |
| `GetToggleState(WorldContext, TargetKey)` | `bool` | Read toggle state |

C++ only (not BlueprintCallable):

| Function | Description |
|----------|-------------|
| `DispatchInstantAction(...)` | Full variant with callback |
| `DispatchContainerInteraction(...)` | With `FOnContainerOpened` delegate |

---

## UFlecsSpawnLibrary

**Header:** `Spawning/Public/Library/FlecsSpawnLibrary.h`

Specialized spawn helpers.

| Function | Returns | Description |
|----------|---------|-------------|
| `SpawnProjectileFromEntityDef(WorldContext, Def, Location, Direction, Speed, OwnerId)` | `FSkeletonKey` | Spawn projectile |
| `SpawnConstrainedGroup(WorldContext, Def, Location, Rotation)` | `FFlecsGroupSpawnResult` | Spawn constraint-linked group |
| `SpawnChain(WorldContext, Mesh, Start, Direction, Count, Spacing, BreakForce, MaxHP)` | `FFlecsGroupSpawnResult` | Spawn physics chain |

!!! note "Deprecated Functions"
    `SpawnWorldItem()`, `SpawnDestructible()`, `SpawnLootableDestructible()` are deprecated. Use `UFlecsEntityLibrary::SpawnEntity()` with `FEntitySpawnRequest` instead.

---

## UFlecsConstraintLibrary

**Header:** `Destructible/Public/Library/FlecsConstraintLibrary.h`

Physics constraint creation and management.

| Function | Description |
|----------|-------------|
| Create fixed constraints | Link two bodies rigidly with break force/torque |
| Create hinge constraints | Rotational joint with angle limits and motor |
| Create distance constraints | Spring/damper connection |
| Break constraint | Force-break a specific constraint |

---

## AFlecsCharacter (BlueprintCallable)

Key functions exposed from the character actor:

### Health
| Function | Returns |
|----------|---------|
| `GetCurrentHealth()` | `float` |
| `GetHealthPercent()` | `float` |
| `IsAlive()` | `bool` |
| `ApplyDamage(Damage)` | `void` |
| `Heal(Amount)` | `void` |

### Weapon
| Function | Returns |
|----------|---------|
| `SpawnAndEquipTestWeapon()` | `void` |
| `StartFiringWeapon()` | `void` |
| `StopFiringWeapon()` | `void` |
| `ReloadTestWeapon()` | `void` |
| `IsAimingDownSights()` | `bool` |
| `GetADSAlpha()` | `float` |

### Inventory
| Function | Returns |
|----------|---------|
| `GetInventoryEntityId()` | `uint64` |
| `GetWeaponInventoryEntityId()` | `uint64` |
| `IsInventoryOpen()` | `bool` |
| `IsLootOpen()` | `bool` |

### Interaction
| Function | Returns |
|----------|---------|
| `GetInteractionTarget()` | `FSkeletonKey` |
| `HasInteractionTarget()` | `bool` |
| `GetInteractionPrompt()` | `FText` |
| `IsInInteraction()` | `bool` |

### Identity
| Function | Returns |
|----------|---------|
| `GetEntityKey()` | `FSkeletonKey` |
| `GetCharacterEntityId()` | `uint64` |

### Blueprint Events
| Event | When Fired |
|-------|-----------|
| `OnDamageTaken(Damage)` | Character takes damage |
| `OnDeath()` | Character dies |
| `OnHealed(Amount)` | Character healed |
| `OnInteractionTargetChanged(NewTarget)` | Target gained/lost |
| `OnInteractionStateChanged(NewState)` | State machine transition |
| `OnHoldProgressChanged(Progress)` | Hold interaction progress |
