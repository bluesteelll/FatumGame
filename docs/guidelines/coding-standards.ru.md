# Стандарты кодирования

Этот документ определяет стандарты кодирования для FatumGame. Каждый разработчик обязан следовать этим правилам. Они существуют для предотвращения багов, поддержания единообразия кодовой базы и обеспечения возможности код-ревью.

---

## Основные принципы

### Никаких обходных решений

!!! danger "НИКОГДА не используйте обходные решения"
    Находите и исправляйте **первопричину**. Обходное решение сегодня превращается в загадочный краш через месяц. Если исправление требует архитектурных изменений, эскалируйте архитектору -- не заклеивайте проблему пластырем.

```cpp
// НЕПРАВИЛЬНО: "Исправление" нулевого указателя добавлением проверки на null
if (Entity.try_get<FHealthInstance>())
{
    // тихо пропускаем если отсутствует
}

// ПРАВИЛЬНО: Понять ПОЧЕМУ компонент отсутствует и исправить путь спавна/настройки
check(Entity.has<FHealthInstance>());  // Краш в точке ошибки
const auto& Health = Entity.get<FHealthInstance>();
```

### Fail-Fast

!!! warning "Используйте `check()`, `ensure()`, `checkf()` для каждого предусловия"
    Краш в точке ошибки бесконечно лучше, чем тихая порча данных, которая проявится через три системы. НИКОГДА не возвращайтесь молча. НИКОГДА не используйте значения по умолчанию для сокрытия багов.

| Макрос | Поведение | Когда использовать |
|--------|----------|-------------------|
| `check(expr)` | Фатальный краш во всех сборках | Инвариант, который НИКОГДА не должен нарушаться |
| `checkf(expr, fmt, ...)` | Фатальный краш с сообщением | То же, но с контекстом для отладки |
| `ensure(expr)` | Assert в dev, продолжение в shipping | Восстановимое, но неожиданное состояние |
| `ensureMsgf(expr, fmt, ...)` | Assert с сообщением | То же, но с контекстом |

```cpp
void UFlecsArtillerySubsystem::BindEntityToBarrage(flecs::entity Entity, FSkeletonKey Key)
{
    checkf(Entity.is_alive(), TEXT("Cannot bind dead entity to Barrage key 0x%llX"), Key);
    checkf(Key != 0, TEXT("Cannot bind entity %llu to null BarrageKey"), Entity.id());

    FBLet* Prim = CachedBarrageDispatch->GetShapeRef(Key);
    checkf(Prim != nullptr, TEXT("No Barrage primitive for key 0x%llX"), Key);

    // ...
}
```

### Избегайте шаблонного кода

Выносите повторяющиеся паттерны в функции или шаблоны. Используйте возможности современного C++ для уменьшения шума:

- **`auto`** для типов итераторов и длинных возвращаемых типов шаблонов
- **Range-based `for`** вместо циклов по индексам
- **Структурные привязки** для пар и кортежей
- **Вспомогательные функции** для любого паттерна, используемого более двух раз

```cpp
// НЕПРАВИЛЬНО: Повторяющийся паттерн
if (auto* Health = Entity.try_get_mut<FHealthInstance>())
{
    Health->CurrentHP = FMath::Clamp(Health->CurrentHP - Damage, 0.f, Static.MaxHP);
    if (Health->CurrentHP <= 0.f)
    {
        Entity.add<FTagDead>();
    }
}

// ПРАВИЛЬНО: Вынесено в функцию, используемую всеми источниками урона
void ApplyDamageToEntity(flecs::entity Entity, float Damage, const FHealthStatic& Static);
```

---

## Правила USTRUCT

!!! danger "Без агрегатной инициализации с GENERATED_BODY()"
    UHT-генерируемые структуры имеют скрытые члены. Агрегатная инициализация `{value}` тихо присваивает не тому полю или не компилируется в зависимости от компилятора.

```cpp
// НЕПРАВИЛЬНО: Агрегатная инициализация
entity.set<FItemInstance>({ 5 });

// НЕПРАВИЛЬНО: Designated initializers на USTRUCT (ненадёжно с GENERATED_BODY)
entity.set<FItemInstance>({ .Count = 5 });

// ПРАВИЛЬНО: Именованное присваивание полей
FItemInstance Instance;
Instance.Count = 5;
entity.set<FItemInstance>(Instance);
```

!!! note "Инициализаторы членов по умолчанию -- нормально"
    Всегда указывайте значения по умолчанию в самом определении структуры:
    ```cpp
    USTRUCT()
    struct FHealthInstance
    {
        GENERATED_BODY()

        UPROPERTY()
        float CurrentHP = 0.f;

        UPROPERTY()
        float RegenAccumulator = 0.f;
    };
    ```

---

## Утверждение архитектуры

!!! warning "Нетривиальные изменения требуют утверждения"
    Перед реализацией любой фичи, затрагивающей несколько файлов или включающей проектные решения, представьте план архитектуры и получите явное подтверждение. Используйте режим планирования для любой такой фичи.

