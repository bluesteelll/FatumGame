# Flecs API Reference

> Quick reference for Flecs ECS API usage in FatumGame. Covers component access methods, system patterns, and critical gotchas.

---

## Component Access

| Method | Returns | If Missing | Thread Safety | Use When |
|--------|---------|------------|--------------|----------|
| `try_get<T>()` | `const T*` | `nullptr` | Read-only | Component might not exist |
| `get<T>()` | `const T&` | **Asserts** | Read-only | Guaranteed to exist |
| `try_get_mut<T>()` | `T*` | `nullptr` | Read-write | Might not exist, need to write |
| `get_mut<T>()` | `T&` | **Asserts** | Read-write | Guaranteed to exist, need to write |
| `obtain<T>()` | `T&` | **Creates default** | Write (deferred) | Create if missing, always write |
| `set<T>(val)` | `entity&` | **Creates** | Write (deferred) | Set value, create if missing |
| `add<T>()` | `entity&` | No-op if exists | Write (deferred) | Add tag or empty component |
| `remove<T>()` | `entity&` | No-op if missing | Write (deferred) | Remove component |
| `has<T>()` | `bool` | `false` | Read-only | Check existence |

---

## System Registration Patterns

### `.each()` — Simple Per-Entity

```cpp
World.system<FProjectileInstance>("ProjectileLifetimeSystem")
    .with<FTagProjectile>()
    .without<FTagDead>()
    .each([](flecs::entity E, FProjectileInstance& Proj)
    {
        Proj.LifetimeRemaining -= E.delta_time();
        if (Proj.LifetimeRemaining <= 0.f)
            E.add<FTagDead>();
    });
```

Flecs auto-manages the iterator. Tags are filtered via `.with<>()` / `.without<>()`, not passed as parameters.

### `.run()` with Query Terms — Manual Iterator

```cpp
World.system<FWeaponStatic, FWeaponInstance, FAimDirection>("WeaponFireSystem")
    .with<FTagWeapon>()
    .run([this](flecs::iter& It)
    {
        while (It.next())  // MUST drain!
        {
            auto Statics = It.field<FWeaponStatic>(0);
            auto Instances = It.field<FWeaponInstance>(1);

            for (auto i : It)
            {
                // Process weapon...
            }
        }
    });
```

!!! danger "Must Drain Iterator"
    `.run()` systems with query terms must either drain (`while (It.next())`) or call `It.fini()` on early exit. Failure causes `ECS_LEAK_DETECTED`.

### `.run()` without Query Terms — Singleton

```cpp
World.system<>("CollisionPairCleanupSystem")
    .run([this](flecs::iter& It)
    {
        // No query terms → EcsQueryMatchNothing
        // Flecs auto-fini's. Do NOT call It.fini() (double-fini crash).
        auto Query = World.query_builder()
            .with<FCollisionPair>()
            .build();

        Query.each([](flecs::entity E) { E.destruct(); });
    });
```

### Observer — Reactive

```cpp
World.observer<FPendingDamage>("DamageObserver")
    .event(flecs::OnSet)
    .each([](flecs::entity E, FPendingDamage& Pending)
    {
        // Fires immediately on modified<FPendingDamage>()
    });
```

---

## Tag Query Rules

!!! danger "Never Pass Tags as Typed Parameters"
    Zero-size tags crash when passed as `const T&` to `.each()`:

    ```cpp
    // WRONG — ecs_field_w_size assertion crash
    World.each([](flecs::entity E, const FTagDead&) { ... });

    // CORRECT — use .with<> filter
    World.system<FHealthInstance>("DeathCheck")
        .without<FTagDead>()
        .each([](flecs::entity E, FHealthInstance& H)
        {
            if (H.CurrentHP <= 0.f) E.add<FTagDead>();
        });
    ```

    **Exception:** `system<T>().each()` is safe — the system builder strips references from template params. But `World.each()` does not.

---

## USTRUCT Rules

### No Aggregate Initialization

```cpp
// WRONG — GENERATED_BODY() adds hidden members
entity.set<FHealthInstance>({ 100.f });

// CORRECT
FHealthInstance Health;
Health.CurrentHP = 100.f;
entity.set<FHealthInstance>(Health);
```

### Component Registration Order

All components must be registered before any system references them:

```cpp
void SetupFlecsSystems()
{
    // Step 1: Register ALL components
    World.component<FHealthStatic>();
    World.component<FHealthInstance>();
    // ... ~50 components

    // Step 2: NOW register systems
    World.system<FHealthInstance>("DeathCheck").each(...);
}
```

---

## Prefab Usage

### Create Prefab

```cpp
flecs::entity Prefab = World.prefab()
    .set<FHealthStatic>({ .MaxHealth = 100.f, .Armor = 0.2f })
    .set<FDamageStatic>({ .Damage = 25.f })
    .add<FTagProjectile>();
```

### Instantiate from Prefab

```cpp
flecs::entity Instance = World.entity()
    .is_a(Prefab)  // Inherit all prefab components
    .set<FHealthInstance>({ .CurrentHP = 100.f })  // Override/add instance data
    .set<FBarrageBody>({ .SkeletonKey = Key });
```

### Query with Inheritance

Queries automatically match entities that inherit components from prefabs. An entity with `is_a(Prefab)` where Prefab has `FHealthStatic` will match queries that require `FHealthStatic`, even though the instance doesn't have it directly.

---

## Deferred Operation Gotchas

### Case 1: Between `.run()` Systems

```cpp
// System A
entity.set<FMyData>({ 42 });

// System B (same tick) — FMyData is nullptr!
auto* Data = entity.try_get<FMyData>();
```

**Fix:** Use subsystem-owned `TArray` for inter-system data.

### Case 2: Within Same System

```cpp
entity.obtain<FPendingDamage>().Hits.Add(Hit);  // Deferred write
auto* Pending = entity.try_get<FPendingDamage>(); // nullptr!
```

**Fix:** Track in local variables.

### Case 3: Cross-Entity Tags

```cpp
// In .each() callback for Entity A
OtherEntity.add<FTagDead>();  // Deferred!
// Later system querying FTagDead won't see it this tick
```

**Fix:** Perform immediate side effects (`SetBodyObjectLayer(DEBRIS)`) instead of relying on deferred tags.
