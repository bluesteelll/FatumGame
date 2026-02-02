# FatumGame - Project Documentation

## ⚠️ КЛЮЧЕВЫЕ ПРИНЦИПЫ РАЗРАБОТКИ

### 1. НЕ ИСПОЛЬЗОВАТЬ КОСТЫЛИ

**НИКОГДА не применяй обходные решения (workarounds/hacks)!**

Всегда ищи и исправляй **КОРНЕВУЮ ПРИЧИНУ** проблемы:
- ❌ ПЛОХО: "Удалим ISM если тело не найдено" → костыль, скрывающий утечку
- ✅ ХОРОШО: "Почему тело уничтожается раньше ISM?" → найти первопричину

**Если видишь симптом - копай глубже, пока не найдёшь первопричину!**

### 2. FAIL-FAST

**Ошибки должны проявляться немедленно и громко!**

- Используй `check()`, `ensure()`, `checkf()` для инвариантов
- Валидируй входные данные в начале функции
- Не "проглатывай" ошибки молча — логируй и падай/возвращай ошибку
- ❌ ПЛОХО: `if (!Ptr) return;` — тихо скрывает баг
- ✅ ХОРОШО: `check(Ptr);` или `if (!Ptr) { UE_LOG(LogTemp, Error, TEXT("...")); return; }`

**Чем раньше ошибка обнаружена — тем проще её найти и исправить!**

### 3. ИЗБЕГАТЬ БОЙЛЕРПЛЕЙТА

**Код должен быть лаконичным и выразительным!**

- Не дублируй код — выноси в функции/шаблоны
- Используй auto, range-based for, structured bindings
- Предпочитай декларативный стиль императивному
- ❌ ПЛОХО: Копипаста одинаковой логики в 5 местах
- ✅ ХОРОШО: Одна функция/макрос, переиспользуемая везде

```cpp
// ❌ Бойлерплейт
for (int32 i = 0; i < Array.Num(); ++i) { DoSomething(Array[i]); }

// ✅ Лаконично
for (auto& Item : Array) { DoSomething(Item); }
```

**Меньше кода = меньше багов = проще поддержка!**

---

## Quick Reference

| Property | Value |
|----------|-------|
| **Engine** | Unreal Engine 5.7 |
| **Physics** | Jolt (via Barrage) |
| **ECS** | Flecs (via FlecsIntegration) |
| **Networking** | Bristlecone (deterministic rollback) |
| **Simulation** | 120Hz on Artillery thread |

### Architecture
```
Bristlecone   → Network inputs (UDP, deterministic)
Cabling       → Local inputs & keymapping
Artillery     → Core simulation (~120Hz, separate thread)
Barrage       → Jolt Physics
Flecs         → Gameplay data (health, damage, items)
Game Thread   → Cosmetics, rendering (non-deterministic)
```

---

## QUICK START

### 1. Создай Data Asset снаряда
Content Browser → ПКМ → Data Asset → **FlecsProjectileDefinition** → `DA_Bullet`

### 2. Создай Blueprint персонажа
Content Browser → Blueprint Class → **FlecsCharacter** → `BP_Player`
- Projectile Definition = `DA_Bullet`
- Max Health = 100
- Add: Spring Arm + Camera

### 3. Настрой Input (Project Settings → Input)
```
Axis: MoveForward (W/S), MoveRight (A/D), Turn (Mouse X), LookUp (Mouse Y)
Action: Fire (LMB), Jump (Space)
```

### 4. Создай GameMode
- Default Pawn = `BP_Player`
- Player Controller = `ABarragePlayerController`

### 5. На карте
- World Settings → GameMode Override
- Добавь Player Start

**Play!** WASD работает автоматически через UBarrageCharacterMovement.

---

## Key Files (Source/FatumGame/)

