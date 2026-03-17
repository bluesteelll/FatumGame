# Data Assets и профили

> Все типы сущностей настраиваются через data assets `UFlecsEntityDefinition` в редакторе. Каждое определение ссылается на профили, управляющие конкретными аспектами (физика, здоровье, рендеринг и т.д.). На этой странице перечислены все профили и их поля.

---

## UFlecsEntityDefinition

Мастер data asset. Создаётся в Content Browser -> Data Asset -> `FlecsEntityDefinition`.

### Ссылки на профили

| Свойство | Тип | Назначение |
|----------|-----|-----------|
| `ItemDefinition` | `UFlecsItemDefinition*` | Метаданные предмета (название, иконка, стак) |
| `PhysicsProfile` | `UFlecsPhysicsProfile*` | Коллизия, масса, гравитация |
| `RenderProfile` | `UFlecsRenderProfile*` | Меш, материал, масштаб |
| `HealthProfile` | `UFlecsHealthProfile*` | HP, броня, реген |
| `DamageProfile` | `UFlecsDamageProfile*` | Урон, тип, площадь |
| `ProjectileProfile` | `UFlecsProjectileProfile*` | Скорость, время жизни, отскоки |
| `WeaponProfile` | `UFlecsWeaponProfile*` | Скорострельность, боезапас, отдача |
| `ContainerProfile` | `UFlecsContainerProfile*` | Сетка, слоты, вес |
| `InteractionProfile` | `UFlecsInteractionProfile*` | Дальность, тип, подсказка |
| `NiagaraProfile` | `UFlecsNiagaraProfile*` | Прикреплённые VFX / VFX смерти |
| `DestructibleProfile` | `UFlecsDestructibleProfile*` | Фрагментация, констрейнты |
| `DoorProfile` | `UFlecsDoorProfile*` | Шарнир, мотор, автозакрытие |
| `MovementProfile` | `UFlecsMovementProfile*` | Скорости, прыжок, способности |
| `AbilityLoadout` | `UFlecsAbilityLoadout*` | Назначение слотов способностей |
| `ResourcePoolProfile` | `UFlecsResourcePoolProfile*` | Мана, стамина и т.д. |
| `ClimbProfile` | `UFlecsClimbProfile*` | Скорость лазания, скорость выхода |
| `RopeSwingProfile` | `UFlecsRopeSwingProfile*` | Сила раскачивания, радиус |
| `StealthLightProfile` | `UFlecsStealthLightProfile*` | Тип света, интенсивность |
| `NoiseZoneProfile` | `UFlecsNoiseZoneProfile*` | Зона поверхностного шума |

### Флаги тегов

| Свойство | Тип | Устанавливает тег |
|----------|-----|-------------------|
| `bPickupable` | `bool` | `FTagPickupable` |
| `bDestructible` | `bool` | `FTagDestructible` |
| `bHasLoot` | `bool` | `FTagHasLoot` |
| `bIsCharacter` | `bool` | `FTagCharacter` |

---

## Детали профилей

### UFlecsPhysicsProfile

| Поле | Тип | По умолчанию | Описание |
|------|-----|-------------|----------|
| `CollisionRadius` | `float` | 10.0 | Радиус сферы/капсулы (см) |
| `CollisionHalfHeight` | `float` | 0.0 | Полувысота капсулы (0 = сфера) |
| `Layer` | `EFlecsPhysicsLayer` | MOVING | Слой объекта Jolt |
| `Mass` | `float` | 1.0 | Масса тела (кг) |
| `Restitution` | `float` | 0.3 | Упругость |
| `Friction` | `float` | 0.5 | Трение поверхности |
| `LinearDamping` | `float` | 0.0 | Демпфирование линейной скорости |
| `AngularDamping` | `float` | 0.0 | Демпфирование угловой скорости |
| `GravityFactor` | `float` | 1.0 | Множитель гравитации (0 = без гравитации) |
| `bIsSensor` | `bool` | false | Только триггер, без физической реакции |
| `bIsKinematic` | `bool` | false | Кинематическое тело (управляется скриптом) |
| `bUseCCD` | `bool` | false | Непрерывное обнаружение столкновений |

### UFlecsRenderProfile

