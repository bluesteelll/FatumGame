# Справочник Flecs API

> Краткий справочник по использованию Flecs ECS API в FatumGame. Охватывает методы доступа к компонентам, паттерны систем и критические подводные камни.

---

## Доступ к компонентам

| Метод | Возвращает | Если отсутствует | Потокобезопасность | Когда использовать |
|-------|-----------|-----------------|-------------------|-------------------|
| `try_get<T>()` | `const T*` | `nullptr` | Только чтение | Компонент может не существовать |
| `get<T>()` | `const T&` | **Assert** | Только чтение | Гарантированно существует |
| `try_get_mut<T>()` | `T*` | `nullptr` | Чтение-запись | Может не существовать, нужна запись |
| `get_mut<T>()` | `T&` | **Assert** | Чтение-запись | Гарантированно существует, нужна запись |
| `obtain<T>()` | `T&` | **Создаёт по умолчанию** | Запись (отложенная) | Создать если нет, всегда писать |
| `set<T>(val)` | `entity&` | **Создаёт** | Запись (отложенная) | Установить значение, создать если нет |
| `add<T>()` | `entity&` | Ничего если есть | Запись (отложенная) | Добавить tag или пустой компонент |
| `remove<T>()` | `entity&` | Ничего если нет | Запись (отложенная) | Удалить компонент |
| `has<T>()` | `bool` | `false` | Только чтение | Проверить наличие |

---

## Паттерны регистрации систем

### `.each()` -- простая обработка по сущностям

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

Flecs автоматически управляет итератором. Теги фильтруются через `.with<>()` / `.without<>()`, а не передаются как параметры.

### `.run()` с условиями запроса -- ручной итератор

```cpp
World.system<FWeaponStatic, FWeaponInstance, FAimDirection>("WeaponFireSystem")
    .with<FTagWeapon>()
    .run([this](flecs::iter& It)
    {
        while (It.next())  // ОБЯЗАТЕЛЬНО дренировать!
        {
            auto Statics = It.field<FWeaponStatic>(0);
            auto Instances = It.field<FWeaponInstance>(1);

            for (auto i : It)
            {
                // Обработка оружия...
            }
        }
    });
```

!!! danger "Обязательно дренировать итератор"
    `.run()` системы с условиями запроса должны либо полностью дренировать (`while (It.next())`), либо вызывать `It.fini()` при раннем выходе. Иначе возникнет `ECS_LEAK_DETECTED`.

### `.run()` без условий запроса -- синглтон

```cpp
World.system<>("CollisionPairCleanupSystem")
    .run([this](flecs::iter& It)
    {
        // Нет условий запроса -> EcsQueryMatchNothing
        // Flecs автоматически финализирует. НЕ вызывайте It.fini() (двойная финализация -- краш).
        auto Query = World.query_builder()
            .with<FCollisionPair>()
            .build();

        Query.each([](flecs::entity E) { E.destruct(); });
    });
```

### Observer -- реактивный

```cpp
World.observer<FPendingDamage>("DamageObserver")
    .event(flecs::OnSet)
    .each([](flecs::entity E, FPendingDamage& Pending)
    {
        // Срабатывает сразу при modified<FPendingDamage>()
    });
```

---

## Правила запросов с тегами

!!! danger "Никогда не передавайте теги как типизированные параметры"
    Теги нулевого размера вызывают краш при передаче как `const T&` в `.each()`:

    ```cpp
    // НЕПРАВИЛЬНО -- краш с ecs_field_w_size assertion
    World.each([](flecs::entity E, const FTagDead&) { ... });

    // ПРАВИЛЬНО -- используйте фильтр .with<>
    World.system<FHealthInstance>("DeathCheck")
        .without<FTagDead>()
        .each([](flecs::entity E, FHealthInstance& H)
        {
            if (H.CurrentHP <= 0.f) E.add<FTagDead>();
        });
    ```

    **Исключение:** `system<T>().each()` безопасен -- system builder убирает ссылки из параметров шаблона. Но `World.each()` -- нет.

---

## Правила USTRUCT

### Без агрегатной инициализации

```cpp
// НЕПРАВИЛЬНО -- GENERATED_BODY() добавляет скрытые члены
entity.set<FHealthInstance>({ 100.f });

// ПРАВИЛЬНО
FHealthInstance Health;
Health.CurrentHP = 100.f;
entity.set<FHealthInstance>(Health);
```

### Порядок регистрации компонентов

Все компоненты должны быть зарегистрированы до того, как любая система на них сошлётся:

```cpp
void SetupFlecsSystems()
{
    // Шаг 1: Зарегистрировать ВСЕ компоненты
    World.component<FHealthStatic>();
    World.component<FHealthInstance>();
    // ... ~50 компонентов

    // Шаг 2: ТЕПЕРЬ регистрировать системы
    World.system<FHealthInstance>("DeathCheck").each(...);
}
```

---

## Использование Prefab

### Создание Prefab

```cpp
flecs::entity Prefab = World.prefab()
    .set<FHealthStatic>({ .MaxHealth = 100.f, .Armor = 0.2f })
    .set<FDamageStatic>({ .Damage = 25.f })
    .add<FTagProjectile>();
```

### Создание экземпляра из Prefab

```cpp
flecs::entity Instance = World.entity()
    .is_a(Prefab)  // Наследовать все компоненты prefab
    .set<FHealthInstance>({ .CurrentHP = 100.f })  // Переопределить/добавить данные экземпляра
    .set<FBarrageBody>({ .SkeletonKey = Key });
```

### Запрос с наследованием

Запросы автоматически совпадают с сущностями, наследующими компоненты от prefab. Сущность с `is_a(Prefab)`, где Prefab имеет `FHealthStatic`, будет совпадать с запросами, требующими `FHealthStatic`, даже если у экземпляра этого компонента нет напрямую.

---

## Подводные камни отложенных операций

### Случай 1: Между `.run()` системами

```cpp
// Система A
entity.set<FMyData>({ 42 });

// Система B (тот же тик) -- FMyData будет nullptr!
auto* Data = entity.try_get<FMyData>();
```

**Решение:** Используйте принадлежащий подсистеме `TArray` для передачи данных между системами.

### Случай 2: Внутри одной системы

```cpp
entity.obtain<FPendingDamage>().Hits.Add(Hit);  // Отложенная запись
auto* Pending = entity.try_get<FPendingDamage>(); // nullptr!
```

**Решение:** Отслеживайте в локальных переменных.

### Случай 3: Теги между сущностями

```cpp
// В callback .each() для сущности A
OtherEntity.add<FTagDead>();  // Отложено!
// Более поздняя система, запрашивающая FTagDead, не увидит его в этом тике
```

**Решение:** Выполняйте немедленные побочные эффекты (`SetBodyObjectLayer(DEBRIS)`) вместо того, чтобы полагаться на отложенные теги.