| Файл | Назначение |
|------|------------|
| `FlecsCharacter.h/cpp` | Персонаж с Flecs (здоровье, урон, стрельба, тест спавна E/F) |
| `FlecsEntitySpawner.h/cpp` | **Unified Entity API**: FEntitySpawnRequest, UFlecsEntityLibrary |
| `FlecsEntityDefinition.h` | **Unified preset** объединяющий все профили |
| `FlecsPhysicsProfile.h` | Профиль физики (масса, коллизия, слой) |
| `FlecsRenderProfile.h` | Профиль рендера (меш, материал, масштаб) |
| `FlecsHealthProfile.h` | Профиль здоровья (HP, броня, реген) |
| `FlecsDamageProfile.h` | Профиль урона (damage, area, crit) |
| `FlecsProjectileProfile.h` | Профиль снаряда (lifetime, bounces, speed) |
| `FlecsContainerProfile.h` | Профиль контейнера (Grid/Slot/List) |
| `FlecsItemDefinition.h` | Определение предмета (стакинг, действия) |
| `FlecsComponents.h/cpp` | ECS компоненты: FHealthData, FDamageSource, FBarrageBody, теги |
| `FlecsGameplayLibrary.h/cpp` | Blueprint API: SpawnProjectile, ApplyDamage, Heal |
| `FlecsArtillerySubsystem.h/cpp` | Мост Artillery↔Flecs, lock-free bidirectional binding, коллизии |

**Plugins/Barrage:** `FBarragePrimitive.h` - atomic `FlecsEntityId` для reverse binding

---

## Core Systems

### Artillery (120Hz Simulation)
- `UArtilleryDispatch` - World subsystem, 120Hz tick
- `FArtilleryBusyWorker` - Worker thread
- `FArtilleryGun` - Gun/ability instances

### Barrage (Jolt Physics)
- `UBarrageDispatch` - Physics world subsystem
- `UBarrageCharacterMovement` - Auto movement (bAutoProcessInput=true)
- `OnBarrageContactAddedDelegate` - Collision events (120Hz)
- `SetBodyObjectLayer(FBarrageKey, uint8)` - Change collision layer (use for entity cleanup!)
- `SuggestTombstone(FBLet)` - Safe deferred destruction (~19 sec)

### Flecs ECS
**Direct flecs::world** created in `FlecsArtillerySubsystem`, bypassing UnrealFlecs plugin ticks.