| Поле | Тип | Описание |
|------|-----|----------|
| `Mesh` | `UStaticMesh*` | Визуальный меш |
| `MaterialOverride` | `UMaterialInterface*` | Материал (null = по умолчанию меша) |
| `Scale` | `FVector` | Масштаб меша |
| `RotationOffset` | `FRotator` | Смещение поворота меша |
| `bCastShadow` | `bool` | Отбрасывать тени |
| `bVisible` | `bool` | Изначально видим |
| `CustomDepthStencilValue` | `int32` | Custom depth для эффектов контура |

### UFlecsHealthProfile

| Поле | Тип | По умолчанию | Описание |
|------|-----|-------------|----------|
| `MaxHealth` | `float` | 100 | Максимальное HP |
| `StartingHealth` | `float` | 0 | Начальное HP (0 = MaxHealth) |
| `Armor` | `float` | 0.0 | Уменьшение урона [0, 1] |
| `RegenPerSecond` | `float` | 0.0 | Скорость регенерации HP |
| `RegenDelay` | `float` | 0.0 | Задержка перед регеном после урона |
| `InvulnerabilityTime` | `float` | 0.0 | Кадры неуязвимости после урона |
| `bDestroyOnDeath` | `bool` | true | Автоуничтожение при смерти |

### UFlecsDamageProfile

| Поле | Тип | Описание |
|------|-----|----------|
| `Damage` | `float` | Базовый урон за попадание |
| `DamageType` | `FGameplayTag` | Классификация урона |
| `CriticalMultiplier` | `float` | Множитель крита |
| `CriticalChance` | `float` | Шанс [0, 1] |
| `bAreaDamage` | `bool` | Наносить в радиусе |
| `AreaRadius` | `float` | Радиус области (см) |
| `AreaFalloff` | `float` | Экспонента затухания |
| `bDestroyOnHit` | `bool` | Уничтожить источник при контакте |
| `bCanHitSameTargetMultipleTimes` | `bool` | Многократные попадания |
| `MultiHitCooldown` | `float` | Кулдаун между многократными попаданиями |

### UFlecsProjectileProfile

| Поле | Тип | Описание |
|------|-----|----------|
| `DefaultSpeed` | `float` | Скорость снаряда (см/с) |
| `bMaintainSpeed` | `bool` | Поддерживать постоянную скорость |
| `Lifetime` | `float` | Максимальное время жизни (секунды) |
| `MaxBounces` | `int32` | Максимум отскоков от стен (0 = уничтожение при столкновении) |
| `MinVelocity` | `float` | Уничтожить, если скорость упадёт ниже |
| `GracePeriod` | `float` | Кадры до активации проверки скорости |
| `bOrientToVelocity` | `bool` | Поворачивать меш по направлению движения |
| `bPenetrating` | `bool` | Проходить сквозь цели |
| `MaxPenetrations` | `int32` | Максимум целей для пробития |

### UFlecsWeaponProfile

Большой профиль -- ключевые поля:

| Группа | Поля |
|--------|------|
| **Стрельба** | FireMode, FireRate, BurstCount, ProjectilesPerShot, AmmoPerShot |
| **Боеприпасы** | MagazineSize, MaxReserveAmmo, ReloadTime |
| **Снаряд** | ProjectileDefinition, ProjectileSpeedMultiplier, DamageMultiplier |
| **Дульный срез** | MuzzleOffset, MuzzleSocketName |
| **Разброс** | BaseSpread, SpreadPerShot, MaxSpread, SpreadDecayRate, SpreadRecoveryDelay, MovingSpreadAdd, JumpingSpreadAdd |
| **Отдача** | KickPitch/Yaw Min/Max, KickRecoverySpeed, KickDamping, RecoilPatternCurve, PatternScale |
| **Тряска экрана** | ShakeAmplitude, ShakeFrequency, ShakeDecaySpeed |
| **Прицеливание (ADS)** | ADSFOV, ADSTransitionIn/OutSpeed, ADSSensitivityMultiplier, SightAnchorSocket |
| **Движение оружия** | InertiaStiffness, InertiaDamping, IdleSway, WalkBob, StrafeTilt, LandingImpact, SprintPose |
| **Коллизия** | CollisionTraceDistance, RetractDistances, ReadyPoseOffsets |
| **Визуал** | EquippedMesh, AttachSocket, AttachOffset, DroppedMesh, DroppedScale |
| **Анимации** | FireMontage, ReloadMontage, EquipMontage |

### UFlecsContainerProfile