"Нетривиальное" включает:

- Новые ECS-компоненты или теги
- Новые системы или изменения порядка систем
- Новые примитивы потоков или межпоточные потоки данных
- Новые типы data asset или структуры профилей
- Изменения пайплайна спавна
- Изменения пайплайна столкновений

---

## Соглашения об именах

### ECS-компоненты

| Паттерн | Значение | Пример |
|---------|----------|--------|
| `FNameStatic` | Данные на **prefab** (общие для всех экземпляров этого типа) | `FHealthStatic`, `FWeaponStatic` |
| `FNameInstance` | Данные на **каждой сущности** (уникальные для экземпляра) | `FHealthInstance`, `FWeaponInstance` |
| `FTagName` | Тег нулевого размера (без данных, только маркер) | `FTagDead`, `FTagProjectile`, `FTagItem` |
| `FPendingName` | Данные в очереди, ожидающие обработки | `FPendingDamage` |
| `FNameData` | Общая структура данных (без разделения static/instance) | `FFragmentationData` |

### Файлы

| Паттерн | Использование | Пример |
|---------|-------------|--------|
| `FlecsCharacter_Aspect.cpp` | Частичные файлы реализации `AFlecsCharacter` | `FlecsCharacter_Combat.cpp`, `FlecsCharacter_UI.cpp` |
| `FlecsArtillerySubsystem_Domain.cpp` | Регистрация систем, сгруппированная по доменам | `FlecsArtillerySubsystem_Systems.cpp`, `FlecsArtillerySubsystem_Items.cpp` |
| `FlecsXxxLibrary.h/cpp` | Blueprint-библиотеки функций | `FlecsContainerLibrary`, `FlecsDamageLibrary` |
| `FlecsXxxComponents.h` | Заголовки доменных компонентов | `FlecsHealthComponents.h`, `FlecsWeaponComponents.h` |
| `FlecsXxxProfile.h` | Классы профилей data asset | `FlecsHealthProfile.h`, `FlecsWeaponProfile.h` |

### Общие соглашения C++

- **Конвенции UE**: Префикс `F` для структур, `U` для UObject, `A` для акторов, `E` для перечислений, `I` для интерфейсов
- **Спецификаторы UPROPERTY**: `EditAnywhere` для настраиваемых в редакторе, `BlueprintReadOnly` для доступа из BP, всегда включать `Category`
- **Булевые члены**: `bPrefixName` (напр., `bDestroyOnHit`, `bAreaDamage`)

---

## Порядок включений

Следуйте существующему паттерну кодовой базы:

```cpp
#include "MyHeader.h"           // 1. Соответствующий заголовок (ПЕРВЫЙ)

#include "OtherProjectHeader.h" // 2. Заголовки проекта
#include "Domain/Components.h"

#include "PluginHeader.h"       // 3. Заголовки плагинов (FlecsIntegration, FlecsBarrage, FlecsUI)

#include "Engine/Header.h"      // 4. Заголовки движка UE

#include <jolt/header.h>        // 5. Сторонние заголовки (редко)
```

!!! note "Предварительное объявление где возможно"
    Включайте только то, что реально используете. Если достаточно указателя или ссылки, предварительно объявите тип в заголовке и включите в `.cpp`.

---

## Комментарии

- **НЕ комментируйте очевидное**: `// Set health to max` не несёт ценности
- **Комментируйте неочевидное**: `// Must run before StepWorld because constraint anchors need committed positions`
- **Комментируйте обходные решения для багов движка** со ссылкой или объяснением, в чём баг
- **Комментируйте потоковые аспекты**: Какой поток это выполняет? Почему это atomic?

---

## Макросы UPROPERTY / UFUNCTION

```cpp
// Поле Data Asset (редактируемое в редакторе, видимое в BP)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health")
float MaxHP = 100.f;

// Только runtime поле (не редактируемое, не видимое)
UPROPERTY()
float CurrentHP = 0.f;

// Blueprint-вызываемая функция
UFUNCTION(BlueprintCallable, Category = "Damage", meta = (WorldContext = "WorldContext"))
static void ApplyDamage(UObject* WorldContext, FSkeletonKey TargetKey, float Amount);
```

---

## Рекомендации по производительности

- **Ноль аллокаций на горячих путях** -- предварительная аллокация, пулы, резервирование TArray
- **Кеш-дружественный доступ** -- контигуозная итерация компонентов (Flecs делает это естественно), избегайте pointer chasing
- **Без лишних копий** -- передавайте большие структуры по `const&`, используйте `MoveTemp()` для временных
- **Без O(N^2) где возможно O(N) или O(1)** -- используйте карты для поиска, не линейный перебор
- **Предпочитайте `TArray` перед `TMap`** для малых коллекций (локальность кеша побеждает overhead хеширования)