**Lock-free bidirectional binding** - see [Lock-Free Bidirectional Binding](#lock-free-bidirectional-binding-january-2025)

```cpp
// Components
FHealthData      { CurrentHP, MaxHP, Armor }
FDamageSource    { Damage, DamageType, bAreaDamage }
FProjectileData  { LifetimeRemaining, MaxBounces, GraceFramesRemaining }
FBarrageBody     { SkeletonKey }  // Forward binding: Entity → BarrageKey

// Item components (Prefab System - February 2025)
FItemStaticData  { TypeId, MaxStack, Weight, GridSize, ItemName, EntityDefinition*, ItemDefinition* }  // In PREFAB
FItemInstance    { Count }  // Instance data only (TypeId moved to FItemStaticData)
FContainedIn     { ContainerEntityId, GridPosition, SlotIndex }  // Links item → container

// Container components
FContainerBase   { Type, DefinitionId, OwnerEntityId, CurrentWeight, MaxWeight }
FContainerListData { MaxItems, CurrentCount }
FContainerGridData { Width, Height, OccupancyMask }

// Tags (zero-size): FTagItem, FTagContainer, FTagDestructible, FTagDead, FTagProjectile, FTagCharacter
```

**Systems:**
- `ItemDespawnSystem` - Despawn timer
- `DeathCheckSystem` - HP <= 0 → FTagDead
- `DeadEntityCleanupSystem` - Destroy dead entities
- `ProjectileLifetimeSystem` - Lifetime, velocity check with grace period

### SkeletonKey (Entity Identity)
64-bit keys: `[Type:4][Metadata:28][Hash:32]`

| Nibble | Type |
|--------|------|
| 0x3 | GUN_SHOT (Projectiles) |
| 0x4 | BAR_PRIM (Barrage primitives) |
| 0x5 | ART_ACTS (Actors) |
| 0xC | ITEM |

---

## Blueprint API (UFlecsGameplayLibrary)

```cpp
// Spawn projectile (thread-safe)
UFlecsGameplayLibrary::SpawnProjectile(World, Definition, Location, Direction, Speed);

// Damage/Heal (thread-safe, enqueues to Artillery)
UFlecsGameplayLibrary::ApplyDamageByBarrageKey(World, TargetKey, 25.f);
UFlecsGameplayLibrary::HealEntityByBarrageKey(World, TargetKey, 50.f);
UFlecsGameplayLibrary::KillEntityByBarrageKey(World, EntityKey);
```

---

## AFlecsCharacter

**Features:**
- Flecs entity on BeginPlay (FHealthData, FTagCharacter)
- Auto damage from projectiles via collision
- Barrage physics movement
- **Test entity spawning** (E = spawn, F = destroy)
- **Container testing** (E = spawn container / add item, F = remove all items)

**Blueprint Events:**
- `OnDamageTaken(float Damage, float NewHealth)`
- `OnDeath()`
- `OnHealed(float Amount, float NewHealth)`

**Properties:**
- `ProjectileDefinition` - UFlecsProjectileDefinition Data Asset
- `TestEntityDefinition` - UFlecsEntityDefinition для теста спавна (E/F)
- `TestContainerDefinition` - UFlecsEntityDefinition с ContainerProfile для теста контейнеров
- `TestItemDefinition` - UFlecsEntityDefinition с ItemDefinition для добавления в контейнер
- `MaxHealth`, `Armor`

**Input Actions (Enhanced Input):**
- `MoveAction`, `LookAction`, `JumpAction`, `FireAction`
- `SpawnItemAction` (E) - спавн TestEntityDefinition ИЛИ контейнер/добавить предмет
- `DestroyItemAction` (F) - удаление последнего заспавненного ИЛИ очистить контейнер

**Режим работы E/F:**
- Если `TestContainerDefinition` + `TestItemDefinition` заданы → режим контейнера
- Иначе → режим спавна сущностей

---

## UFlecsProjectileDefinition (Data Asset)

| Property | Description |
|----------|-------------|
| Mesh, Material, VisualScale | Visuals |
| CollisionRadius, bIsBouncing, Restitution | Physics |
| DefaultSpeed | Movement |
| Damage, bAreaDamage, AreaRadius | Damage |
| LifetimeSeconds, MaxBounces | Lifetime |

---

## Collision Handling

All collision logic in `UFlecsArtillerySubsystem::OnBarrageContact()`:

**Automatic:**
- Projectiles with `FDamageSource` → damage entities with `FHealthData`
- Armor respected: `EffectiveDamage = max(0, Damage - Armor)`
- `FTagDestructible` entities destroyed by projectiles
- Dead entities cleaned up by `DeadEntityCleanupSystem`

**Custom collision:**
```cpp
void UFlecsArtillerySubsystem::OnBarrageContact(const BarrageContactEvent& Event)
{
    // Example: Item pickup
    if (Entity1.has<FTagItem>() && Entity2.has<FTagCharacter>())
    {
        Entity1.add<FTagDead>();
    }
}
```

---

## Threading Model

| Thread | Frequency | Purpose |
|--------|-----------|---------|
| Artillery BusyWorker | ~120Hz | Deterministic simulation |
| Ticklites | ~120Hz | Lightweight mechanics |
| StateTrees | Variable | AI |
| Game Thread | ~60Hz+ | Rendering, cosmetics |

**Thread Safety:** Game thread → `EnqueueCommand()` → Artillery executes before `progress()`

---

## Lock-Free Bidirectional Binding (January 2025)

**Architecture for Entity ↔ Physics mapping WITHOUT locks:**

```
Forward lookup (Entity → BarrageKey): O(1)
├── Flecs sparse set: entity.get<FBarrageBody>()->BarrageKey

Reverse lookup (BarrageKey → Entity): O(1)
├── libcuckoo map: UBarrageDispatch::GetShapeRef(Key) → FBLet
└── atomic load:   FBLet->GetFlecsEntity()
```

### API (UFlecsArtillerySubsystem)

```cpp
// Bind/Unbind (sets both directions atomically)
void BindEntityToBarrage(flecs::entity Entity, FSkeletonKey BarrageKey);
void UnbindEntityFromBarrage(flecs::entity Entity);

// Lookups (lock-free O(1))
flecs::entity GetEntityForBarrageKey(FSkeletonKey BarrageKey) const;
FSkeletonKey GetBarrageKeyForEntity(flecs::entity Entity) const;
bool HasEntityForBarrageKey(FSkeletonKey BarrageKey) const;

// Flecs Stages (for future multi-threaded collision processing)
flecs::world GetStage(int32 ThreadIndex = 0) const;
```

### Implementation

**FBarragePrimitive.h** - Added atomic for reverse binding:
```cpp
std::atomic<uint64> FlecsEntityId{0};
void SetFlecsEntity(uint64 Id);
uint64 GetFlecsEntity() const;
void ClearFlecsEntity();
bool HasFlecsEntity() const;
```

**FlecsComponents.h** - FBarrageBody is forward binding:
```cpp
struct FBarrageBody
{
    FSkeletonKey BarrageKey;
    bool IsValid() const { return BarrageKey.IsValid(); }
};
```

### Usage

```cpp
// Creating entities (use in EnqueueCommand lambda)
flecs::entity Entity = FlecsWorld->entity()
    .set<FHealthData>({ 100.f, 100.f, 0.f })
    .add<FTagCharacter>();
Subsystem->BindEntityToBarrage(Entity, Key);

// Collision handlers (direct O(1) lookup)
uint64 FlecsId = Body->GetFlecsEntity();  // atomic read
if (FlecsId != 0) {
    flecs::entity Entity = FlecsWorld->entity(FlecsId);
    // ...
}

// Cleanup
Subsystem->UnbindEntityFromBarrage(Entity);
Entity.destruct();
```

### Flecs Stages (Future Multi-Threading)

Stages are thread-local command queues for safe multi-threaded writes:
```cpp
// Each collision thread will use its own stage
flecs::world Stage = Subsystem->GetStage(ThreadIndex);
Stage.defer([&]() {
    Entity.add<FTagDead>();  // Deferred, merged at sync point
});
```

Currently all processing runs on Artillery thread (stage 0). Infrastructure ready for future parallel collision processing in Barrage.

---

## Item Prefab System (February 2025)

**Статус: Реализовано, требуется тестирование**

Flecs prefabs используются для хранения shared static данных предметов. Это позволяет:
- Хранить статические данные (TypeId, MaxStack, Weight) один раз в prefab
- Хранить instance данные (Count) на каждой entity
- Получить EntityDefinition из любого item entity для спавна в мир

### Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│ PREFAB (один на тип предмета)                               │
│   FItemStaticData {                                         │
│     TypeId, MaxStack, Weight, GridSize, ItemName,           │
│     EntityDefinition*,  ← Ссылка на UFlecsEntityDefinition  │
│     ItemDefinition*     ← Ссылка на UFlecsItemDefinition    │
│   }                                                         │
└─────────────────────────────────────────────────────────────┘
           ▲ IsA (наследование)
           │
┌─────────────────────────────────────────────────────────────┐
│ ITEM ENTITY (каждый предмет)                                │
│   (IsA, Prefab)         ← Наследует FItemStaticData         │
│   FItemInstance { Count }  ← Instance данные                │
│   FContainedIn { ... }     ← В каком контейнере             │
│   FTagItem                 ← Zero-size tag                  │
└─────────────────────────────────────────────────────────────┘
```

### API (UFlecsArtillerySubsystem)

```cpp
// Создать/получить prefab для типа предмета
flecs::entity GetOrCreateItemPrefab(UFlecsEntityDefinition* EntityDef);

// Получить prefab по TypeId
flecs::entity GetItemPrefab(int32 TypeId) const;

// Получить Definition из item entity (через prefab)
UFlecsEntityDefinition* GetEntityDefinitionForItem(flecs::entity ItemEntity) const;
UFlecsItemDefinition* GetItemDefinitionForItem(flecs::entity ItemEntity) const;
```

### Использование

```cpp
// Добавление предмета в контейнер (с prefab)
UFlecsEntityLibrary::AddItemToContainerFromDefinition(
    World, ContainerKey, DA_HealthPotion, Count, OutAdded);

// Внутри (Artillery thread):
flecs::entity Prefab = Subsystem->GetOrCreateItemPrefab(EntityDef);
flecs::entity Item = FlecsWorld->entity()
    .is_a(Prefab)                    // Наследует FItemStaticData
    .set<FItemInstance>(Instance)    // Count only
    .set<FContainedIn>(Contained)
    .add<FTagItem>();

// Получение EntityDefinition для спавна в мир при дропе:
UFlecsEntityDefinition* Def = Subsystem->GetEntityDefinitionForItem(Item);
// Теперь можно использовать Def->PhysicsProfile, Def->RenderProfile, etc.
```

### Flecs API Gotchas

**ВАЖНО при работе с Flecs:**

```cpp
// ❌ НЕПРАВИЛЬНО: get<>() возвращает const T&, не указатель
const FItemStaticData* Data = entity.get<FItemStaticData>();  // ОШИБКА!

// ✅ ПРАВИЛЬНО: try_get<>() возвращает указатель (nullptr если нет)
const FItemStaticData* Data = entity.try_get<FItemStaticData>();

// ❌ НЕПРАВИЛЬНО: Aggregate init не работает с USTRUCT
entity.set<FItemInstance>({ 5 });  // ОШИБКА компиляции!

// ✅ ПРАВИЛЬНО: Явная инициализация
FItemInstance Instance;
Instance.Count = 5;
entity.set<FItemInstance>(Instance);
```

---

## Critical Patterns

### Subsystem OnWorldBeginPlay
```cpp
void UMySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);  // REQUIRED!
    // ...
}
```

### Subsystem Dependencies
```cpp
void UMySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UArtilleryDispatch>();  // BEFORE Super!
    Super::Initialize(Collection);
}
```

### Entity Destruction

**Correct approach - DEBRIS layer + tombstone:**

```cpp
// Get primitive and Jolt key
FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
FBarrageKey BarrageKey = Prim->KeyIntoBarrage;