| Поле | Тип | Описание |
|------|-----|----------|
| `ContainerName` | `FName` | Внутреннее имя |
| `DisplayName` | `FText` | Отображаемое имя в UI |
| `ContainerType` | `EContainerType` | Grid, Slot или List |
| `GridWidth` / `GridHeight` | `int32` | Размеры сетки (тип Grid) |
| `Slots` | `TArray<FContainerSlotDefinition>` | Именованные слоты (тип Slot) |
| `MaxListItems` | `int32` | Максимум предметов (тип List) |
| `AllowedItemFilter` | `FGameplayTagQuery` | Ограничение типов предметов |
| `MaxWeight` | `float` | Ограничение по весу |
| `bAllowNestedContainers` | `bool` | Разрешить контейнеры внутри |
| `bAutoStackOnAdd` | `bool` | Автостакинг совпадающих предметов |

### UFlecsInteractionProfile

| Поле | Тип | Описание |
|------|-----|----------|
| `InteractionPrompt` | `FText` | Текст подсказки в UI |
| `InteractionRange` | `float` | Максимальная дистанция взаимодействия (см) |
| `bSingleUse` | `bool` | Убрать интерактивность после использования |
| `InteractionType` | `EInteractionType` | Instant, Focus или Hold |
| `bRestrictAngle` | `bool` | Ограничение по углу |
| `InteractionAngle` | `float` | Максимальный угол от направления сущности |

**Для Instant:** `InstantAction`, `CustomEventTag`

**Для Focus:** `bMoveCamera`, `FocusWidgetClass`, `FocusCameraPosition/Rotation`, `FocusFOV`, `TransitionIn/OutTime`

**Для Hold:** `HoldDuration`, `bCanCancel`, `CompletionAction`, `HoldCompletionEventTag`

### UFlecsMovementProfile

См. [Система передвижения](../systems/movement-system.md) для полного списка полей.

### Другие профили

| Профиль | Ключевые поля |
|---------|--------------|
| `UFlecsNiagaraProfile` | AttachedEffect, DeathEffect, Scale, Offset |
| `UFlecsDestructibleProfile` | См. [Система разрушаемых объектов](../systems/destructible-system.md) |
| `UFlecsDoorProfile` | См. [Система дверей](../systems/door-system.md) |
| `UFlecsAbilityDefinition` | См. [Система способностей](../systems/ability-system.md) |
| `UFlecsResourcePoolProfile` | См. [Система способностей](../systems/ability-system.md) |
| `UFlecsClimbProfile` | ClimbSpeed, TopExitDirection, TopExitVelocity, DetachAngle |
| `UFlecsRopeSwingProfile` | AttachOffset, SwingRadius, MaxSwingForce, AngularDamping |
| `UFlecsStealthLightProfile` | LightType, Intensity, Radius, ConeAngles, Direction |
| `UFlecsNoiseZoneProfile` | Extent (полуразмеры), SurfaceType |

---

## UFlecsItemDefinition

Отдельный data asset для метаданных предмета:

| Поле | Тип | Описание |
|------|-----|----------|
| `ItemTypeId` | `int32` | Автохеш от ItemName |
| `ItemName` | `FName` | Внутреннее имя (для хеширования TypeId) |
| `DisplayName` | `FText` | Отображаемое имя в UI |
| `Description` | `FText` | Описание предмета |
| `ItemTags` | `FGameplayTagContainer` | Теги классификации |
| `MaxStackSize` | `int32` | Лимит стака |
| `GridSize` | `FIntPoint` | Размер в сетке инвентаря (Ш x В) |
| `Weight` | `float` | Вес за единицу |
| `BaseValue` | `int32` | Стоимость в валюте |
| `RarityTier` | `int32` | Уровень редкости |
| `Icon` | `UTexture2D*` | Иконка инвентаря |
| `IconTint` | `FLinearColor` | Цветовой оттенок иконки |
| `Actions` | `TArray<FItemAction>` | Действия контекстного меню |
| `EquipmentSlot` | `FGameplayTag` | Тип слота экипировки |
| `MaxDurability` | `float` | Прочность (0 = без прочности) |

---

## Быстрый старт: создание сущности

1. **Content Browser** -> Правый клик -> Data Asset -> `FlecsEntityDefinition`
2. Назовите `DA_MyEntity`
3. Добавьте необходимые профили:
   - PhysicsProfile -> Задайте радиус коллизии, массу, слой
   - RenderProfile -> Задайте меш и материал
   - HealthProfile -> Задайте максимальное HP, если объект разрушаемый
4. Поставьте `AFlecsEntitySpawnerActor` на уровне
5. Установите `EntityDefinition = DA_MyEntity`
6. Играйте!
