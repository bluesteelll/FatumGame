# Компоненты ECS

> Полный справочник всех компонентов Flecs, организованных по доменам. Для каждого компонента указаны поля, размещение на prefab или экземпляре и заголовочный файл.

---

## Ядро

**Заголовок:** `Core/Public/Components/FlecsHealthComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FHealthStatic` | Prefab | `MaxHP`, `Armor`, `RegenPerSecond`, `RegenDelay`, `InvulnerabilityTime`, `bDestroyOnDeath` |
| `FDamageStatic` | Prefab | `Damage`, `DamageType`, `CriticalMultiplier`, `CriticalChance`, `bAreaDamage`, `AreaRadius`, `AreaFalloff`, `bDestroyOnHit` |
| `FHealthInstance` | Экземпляр | `CurrentHP`, `RegenAccumulator` |
| `FDamageHit` | Временный | `Damage`, `DamageType`, `bIgnoreArmor` |
| `FPendingDamage` | Временный | `TArray<FDamageHit> Hits` |

**Заголовок:** `Core/Public/Components/FlecsEntityComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FEntityDefinitionRef` | Экземпляр | `EntityDefinition*` |
| `FFocusCameraOverride` | Экземпляр | Локальная позиция/поворот камеры для фокусного взаимодействия |
| `FLootStatic` | Prefab | `MinDrops`, `MaxDrops`, `DropChance` |

**Заголовок:** `Core/Public/Components/FlecsInteractionComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FInteractionStatic` | Prefab | `MaxRange`, `bSingleUse`, `InteractionType`, `AngleCosine` |
| `FInteractionInstance` | Экземпляр | `bToggleState`, `UseCount` |
| `FInteractionAngleOverride` | Экземпляр | Пользовательское ограничение угла |

---

## Оружие

**Заголовок:** `Weapon/Public/Components/FlecsWeaponComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FAimDirection` | Экземпляр | `AimWorldDirection`, `AimWorldOrigin`, `MuzzleWorldPosition` |
| `FWeaponStatic` | Prefab | Все данные стрельбы, боеприпасов, разброса, отдачи, дульного среза, визуала, ADS, движения оружия |
| `FWeaponInstance` | Экземпляр | `CurrentMag`, `CurrentReserve`, `FireCooldown`, `bReloading`, `ReloadTimer`, `CurrentBloom`, флаги ввода |
| `FEquippedBy` | Экземпляр | `OwnerEntityId`, `SlotId` |

**Заголовок:** `Weapon/Public/Components/FlecsProjectileComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FProjectileStatic` | Prefab | `MaxLifetime`, `MaxBounces`, `GracePeriodFrames`, `MinVelocity`, `bOrientToVelocity` |
| `FProjectileInstance` | Экземпляр | `LifetimeRemaining`, `BounceCount`, `GraceFramesRemaining` |

---

## Предметы и контейнеры

**Заголовок:** `Item/Public/Components/FlecsItemComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FItemStaticData` | Prefab | `TypeId`, `MaxStack`, `Weight`, `GridSize`, `EntityDefinition*`, `ItemDefinition*` |
| `FContainerStatic` | Prefab | `Type`, `GridWidth`, `GridHeight`, `MaxItems`, `MaxWeight` |
| `FItemInstance` | Экземпляр | `Count` |
| `FItemUniqueData` | Экземпляр | `Durability`, `Enchantments`, `CustomStats` |
| `FContainerInstance` | Экземпляр | `CurrentWeight`, `CurrentCount`, `OwnerEntityId` |
| `FContainerGridInstance` | Экземпляр | `OccupancyMask`, `Width`, `Height` |
| `FContainerSlotsInstance` | Экземпляр | Карта `SlotToItemEntity` |
| `FWorldItemInstance` | Экземпляр | `DespawnTimer`, `PickupGraceTimer`, `DroppedByEntityId` |
| `FContainedIn` | Экземпляр | `ContainerEntityId`, `GridPosition`, `SlotIndex` |

---

## Передвижение

**Заголовок:** `Movement/Public/Components/FlecsMovementComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FMovementStatic` | Prefab | Все скорости, ускорения, прыжок, гравитация, капсула, поза, скольжение, карабканье, уступ |
| `FCharacterMoveState` | Экземпляр | Текущая поза, состояние движения, скорость |

---

## Способности

**Заголовок:** `Abilities/Public/Components/FlecsAbilityTypes.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FAbilitySystem` | Экземпляр | `ActiveMask`, `SlotCount`, `Slots[8]` |
| `FAbilitySlot` | (вложенный) | `TypeId`, `Phase`, `PhaseTimer`, `Charges`, `RechargeTimer`, `CooldownTimer`, `ConfigData[32]` |