// Clear Flecs binding
Prim->ClearFlecsEntity();

// Move to DEBRIS layer - IMMEDIATELY disables collision with gameplay entities
// (DEBRIS only collides with NON_MOVING static geometry)
CachedBarrageDispatch->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);

// Tombstone for safe deferred destruction (~19 seconds)
CachedBarrageDispatch->SuggestTombstone(Prim);
```

**Why this works:**
1. `SetBodyObjectLayer(DEBRIS)` - instantly disables collision with players, projectiles, enemies
2. `SuggestTombstone()` - schedules safe destruction when all Jolt internal refs are cleared
3. No crash risk - Jolt handles the body lifecycle properly

**WRONG approach (causes crash on PIE exit):**
```cpp
// DON'T DO THIS - corrupts Jolt state, crashes during character cleanup:
CachedBarrageDispatch->FinalizeReleasePrimitive(BarrageKey);  // CRASH RISK!
```

### Flecs World
Direct `flecs::world` in FlecsArtillerySubsystem, NOT UFlecsWorld wrapper:
```cpp
FlecsWorld = MakeUnique<flecs::world>();
FlecsWorld->set_threads(0);  // Artillery is only executor
```

---

## Input System

- **Single-player:** Standard UE Input → UBarrageCharacterMovement (bAutoProcessInput=true)
- **Multiplayer rollback:** Artillery Input → Cabling → Bristlecone → IArtilleryLocomotionInterface
- Current AFlecsCharacter uses **standard UE Input**

---

## Build Requirements

- Visual Studio 2022 (VC++ 14.44, Win11 SDK 22621)
- CMake 3.30.2+ (for Jolt)
- Git LFS

---

## Detailed Documentation

| Document | Purpose |
|----------|---------|
| `.claude/ARTILLERY_BARRAGE_DOCUMENTATION.md` | Full architecture |
| `.claude/BARRAGE_BLUEPRINT_INTEGRATION.md` | Blueprint API |
| `.claude/BARRAGE_INTEGRATION_SETUP.md` | Setup guide |
| `.claude/BARRAGE_AUTO_MOVEMENT_GUIDE.md` | Movement docs |

---

## Unified Entity Spawning System (February 2025)

**Статус: Реализовано, требуется тестирование**

### Архитектура

Все сущности (предметы, снаряды, контейнеры, персонажи) спавнятся через **единый API** с композицией профилей.

```
UFlecsEntityDefinition (unified preset)
    ├── UFlecsItemDefinition      (item logic)
    ├── UFlecsPhysicsProfile      (collision, mass)
    ├── UFlecsRenderProfile       (mesh, material)
    ├── UFlecsHealthProfile       (HP, armor)
    ├── UFlecsDamageProfile       (contact damage)
    ├── UFlecsProjectileProfile   (lifetime, bounces)
    └── UFlecsContainerProfile    (inventory)
