# ECS Components

> Complete reference of all Flecs components organized by domain. Each component lists its fields, whether it lives on the prefab or instance, and which header defines it.

---

## Core

**Header:** `Core/Public/Components/FlecsHealthComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FHealthStatic` | Prefab | `MaxHP`, `Armor`, `RegenPerSecond`, `RegenDelay`, `InvulnerabilityTime`, `bDestroyOnDeath` |
| `FDamageStatic` | Prefab | `Damage`, `DamageType`, `CriticalMultiplier`, `CriticalChance`, `bAreaDamage`, `AreaRadius`, `AreaFalloff`, `bDestroyOnHit` |
| `FHealthInstance` | Instance | `CurrentHP`, `RegenAccumulator` |
| `FDamageHit` | Transient | `Damage`, `DamageType`, `bIgnoreArmor` |
| `FPendingDamage` | Transient | `TArray<FDamageHit> Hits` |

**Header:** `Core/Public/Components/FlecsEntityComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FEntityDefinitionRef` | Instance | `EntityDefinition*` |
| `FFocusCameraOverride` | Instance | Local camera position/rotation for focus interaction |
| `FLootStatic` | Prefab | `MinDrops`, `MaxDrops`, `DropChance` |

**Header:** `Core/Public/Components/FlecsInteractionComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FInteractionStatic` | Prefab | `MaxRange`, `bSingleUse`, `InteractionType`, `AngleCosine` |
| `FInteractionInstance` | Instance | `bToggleState`, `UseCount` |
| `FInteractionAngleOverride` | Instance | Custom angle restriction |

---

## Weapon

**Header:** `Weapon/Public/Components/FlecsWeaponComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FAimDirection` | Instance | `Direction`, `CharacterPosition`, `MuzzleWorldPosition` |
| `FWeaponStatic` | Prefab | Firing (ProjectileDefinition, FireInterval, BurstCount, ProjectilesPerShot, bIsAutomatic, bIsBurst), Ammo (AcceptedCaliberIds, AmmoPerShot, bHasChamber, bUnlimitedAmmo, RemoveMagTime, InsertMagTime, ChamberTime), ReloadType (ReloadType, OpenTime, InsertRoundTime, CloseTime), Quick-Load (AcceptedDeviceTypes, bDisableQuickLoadDevices, OpenTimeDevice, CloseTimeDevice), Cycling (bRequiresCycling, CycleTime), Trigger Pull (bEnableTriggerPull, TriggerPullTime, bTriggerPullEveryShot), Bloom (BaseSpread, SpreadPerShot, MaxBloom, BloomDecayRate, BaseSpreadMultipliers[], BloomMultipliers[]), Pellet Rings (PelletRingCount, PelletRings[4]), Muzzle, Visual, Animation data |
| `FWeaponInstance` | Instance | Magazine (InsertedMagazineId), Firing (FireCooldownRemaining, BurstShotsRemaining, BurstCooldownRemaining, bHasFiredSincePress), Reload (ReloadPhase, ReloadPhaseTimer, SelectedMagazineId, bPrevMagWasEmpty, bChambered, ChamberedAmmoTypeIdx, RoundsInsertedThisReload), Quick-Load (ActiveLoadMethod, ActiveDeviceEntityId, BatchSize, BatchInsertTime, DeviceAmmoTypeIdx, bUsedDeviceThisReload), Bloom (CurrentBloom, TimeSinceLastShot), Trigger (TriggerPullTimer, bTriggerPulling), Cycling (bNeedsCycle, bCycling, CycleTimeRemaining), ShotsFiredTotal, input flags |
| `FEquippedBy` | Instance | `CharacterEntityId`, `SlotId` |
| `FPelletRingData` | (nested) | `PelletCount`, `RadiusRadians`, `AngularJitterRadians`, `RadialJitterRadians` |
| `FWeaponSlotState` | Instance (character) | `ActiveSlotIndex`, `PendingSlotIndex`, `EquipPhase`, `EquipTimer`, `WeaponSlotContainerId` |