**Заголовок:** `Abilities/Public/Components/FlecsResourceTypes.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FResourcePool` | Экземпляр | `ResourceType`, `CurrentValue`, `MaxValue`, `RegenRate`, `RegenDelay` |

---

## Разрушаемые объекты

**Заголовок:** `Destructible/Public/Components/FlecsDestructibleComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FDestructibleStatic` | Prefab | `Profile*`, силы констрейнтов, настройки якоря |
| `FDebrisInstance` | Экземпляр | `LifetimeRemaining`, `PoolSlotIndex`, `FreeMassKg`, `PendingImpulse` |
| `FFragmentationData` | Пара столкновений | `ImpactPoint`, `ImpactDirection`, `ImpactImpulse` |

---

## Двери

**Заголовок:** `Door/Public/Components/FlecsDoorComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FDoorStatic` | Prefab | `HingeOffset`, `OpenAngle`, `CloseAngle`, `AngularDamping`, `bAutoClose` |
| `FDoorInstance` | Экземпляр | `CurrentAngle`, `AngularVelocity`, `DoorState`, `AutoCloseTimer` |

---

## Лазание

**Заголовок:** `Climbing/Public/Components/FlecsClimbableComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FClimbableStatic` | Prefab | `ClimbDirection`, `ClimbSpeed`, `TopExitVelocity` |
| `FClimbInstance` | Экземпляр | Фаза, таймер |

**Заголовок:** `Climbing/Public/Components/FlecsSwingableComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FSwingableStatic` | Prefab | `AttachRadius`, `SwingForce` |
| `FRopeSwingInstance` | Экземпляр | Прикреплённая сущность, ключ констрейнта |

---

## Стелс

**Заголовок:** `Stealth/Public/Components/FlecsStealthComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FStealthLightStatic` | Prefab | `Type`, `Intensity`, `Radius`, углы конуса |
| `FNoiseZoneStatic` | Prefab | `Extent`, `SurfaceType` |
| `FStealthInstance` | Экземпляр | `LightLevel`, `NoiseLevel`, `Detectability`, `PendingNoise` |

---

## Привязка (плагин FlecsBarrage)

**Заголовок:** `Plugins/FlecsBarrage/.../FlecsBarrageComponents.h`

| Компонент | Уровень | Поля |
|-----------|---------|------|
| `FBarrageBody` | Экземпляр | `SkeletonKey` (прямая привязка к физическому телу) |
| `FISMRender` | Экземпляр | `Mesh`, `Material`, `Scale` |
| `FCollisionPair` | Временный | `EntityA`, `EntityB` (uint64 Flecs ID) |

---

## Теги

**Заголовок:** `Core/Public/FlecsGameTags.h`

Все теги -- структуры нулевого размера (`sizeof == 0`):

| Тег | Назначение |
|-----|-----------|
| `FTagProjectile` | Сущность-снаряд |
| `FTagCharacter` | Сущность-персонаж |
| `FTagItem` | Сущность-предмет |
| `FTagDroppedItem` | Выброшенный игроком предмет |
| `FTagContainer` | Сущность-контейнер |
| `FTagPickupable` | Можно подобрать |
| `FTagInteractable` | Поддерживает взаимодействие |
| `FTagDestructible` | Можно повредить/уничтожить |
| `FTagDead` | Помечен для очистки |
| `FTagHasLoot` | Содержит лут |
| `FTagEquipment` | Экипируемый предмет |
| `FTagConsumable` | Расходуемый предмет |
| `FTagDebrisFragment` | Фрагмент обломков (возврат в пул) |
| `FTagWeapon` | Сущность-оружие |
| `FTagDoor` | Сущность-дверь |
| `FTagDoorTrigger` | Триггер двери |
| `FTagTelekinesisHeld` | Удерживается телекинезом |
| `FTagStealthLight` | Источник стелс-света |
| `FTagNoiseZone` | Зона шума |

### Теги столкновений (плагин FlecsBarrage)

| Тег | Направляется в |
|-----|---------------|
| `FTagCollisionDamage` | DamageCollisionSystem |
| `FTagCollisionBounce` | BounceCollisionSystem |
| `FTagCollisionPickup` | PickupCollisionSystem |
| `FTagCollisionDestructible` | DestructibleCollisionSystem |
| `FTagCollisionFragmentation` | FragmentationSystem |
