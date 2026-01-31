# FatumGame - Project Documentation

## ⚠️ ВАЖНЫЙ ПРИНЦИП: НЕ ИСПОЛЬЗОВАТЬ КОСТЫЛИ

**НИКОГДА не применяй обходные решения (workarounds/hacks)!**

Всегда ищи и исправляй **КОРНЕВУЮ ПРИЧИНУ** проблемы:
- ❌ ПЛОХО: "Удалим ISM если тело не найдено" → костыль, скрывающий утечку
- ✅ ХОРОШО: "Почему тело уничтожается раньше ISM?" → найти первопричину

**Если видишь симптом - копай глубже, пока не найдёшь первопричину!**

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
| `FlecsCharacter.h/cpp` | Персонаж с Flecs (здоровье, урон, стрельба) |
| `FlecsProjectileDefinition.h/cpp` | Data Asset для снарядов |
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

### Flecs ECS
**Direct flecs::world** created in `FlecsArtillerySubsystem`, bypassing UnrealFlecs plugin ticks.

**Lock-free bidirectional binding** - see [Lock-Free Bidirectional Binding](#lock-free-bidirectional-binding-january-2025)

```cpp
// Components
FHealthData      { CurrentHP, MaxHP, Armor }
FDamageSource    { Damage, DamageType, bAreaDamage }
FProjectileData  { LifetimeRemaining, MaxBounces, GraceFramesRemaining }
FBarrageBody     { SkeletonKey }  // Forward binding: Entity → BarrageKey
// Tags: FTagItem, FTagDestructible, FTagDead, FTagProjectile, FTagCharacter
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

**Blueprint Events:**
- `OnDamageTaken(float Damage, float NewHealth)`
- `OnDeath()`
- `OnHealed(float Amount, float NewHealth)`

**Properties:**
- `ProjectileDefinition` - UFlecsProjectileDefinition Data Asset
- `MaxHealth`, `Armor`

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
```cpp
// CORRECT - let tombstone handle cleanup:
UBarrageDispatch::SelfPtr->SuggestTombstone(Prim);

// WRONG - causes double-free:
// UBarrageDispatch::SelfPtr->FinalizeReleasePrimitive(Key);
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