```

### Профили (Data Assets с EditInlineNew)

Профили можно:
1. **Создать отдельно** как Data Asset и переиспользовать
2. **Создать inline** прямо внутри EntityDefinition (Instanced)

| Профиль | Назначение |
|---------|------------|
| `UFlecsPhysicsProfile` | CollisionRadius, Mass, Restitution, Friction, Layer, bIsSensor |
| `UFlecsRenderProfile` | Mesh, MaterialOverride, Scale, bCastShadow |
| `UFlecsHealthProfile` | MaxHealth, Armor, RegenPerSecond, bDestroyOnDeath |
| `UFlecsDamageProfile` | Damage, DamageType, bAreaDamage, AreaRadius, bDestroyOnHit |
| `UFlecsProjectileProfile` | DefaultSpeed, Lifetime, MaxBounces, GracePeriod |
| `UFlecsContainerProfile` | ContainerType (Grid/Slot/List), GridWidth/Height, MaxWeight |
| `UFlecsItemDefinition` | ItemTypeId, MaxStackSize, GridSize, Weight, Actions |

### UFlecsEntityDefinition (unified preset)

```cpp
UCLASS(BlueprintType)
class UFlecsEntityDefinition : public UPrimaryDataAsset
{
    // Profiles (Instanced - можно создать inline или выбрать существующий)
    TObjectPtr<UFlecsItemDefinition> ItemDefinition;
    TObjectPtr<UFlecsPhysicsProfile> PhysicsProfile;
    TObjectPtr<UFlecsRenderProfile> RenderProfile;
    TObjectPtr<UFlecsHealthProfile> HealthProfile;
    TObjectPtr<UFlecsDamageProfile> DamageProfile;
    TObjectPtr<UFlecsProjectileProfile> ProjectileProfile;
    TObjectPtr<UFlecsContainerProfile> ContainerProfile;