**Header:** `Weapon/Public/Components/FlecsProjectileComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FProjectileStatic` | Prefab | `MaxLifetime`, `MaxBounces`, `GracePeriodFrames`, `MinVelocity`, `bOrientToVelocity`, `FuseTime` |
| `FProjectileInstance` | Instance | `LifetimeRemaining`, `BounceCount`, `GraceFramesRemaining`, `FuseRemaining` |

**Header:** `Weapon/Public/Components/FlecsExplosionComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FExplosionStatic` | Prefab | `Radius`, `BaseDamage`, `ImpulseStrength`, `DamageFalloff`, `ImpulseFalloff`, `VerticalBias`, `EpicenterLift`, `bDamageOwner`, `ExplosionEffect`, `ExplosionEffectScale`, `DamageType` |
| `FExplosionContactData` | Transient | `ContactNormal` (stored at collision time for epicenter lift) |

---

## Item & Container

**Header:** `Item/Public/Components/FlecsItemComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FItemStaticData` | Prefab | `TypeId`, `MaxStack`, `Weight`, `GridSize`, `EntityDefinition*`, `ItemDefinition*` |
| `FContainerStatic` | Prefab | `Type`, `GridWidth`, `GridHeight`, `MaxItems`, `MaxWeight` |
| `FItemInstance` | Instance | `Count` |
| `FItemUniqueData` | Instance | `Durability`, `Enchantments`, `CustomStats` |
| `FContainerInstance` | Instance | `CurrentWeight`, `CurrentCount`, `OwnerEntityId` |
| `FContainerGridInstance` | Instance | `OccupancyMask`, `Width`, `Height` |
| `FContainerSlotsInstance` | Instance | `SlotToItemEntity` map |
| `FWorldItemInstance` | Instance | `DespawnTimer`, `PickupGraceTimer`, `DroppedByEntityId` |
| `FContainedIn` | Instance | `ContainerEntityId`, `GridPosition`, `SlotIndex` |
| `FMagazineStatic` | Prefab | `CaliberId`, `Capacity`, `WeightPerRound`, `ReloadSpeedModifier`, `AcceptedAmmoTypes[8]`, `AcceptedAmmoTypeCount` |
| `FMagazineInstance` | Instance | `AmmoSlots[60]` (LIFO uint8 ammo type indices), `AmmoCount` |
| `FAmmoTypeRef` | Instance | `AmmoTypeIndex` (index into `FMagazineStatic::AcceptedAmmoTypes`) |
| `FQuickLoadStatic` | Prefab | `DeviceType` (EQuickLoadDeviceType), `RoundsHeld`, `CaliberId`, `AmmoTypeDefinition*`, `InsertTime`, `bRequiresEmptyMagazine` |

---

## Movement

**Header:** `Movement/Public/Components/FlecsMovementComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FMovementStatic` | Prefab | All speeds, accelerations, jump, gravity, capsule, posture, slide, mantle, ledge settings |
| `FCharacterMoveState` | Instance | Current posture, movement state, velocity |

---

## Abilities

**Header:** `Abilities/Public/Components/FlecsAbilityTypes.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FAbilitySystem` | Instance | `ActiveMask`, `SlotCount`, `Slots[8]` |
| `FAbilitySlot` | (nested) | `TypeId`, `Phase`, `PhaseTimer`, `Charges`, `RechargeTimer`, `CooldownTimer`, `ConfigData[32]` |

**Header:** `Abilities/Public/Components/FlecsResourceTypes.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FResourcePool` | Instance | `ResourceType`, `CurrentValue`, `MaxValue`, `RegenRate`, `RegenDelay` |

---

## Destructible

**Header:** `Destructible/Public/Components/FlecsDestructibleComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FDestructibleStatic` | Prefab | `Profile*`, constraint forces, anchor settings |
| `FDebrisInstance` | Instance | `LifetimeRemaining`, `PoolSlotIndex`, `FreeMassKg`, `PendingImpulse` |
| `FFragmentationData` | Collision pair | `ImpactPoint`, `ImpactDirection`, `ImpactImpulse` |
| `FPendingFragmentation` | Transient | `ImpactPoint`, `ImpactDirection`, `ImpactImpulse` (set by ApplyExplosion, processed by PendingFragmentationSystem) |

