# Blueprint-библиотеки

> Все Blueprint-вызываемые функции для взаимодействия с ECS-симуляцией из Blueprints или C++ на game thread. Каждая функция внутри использует `EnqueueCommand` для потокобезопасности -- их можно вызывать из любого контекста game thread.

---

## UFlecsEntityLibrary

**Заголовок:** `Spawning/Public/FlecsEntitySpawner.h`

Управление жизненным циклом сущностей.

| Функция | Возвращает | Описание |
|---------|-----------|----------|
| `SpawnEntity(WorldContext, Request)` | `FSkeletonKey` | Создать сущность из `FEntitySpawnRequest` |
| `SpawnEntityFromDefinition(WorldContext, Def, Location, Rotation)` | `FSkeletonKey` | Удобный метод: создание из определения + трансформ |
| `SpawnEntities(WorldContext, Requests)` | `TArray<FSkeletonKey>` | Пакетное создание |
| `DestroyEntity(WorldContext, EntityKey)` | `void` | Добавляет `FTagDead` -- очистка на следующем тике |
| `DestroyEntities(WorldContext, EntityKeys)` | `void` | Пакетное уничтожение |
| `IsEntityAlive(WorldContext, EntityKey)` | `bool` | Проверка существования и отсутствия метки смерти |
| `GetEntityId(WorldContext, EntityKey)` | `int64` | Получить Flecs entity ID по SkeletonKey |

---

## UFlecsDamageLibrary

**Заголовок:** `Weapon/Public/Library/FlecsDamageLibrary.h`

Урон, лечение и запросы здоровья.

| Функция | Возвращает | Описание |
|---------|-----------|----------|
| `ApplyDamageByBarrageKey(WorldContext, BarrageKey, Damage)` | `void` | Поставить в очередь удар урона |
| `ApplyDamageWithType(WorldContext, BarrageKey, Damage, DamageType, bIgnoreArmor)` | `void` | Поставить в очередь типизированный урон |
| `HealEntityByBarrageKey(WorldContext, BarrageKey, Amount)` | `void` | Добавить HP (ограничено максимумом) |
| `KillEntityByBarrageKey(WorldContext, BarrageKey)` | `void` | Мгновенное убийство (обходит урон, добавляет `FTagDead`) |
| `GetEntityHealth(WorldContext, BarrageKey)` | `float` | Текущее HP (читает `FSimStateCache`) |
| `GetEntityMaxHealth(WorldContext, BarrageKey)` | `float` | Максимальное HP |
| `IsEntityAlive(WorldContext, BarrageKey)` | `bool` | HP > 0 и не мёртв |

---

## UFlecsWeaponLibrary

**Заголовок:** `Weapon/Public/Library/FlecsWeaponLibrary.h`

Управление оружием и запросы.

| Функция | Возвращает | Описание |
|---------|-----------|----------|
| `StartFiring(WorldContext, WeaponEntityId)` | `void` | Установить `bFireInputActive = true` |
| `StopFiring(WorldContext, WeaponEntityId)` | `void` | Установить `bFireInputActive = false` |
| `ReloadWeapon(WorldContext, WeaponEntityId)` | `void` | Начать перезарядку |
| `SetAimDirection(WorldContext, CharacterId, Direction, Position)` | `void` | Обновить прицеливание (предпочтительнее FLateSyncBridge) |
| `GetWeaponAmmo(WorldContext, WeaponEntityId)` | `int32` | Текущие патроны в магазине |
| `GetWeaponAmmoInfo(WorldContext, WeaponEntityId, OutCurrent, OutMag, OutReserve)` | `bool` | Полное состояние боеприпасов |
| `IsWeaponReloading(WorldContext, WeaponEntityId)` | `bool` | Идёт ли перезарядка |

---

## UFlecsContainerLibrary

**Заголовок:** `Item/Public/Library/FlecsContainerLibrary.h`

Операции с предметами и контейнерами.

| Функция | Возвращает | Описание |
|---------|-----------|----------|
| `AddItemToContainer(WorldContext, ContainerId, EntityDef, Count, OutAdded, bAutoStack)` | `bool` | Добавить предметы в контейнер |
| `RemoveItemFromContainer(WorldContext, ContainerId, ItemEntityId, Count)` | `bool` | Удалить предметы |
| `RemoveAllItemsFromContainer(WorldContext, ContainerId)` | `int32` | Удалить все, возвращает количество |
| `TransferItem(WorldContext, SourceId, DestId, ItemEntityId, DestGridPos)` | `bool` | Переместить между контейнерами |
| `PickupItem(WorldContext, WorldItemKey, ContainerId, OutPickedUp)` | `bool` | Подобрать предмет из мира |
| `DropItem(WorldContext, ContainerId, ItemEntityId, DropLocation, Count)` | `FSkeletonKey` | Выбросить предмет в мир |
| `GetContainerItemCount(WorldContext, ContainerId)` | `int32` | Текущее количество предметов |
| `SetItemDespawnTimer(WorldContext, BarrageKey, Timer)` | `void` | Установить таймер исчезновения мирового предмета |