    // Tags
    bool bPickupable, bDestructible, bHasLoot, bIsCharacter;

    // Defaults
    int32 DefaultItemCount = 1;
    float DefaultDespawnTime = -1.f;
};
```

### Blueprint API (UFlecsEntityLibrary)

```cpp
// Spawn from unified definition
FSkeletonKey SpawnEntityFromDefinition(World, Definition, Location, Rotation);

// Spawn from request (full control)
FSkeletonKey SpawnEntity(World, FEntitySpawnRequest);

// Batch spawn
TArray<FSkeletonKey> SpawnEntities(World, TArray<FEntitySpawnRequest>);

// Destruction
void DestroyEntity(World, EntityKey);
void DestroyEntities(World, TArray<EntityKeys>);

// Health
bool ApplyDamage(World, TargetKey, Damage);
bool Heal(World, TargetKey, Amount);
void Kill(World, TargetKey);
float GetHealth(World, EntityKey);
float GetMaxHealth(World, EntityKey);

// Container operations (IMPLEMENTED February 2025)
bool AddItemToContainerFromDefinition(World, ContainerKey, EntityDef, Count, OutAdded);  // ✅ Prefab-based (recommended)
bool AddItemToContainer(World, ContainerKey, ItemDef, Count, OutAdded);  // ✅ Legacy (no EntityDef reference)
int32 RemoveAllItemsFromContainer(World, ContainerKey);                   // ✅ Working
int32 GetContainerItemCount(World, ContainerKey);                         // ✅ Working
bool RemoveItemFromContainer(World, ContainerKey, ItemEntityId, Count);   // ✅ Working

// Items (NOT YET IMPLEMENTED - need EntityDefinition from prefab for physics/render)
bool PickupItem(World, WorldItemKey, ContainerKey, OutPickedUp);          // ❌ TODO
FSkeletonKey DropItem(World, ContainerKey, ItemEntityId, Location, Count); // ❌ TODO (use GetEntityDefinitionForItem)
```

### FEntitySpawnRequest (C++ fluent builder)

```cpp
// Fluent API
FSkeletonKey Key = FEntitySpawnRequest::At(Location)
    .WithDefinition(DA_Bullet)         // или отдельные профили:
    .WithPhysics(DA_SmallPhysics)
    .WithRender(DA_BulletMesh)
    .WithDamage(DA_BulletDamage)
    .WithProjectile(DA_FastProjectile)
    .WithVelocity(Direction * Speed)
    .Spawn(WorldContext);

// From definition
FSkeletonKey Key = FEntitySpawnRequest::FromDefinition(DA_HealthPotion, Location)
    .Pickupable()
    .WithDespawn(30.f)
    .Spawn(WorldContext);
