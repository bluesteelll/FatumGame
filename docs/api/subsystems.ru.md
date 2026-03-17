# Подсистемы

> FatumGame использует четыре подсистемы UE для связи ECS-симуляции с игровым циклом UE.

---

## UFlecsArtillerySubsystem

**Тип:** `UTickableWorldSubsystem`
**Заголовок:** `Core/Public/FlecsArtillerySubsystem.h`

Центральный узел. Владеет потоком симуляции, миром Flecs и всеми межпоточными коммуникациями.

### Обязанности

- Запуск/остановка `FSimulationWorker`
- Регистрация компонентов и систем Flecs
- Создание и кеширование prefab сущностей
- Управление `FCharacterPhysBridge` для каждого персонажа
- Вызов `UFlecsRenderManager::UpdateTransforms()` на каждом game tick
- Чтение MPSC-очередей sim->game (спавн снарядов, спавн фрагментов, события выстрелов)
- Вычисление альфы интерполяции рендера
- Предоставление `EnqueueCommand()` для game->sim мутаций

### Ключевой API

| Метод | Поток | Описание |
|-------|-------|----------|
| `EnqueueCommand(TFunction<void()>)` | Game | Поставить мутацию в очередь для sim thread |
| `BindEntityToBarrage(Entity, Key)` | Sim | Создать двунаправленную привязку |
| `UnbindEntityFromBarrage(Entity)` | Sim | Удалить привязку |
| `GetEntityForBarrageKey(Key)` | Любой | Разрешить физический ключ -> сущность Flecs |
| `GetOrCreateEntityPrefab(Def)` | Sim | Получить/создать prefab для определения |
| `RegisterCharacter(Character)` | Game | Создать мост персонажа |
| `UnregisterCharacter(Character)` | Game | Удалить мост персонажа |

### Зависимости инициализации

```cpp
void Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UBarrageDispatch>();  // ДО Super!
    Super::Initialize(Collection);
}
```

---

## UFlecsRenderManager

**Тип:** `UWorldSubsystem`
**Заголовок:** `Rendering/Public/FlecsRenderManager.h`

Управляет всеми ISM-компонентами для ECS-сущностей. Не тикает самостоятельно -- управляется из `UFlecsArtillerySubsystem::Tick()`.

### Обязанности

- Создание/управление ISM-компонентами, сгруппированными по (меш, материал)
- Отслеживание состояния трансформа каждой сущности (пред./текущая позиция для интерполяции)
- Интерполяция и обновление трансформов ISM на каждом кадре
- Добавление/удаление экземпляров с swap-and-pop

### Ключевой API

| Метод | Описание |
|-------|----------|
| `AddInstance(Key, Mesh, Material, Transform)` | Зарегистрировать визуал новой сущности |
| `RemoveInstance(Key)` | Поставить экземпляр в очередь на удаление |
| `UpdateTransforms(Alpha, SimTick)` | Интерполировать и обновить все ISM-экземпляры |
| `GetOrCreateISM(Mesh, Material)` | Получить/создать ISM-компонент для пары меш+материал |

---

## UFlecsUISubsystem

**Тип:** `UTickableWorldSubsystem`
**Заголовок:** `UI/Public/FlecsUISubsystem.h`

Мост между состоянием ECS-контейнеров и UI-виджетами. Управляет моделями, тройными буферами и состоянием с подсчётом ссылок.

### Обязанности

- Фабрика для `UFlecsContainerModel` и `UFlecsValueModel`
- Поддержание `FContainerSharedState` для каждого контейнера (тройной буфер + SimVersion atomic + MPSC-очередь OpResults)
- Жизненный цикл моделей с подсчётом ссылок (модели уничтожаются, когда последнее view отключается)
- Управление GC root для `UObject`-моделей

### Ключевой API

| Метод | Описание |
|-------|----------|
| `GetOrCreateContainerModel(ContainerEntityId)` | Получить/создать модель для контейнера |
| `GetOrCreateValueModel(EntityId, Channel)` | Получить/создать модель скалярного значения |
| `ReleaseContainerModel(ContainerEntityId)` | Уменьшить счётчик ссылок |

### Безопасность GC

Модели -- обычные `UObject`, хранимые в структурах (не `UPROPERTY`). Для предотвращения сборки мусора:

```cpp
UPROPERTY()
TArray<TObjectPtr<UObject>> GCRoots;
```

---

## UFlecsMessageSubsystem

**Тип:** `UGameInstanceSubsystem`
**Заголовок:** `UI/Public/FlecsMessageSubsystem.h`

Типизированная шина сообщений publish/subscribe для UI-коммуникации.

### Обязанности

- Типобезопасная публикация сообщений из любого потока
- Регистрация слушателей с `FMessageListenerHandle` для управления жизненным циклом
- Фильтрация по каналам (здоровье, боеприпасы, перезарядка, взаимодействие и т.д.)

### Ключевой API

```cpp
// Публикация (любой поток)
MessageSubsystem->Publish(FUIHealthMessage{ .CurrentHP = 75.f, .MaxHP = 100.f });

// Подписка (game thread, обычно в Initialize виджета)
Handle = MessageSubsystem->Subscribe<FUIHealthMessage>(
    [this](const FUIHealthMessage& Msg)
    {
        UpdateHealthBar(Msg.CurrentHP, Msg.MaxHP);
    });

// Автоотписка при уничтожении handle
```

### Типы сообщений

| Сообщение | Поля | Публикуется |
|-----------|------|------------|
| `FUIHealthMessage` | CurrentHP, MaxHP | Чтение SimStateCache |
| `FUIDeathMessage` | EntityId | DeadEntityCleanupSystem |
| `FUIAmmoMessage` | CurrentMag, MagSize, Reserve | Чтение SimStateCache |
| `FUIReloadMessage` | bStarted, bComplete | WeaponReloadSystem |
| `FUIInteractionMessage` | PromptText, bHasTarget | Трассировка взаимодействия |
| `FUIHoldProgressMessage` | Progress [0,1] | Тик удержания взаимодействия |
| `FUIInteractionStateMessage` | NewState | Переход автомата состояний |
| `FResourceBarData` | ResourceType, Current, Max, Percent | Чтение SimStateCache |