---

## UFlecsInteractionLibrary

**Заголовок:** `Interaction/Public/Library/FlecsInteractionLibrary.h`

Выполнение взаимодействий и запросы.

| Функция | Возвращает | Описание |
|---------|-----------|----------|
| `ExecuteInteraction(WorldContext, Action, TargetKey, InventoryId, EventTag)` | `void` | Выполнить мгновенное действие на цели |
| `ApplySingleUseIfNeeded(WorldContext, TargetKey)` | `void` | Убрать `FTagInteractable`, если одноразовый |
| `GetToggleState(WorldContext, TargetKey)` | `bool` | Прочитать состояние переключателя |

Только C++ (не BlueprintCallable):

| Функция | Описание |
|---------|----------|
| `DispatchInstantAction(...)` | Полный вариант с callback |
| `DispatchContainerInteraction(...)` | С делегатом `FOnContainerOpened` |

---

## UFlecsSpawnLibrary

**Заголовок:** `Spawning/Public/Library/FlecsSpawnLibrary.h`

Специализированные вспомогательные функции создания.

| Функция | Возвращает | Описание |
|---------|-----------|----------|
| `SpawnProjectileFromEntityDef(WorldContext, Def, Location, Direction, Speed, OwnerId)` | `FSkeletonKey` | Создать снаряд |
| `SpawnConstrainedGroup(WorldContext, Def, Location, Rotation)` | `FFlecsGroupSpawnResult` | Создать группу, связанную констрейнтами |
| `SpawnChain(WorldContext, Mesh, Start, Direction, Count, Spacing, BreakForce, MaxHP)` | `FFlecsGroupSpawnResult` | Создать физическую цепь |

!!! note "Устаревшие функции"
    `SpawnWorldItem()`, `SpawnDestructible()`, `SpawnLootableDestructible()` устарели. Используйте `UFlecsEntityLibrary::SpawnEntity()` с `FEntitySpawnRequest` вместо них.

---

## UFlecsConstraintLibrary

**Заголовок:** `Destructible/Public/Library/FlecsConstraintLibrary.h`

Создание и управление физическими констрейнтами.

| Функция | Описание |
|---------|----------|
| Создание фиксированных констрейнтов | Жёсткое соединение двух тел с силой/моментом разрыва |
| Создание шарнирных констрейнтов | Вращательное соединение с ограничениями угла и мотором |
| Создание дистанционных констрейнтов | Пружинно-демпферное соединение |
| Разрыв констрейнта | Принудительный разрыв конкретного констрейнта |

---

## AFlecsCharacter (BlueprintCallable)

Ключевые функции, доступные из актора персонажа:

### Здоровье
| Функция | Возвращает |
|---------|-----------|
| `GetCurrentHealth()` | `float` |
| `GetHealthPercent()` | `float` |
| `IsAlive()` | `bool` |
| `ApplyDamage(Damage)` | `void` |
| `Heal(Amount)` | `void` |

### Оружие
| Функция | Возвращает |
|---------|-----------|
| `SpawnAndEquipTestWeapon()` | `void` |
| `StartFiringWeapon()` | `void` |
| `StopFiringWeapon()` | `void` |
| `ReloadTestWeapon()` | `void` |
| `IsAimingDownSights()` | `bool` |
| `GetADSAlpha()` | `float` |

### Инвентарь
| Функция | Возвращает |
|---------|-----------|
| `GetInventoryEntityId()` | `uint64` |
| `GetWeaponInventoryEntityId()` | `uint64` |
| `IsInventoryOpen()` | `bool` |
| `IsLootOpen()` | `bool` |

### Взаимодействие
| Функция | Возвращает |
|---------|-----------|
| `GetInteractionTarget()` | `FSkeletonKey` |
| `HasInteractionTarget()` | `bool` |
| `GetInteractionPrompt()` | `FText` |
| `IsInInteraction()` | `bool` |

### Идентификация
| Функция | Возвращает |
|---------|-----------|
| `GetEntityKey()` | `FSkeletonKey` |
| `GetCharacterEntityId()` | `uint64` |

### Blueprint-события
| Событие | Когда срабатывает |
|---------|------------------|
| `OnDamageTaken(Damage)` | Персонаж получает урон |
| `OnDeath()` | Персонаж умирает |
| `OnHealed(Amount)` | Персонаж исцелён |
| `OnInteractionTargetChanged(NewTarget)` | Цель появилась/пропала |
| `OnInteractionStateChanged(NewState)` | Переход состояния автомата |
| `OnHoldProgressChanged(Progress)` | Прогресс удержания взаимодействия |