```

### Примеры композиции

| Сущность | Профили |
|----------|---------|
| Item in world | Item + Physics + Render + Pickupable |
| Projectile | Physics + Render + Damage + Projectile |
| Destructible box | Physics + Render + Health + Destructible |
| Chest | Physics + Render + Container |
| Player inventory | Container only (no world presence) |
| Trigger zone | Physics(Sensor) only |

### Тестирование в игре

**AFlecsCharacter** имеет встроенный тест:
- `TestEntityDefinition` - назначь EntityDefinition
- **E** - спавнит сущность перед персонажем
- **F** - удаляет последнюю заспавненную

**Input Actions (нужно создать):**
- `IA_SpawnItem` → E
- `IA_DestroyItem` → F

### Создание тестовой сущности

1. Content Browser → Data Asset → **FlecsEntityDefinition** → `DA_TestCube`
2. В редакторе:
   - Physics Profile → выбрать тип `FlecsPhysicsProfile` → настроить inline
   - Render Profile → выбрать тип `FlecsRenderProfile` → выбрать Mesh
   - bPickupable = true
3. В BP_Player:
   - Test Entity Definition = `DA_TestCube`
   - Spawn Item Action = `IA_SpawnItem`
   - Destroy Item Action = `IA_DestroyItem`
4. Play → E спавнит кубы, F удаляет

### Тестирование контейнеров (February 2025)

**Создание Data Assets:**

1. **DA_TestContainer** (FlecsEntityDefinition):
   - Container Profile → создать `FlecsContainerProfile` inline → ContainerType = `List`
   - Physics Profile → создать `FlecsPhysicsProfile` inline
   - Render Profile → создать `FlecsRenderProfile` inline → выбрать Mesh (куб/сундук)

2. **DA_TestItem** (FlecsEntityDefinition):
   - Item Definition → создать `FlecsItemDefinition` inline → ItemName = "TestItem"

**Настройка персонажа (BP_Player):**
```
Test Container Definition = DA_TestContainer
Test Item Definition = DA_TestItem
Spawn Item Action = IA_SpawnItem (E)
Destroy Item Action = IA_DestroyItem (F)
```

**Управление:**
| Клавиша | Действие |
|---------|----------|
| **E** (1-й раз) | Спавнит контейнер перед персонажем |
| **E** (далее) | Добавляет предмет в контейнер |
| **F** | Удаляет ВСЕ предметы из контейнера |

**On-screen сообщения:**
- `Container spawned: 12345` (зелёный)
- `Added item: TestItem (Container now has 3 items)` (голубой)
- `Removed all items from container (3 items removed)` (жёлтый)

**Как работает под капотом:**
1. Контейнер создаётся как Flecs entity с `FContainerBase` + `FContainerListData` + `FTagContainer`
2. Предметы создаются как отдельные Flecs entities с `FItemInstance` + `FContainedIn`
3. `FContainedIn.ContainerEntityId` связывает предмет с контейнером
4. При удалении — query по `FContainedIn` находит все предметы контейнера

---

## Known Issues / Debugging Tips

### Distance Constraint Spring не работает (нет упругости)

**Симптом:** Пружина Distance Constraint не сближает/отдаляет тела, или делает это очень медленно.

**Причина:** Старый скомпилированный код использовал неправильную конвертацию `SpringFrequency → Stiffness` (freq * 10000), делая пружину в ~54× жёстче чем нужно.

**Диагностика:** Проверить лог:
- ❌ Старый код: `DistanceConstraint SPRING: Stiffness=46400 N/m, Damping=311`
- ✅ Новый код: `DistanceConstraint SPRING: Frequency=15.00 Hz, Damping=0.31`

**Решение:**
1. Закрыть Unreal Editor и Rider/VS
2. Удалить папки: `Binaries/`, `Intermediate/`, `Plugins/Barrage/Binaries/`, `Plugins/Barrage/Intermediate/`
3. Rebuild Solution
4. Проверить что лог показывает `Frequency=... Hz`

**Параметры пружины:**
- `SpringFrequency`: 1-5 Hz (мягкая), 10-15 Hz (средняя), 20+ Hz (жёсткая)
- `SpringDamping`: 0 (бесконечные колебания), 0.3-0.5 (затухающие), 1.0 (без колебаний)

### Физическое тело не удаляется (остаётся коллизия после удаления)

**Симптом:** При удалении сущности меш/ISM удаляется, но физическое тело продолжает участвовать в коллизиях.

**Причина:** Использование только `SuggestTombstone()` создаёт **отложенное удаление на ~19 секунд**. Тело остаётся в Jolt simulation.

**Решение:** Переместить тело в DEBRIS layer (не коллидирует с gameplay):
```cpp
// ПРАВИЛЬНО - мгновенное отключение коллизии:
Prim->ClearFlecsEntity();
FBarrageKey BarrageKey = Prim->KeyIntoBarrage;
CachedBarrageDispatch->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);  // Disable collision
CachedBarrageDispatch->SuggestTombstone(Prim);                          // Safe deferred destroy