---

## Door

**Header:** `Door/Public/Components/FlecsDoorComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FDoorStatic` | Prefab | `HingeOffset`, `OpenAngle`, `CloseAngle`, `AngularDamping`, `bAutoClose` |
| `FDoorInstance` | Instance | `CurrentAngle`, `AngularVelocity`, `DoorState`, `AutoCloseTimer` |

---

## Climbing

**Header:** `Climbing/Public/Components/FlecsClimbableComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FClimbableStatic` | Prefab | `ClimbDirection`, `ClimbSpeed`, `TopExitVelocity` |
| `FClimbInstance` | Instance | Phase, timer |

**Header:** `Climbing/Public/Components/FlecsSwingableComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FSwingableStatic` | Prefab | `AttachRadius`, `SwingForce` |
| `FRopeSwingInstance` | Instance | Attached entity, constraint key |

---

## Stealth

**Header:** `Stealth/Public/Components/FlecsStealthComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FStealthLightStatic` | Prefab | `Type`, `Intensity`, `Radius`, cone angles |
| `FNoiseZoneStatic` | Prefab | `Extent`, `SurfaceType` |
| `FStealthInstance` | Instance | `LightLevel`, `NoiseLevel`, `Detectability`, `PendingNoise` |

---

## Binding (Plugin: FlecsBarrage)

**Header:** `Plugins/FlecsBarrage/.../FlecsBarrageComponents.h`

| Component | Level | Fields |
|-----------|-------|--------|
| `FBarrageBody` | Instance | `SkeletonKey` (forward binding to physics body) |
| `FISMRender` | Instance | `Mesh`, `Material`, `Scale` |
| `FCollisionPair` | Transient | `EntityA`, `EntityB` (uint64 Flecs IDs) |

---

## Tags

**Header:** `Core/Public/FlecsGameTags.h`

All tags are zero-size structs (`sizeof == 0`):

| Tag | Purpose |
|-----|---------|
| `FTagProjectile` | Projectile entity |
| `FTagCharacter` | Character entity |
| `FTagItem` | Item entity |
| `FTagDroppedItem` | Player-dropped item |
| `FTagContainer` | Container entity |
| `FTagPickupable` | Can be picked up |
| `FTagInteractable` | Supports interaction |
| `FTagDestructible` | Can be damaged/destroyed |
| `FTagDead` | Marked for cleanup |
| `FTagHasLoot` | Has loot drops |
| `FTagEquipment` | Equippable item |
| `FTagConsumable` | Consumable item |
| `FTagDebrisFragment` | Debris fragment (pool return) |
| `FTagWeapon` | Weapon entity |
| `FTagReloading` | Weapon is currently reloading (query optimization) |
| `FTagWeaponSlot` | Weapon slot container |
| `FTagMagazine` | Magazine entity |
| `FTagQuickLoadDevice` | Quick-load device entity (stripper clip / speedloader) |
| `FTagDetonate` | Projectile marked for detonation this tick (ExplosionSystem) |
| `FTagDoor` | Door entity |
| `FTagDoorTrigger` | Door trigger |
| `FTagTelekinesisHeld` | Held by telekinesis |
| `FTagStealthLight` | Stealth light source |
| `FTagNoiseZone` | Noise zone |

### Collision Tags (FlecsBarrage plugin)

| Tag | Routed To |
|-----|-----------|
| `FTagCollisionDamage` | DamageCollisionSystem |
| `FTagCollisionBounce` | BounceCollisionSystem |
| `FTagCollisionPickup` | PickupCollisionSystem |
| `FTagCollisionDestructible` | DestructibleCollisionSystem |
| `FTagCollisionFragmentation` | FragmentationSystem |