// НЕПРАВИЛЬНО - вызывает краш при выходе из PIE:
CachedBarrageDispatch->FinalizeReleasePrimitive(BarrageKey);  // НЕ ДЕЛАТЬ!
```

**Диагностика:** В логе должно быть:
- `PROJ_DEBUG Physics moved to DEBRIS layer: BarrageKey=...` — успех

### Краш при выходе из PIE (BodyManager::DestroyBodies)

**Симптом:** Краш в `JPH::BodyManager::DestroyBodies()` при выходе из PIE. Стек: `~UBarrageDispatch` → `~FWorldSimOwner` → `~CharacterVirtual` → crash.

**Причина:** Вызов `FinalizeReleasePrimitive()` во время gameplay корраптит внутреннее состояние Jolt. При shutdown, когда CharacterVirtual пытается уничтожить свой inner body, Jolt крашится.

**Почему это происходит:**
- `FinalizeReleasePrimitive()` вызывает `body_interface->RemoveBody()` + `DestroyBody()`
- Jolt держит внутренние ссылки на тела (contact cache, broad phase, constraints)
- Немедленное удаление оставляет dangling references
- Tombstone система (~19 сек) даёт время Jolt очистить все ссылки

**Решение:** НИКОГДА не вызывать `FinalizeReleasePrimitive()` напрямую! Использовать:
1. `SetBodyObjectLayer(DEBRIS)` — мгновенно отключает gameplay коллизии
2. `SuggestTombstone()` — безопасное отложенное удаление

**API для изменения слоя:**
```cpp
// В FWorldSimOwner.h и BarrageDispatch:
void SetBodyObjectLayer(FBarrageKey BarrageKey, uint8 NewLayer);
```

**Слои коллизий (EPhysicsLayer.h):**
- `DEBRIS` — коллидирует ТОЛЬКО с `NON_MOVING` (статичная геометрия)
- Не коллидирует с: MOVING, PROJECTILE, HITBOX, ENEMY, CHARACTER

### Flecs API Gotchas (February 2025)

**Симптом:** Ошибки компиляции при работе с Flecs components.

**Проблема 1: `get<>()` vs `try_get<>()`**
```cpp
// ❌ НЕПРАВИЛЬНО: get<>() возвращает const T&, не const T*
const FItemStaticData* Data = entity.get<FItemStaticData>();  // ОШИБКА!

// ✅ ПРАВИЛЬНО: try_get<>() возвращает указатель
const FItemStaticData* Data = entity.try_get<FItemStaticData>();
```

**Проблема 2: Aggregate initialization с USTRUCT**
```cpp
// ❌ НЕПРАВИЛЬНО: USTRUCT с GENERATED_BODY() не поддерживает aggregate init
entity.set<FItemInstance>({ 5 });           // ОШИБКА компиляции!
entity.set<FContainedIn>({ id, pos, -1 });  // ОШИБКА компиляции!

// ✅ ПРАВИЛЬНО: Явная инициализация
FItemInstance Instance;
Instance.Count = 5;
entity.set<FItemInstance>(Instance);

FContainedIn Contained;
Contained.ContainerEntityId = id;
Contained.GridPosition = pos;
Contained.SlotIndex = -1;
entity.set<FContainedIn>(Contained);
```

**Примечание:** Plain C++ structs (без GENERATED_BODY) поддерживают aggregate init, USTRUCT — нет.

---

## Broken Assets (need cleanup)

- `Content/BP_MyArtilleryCharacter.uasset` - broken Enace refs
- `Content/DA_EnaceCont.uasset` - orphaned
- `Content/DA_MyItemDef.uasset` - orphaned

---

## Credits

- **Artillery/Barrage/Bristlecone:** Hedra, Breach Dogs, Oversized Sun Inc.
- **Jolt Physics:** Jorrit Rouwe
- **Mass ECS Sample:** Karl Mavko, Alvaro Jover
