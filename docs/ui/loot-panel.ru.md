# Панель лута

Панель лута отображает вид "бок-о-бок" инвентаря игрока и внешнего контейнера (сундук, ящик, лут-дроп). Открывается при взаимодействии игрока с сущностью-контейнером и позволяет drag-and-drop перенос предметов между ними.

## Обзор

```mermaid
graph LR
    subgraph "UFlecsLootPanel"
        subgraph "Левая сторона"
            PG[Инвентарь игрока<br/>UFlecsContainerGridWidget]
        end
        subgraph "Правая сторона"
            CG[Внешний контейнер<br/>UFlecsContainerGridWidget]
        end
    end

    PG <-->|Drag-Drop перенос| CG

    style PG fill:#3498db,color:#fff
    style CG fill:#2ecc71,color:#fff
```

---

## UFlecsLootPanel

`UFlecsLootPanel` расширяет `UFlecsUIPanel` (`UCommonActivatableWidget`) и управляет двумя экземплярами `UFlecsContainerGridWidget`, размещёнными бок-о-бок.

### Открытие панели

Панель открывается через систему взаимодействия, когда игрок взаимодействует с сущностью `FTagContainer`:

```mermaid
sequenceDiagram
    participant P as Игрок
    participant IS as Система взаимодействия
    participant LP as UFlecsLootPanel
    participant PM as Модель контейнера игрока
    participant CM as Модель внешнего контейнера

    P->>IS: Взаимодействие (клавиша E)
    IS->>IS: Рейкаст -> Сущность-контейнер
    IS->>LP: OpenContainer(ContainerKey)
    LP->>PM: Привязать инвентарь игрока
    LP->>CM: Создать/привязать внешнюю модель
    LP->>LP: NativeOnActivated()
    Note over LP: Курсор видим, UI-ввод
```

### Ключевые свойства

| Свойство | Тип | Описание |
|----------|-----|----------|
| `PlayerGridWidget` | `UFlecsContainerGridWidget*` | Левая сетка -- инвентарь игрока |
| `ExternalGridWidget` | `UFlecsContainerGridWidget*` | Правая сетка -- внешний контейнер |
| `PlayerContainerModel` | `UFlecsContainerModel*` | Модель предметов игрока |
| `ExternalContainerModel` | `UFlecsContainerModel*` | Модель предметов контейнера |
| `ExternalContainerKey` | `FSkeletonKey` | BarrageKey открытой сущности-контейнера |

---

## Жизненный цикл

### Построение

Оба виджета сетки строятся в `Initialize()`:

```cpp
void UFlecsLootPanel::Initialize()
{
    Super::Initialize();

    PlayerGridWidget = CreateWidget<UFlecsContainerGridWidget>(GetOwningPlayer());
    check(PlayerGridWidget);
    LeftPanel->AddChild(PlayerGridWidget);

    ExternalGridWidget = CreateWidget<UFlecsContainerGridWidget>(GetOwningPlayer());
    check(ExternalGridWidget);
    RightPanel->AddChild(ExternalGridWidget);
}
```

!!! danger "Строить в Initialize(), НЕ в NativeConstruct()"
    Обе сетки должны быть созданы в `Initialize()`. К моменту вызова `NativeConstruct()` CommonUI мог уже запустить callback-и активации, ссылающиеся на эти виджеты.

### Открытие контейнера

```cpp
void UFlecsLootPanel::OpenContainer(FSkeletonKey ContainerKey)
{
    check(ContainerKey.IsValid());
    ExternalContainerKey = ContainerKey;

    // Привязать инвентарь игрока (всегда доступен)
    PlayerGridWidget->BindModel(PlayerContainerModel);

    // Создать модель для внешнего контейнера
    ExternalContainerModel = UISubsystem->CreateContainerModel(ContainerKey);
    ExternalGridWidget->BindModel(ExternalContainerModel);

    // Активировать панель (стек CommonUI)
    ActivateWidget();
}
```

### Активация / Деактивация

```cpp
void UFlecsLootPanel::NativeOnActivated()
{
    Super::NativeOnActivated();

    // ОБЯЗАТЕЛЬНО установить состояние ввода вручную (особенность CommonUI)
    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->SetShowMouseCursor(true);
        PC->SetInputMode(FInputModeUIOnly());
    }
}

void UFlecsLootPanel::NativeOnDeactivated()
{
    Super::NativeOnDeactivated();

    // ОБЯЗАТЕЛЬНО восстановить FPS-состояние вручную (особенность CommonUI)
    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->SetShowMouseCursor(false);
        PC->SetInputMode(FInputModeGameOnly());
    }

    // Отвязать внешний контейнер
    ExternalGridWidget->UnbindModel();
    ExternalContainerKey = FSkeletonKey{};
}
```

!!! warning "Ручное состояние PC в обоих callback-ах"
    Из-за особенностей CommonUI (нет сброса ActionDomainTable, устаревший ActiveInputConfig) и `NativeOnActivated()`, и `NativeOnDeactivated()` должны вручную настраивать контроллер игрока. См. [плагин FlecsUI](../plugins/flecs-ui.md#commonui-input-quirks).

---

## Двойная сетка

```mermaid
graph TD
    subgraph "UFlecsLootPanel"
        direction LR
        subgraph "LeftPanel (слот UHorizontalBox)"
            PH[Заголовок: 'Инвентарь']
            PG[UFlecsContainerGridWidget<br/>Контейнер игрока<br/>напр., сетка 8x6]
        end
        subgraph "RightPanel (слот UHorizontalBox)"
            CH[Заголовок: 'Название контейнера']
            CG[UFlecsContainerGridWidget<br/>Внешний контейнер<br/>напр., сетка 4x4]
        end
    end

    style PG fill:#3498db,color:#fff
    style CG fill:#2ecc71,color:#fff
```

Две сетки могут иметь **разные размеры**. Инвентарь игрока может быть 8x6, а маленький сундук -- 4x4. Каждая сетка независимо управляет своими виджетами слотов и маской занятости.

---

## Перенос предметов (Drag-Drop между сетками)

Основное взаимодействие: перетаскивание предмета из одной сетки и бросание на другую.

### Поток переноса

```mermaid
sequenceDiagram
    participant P as Игрок
    participant SG as Исходная сетка
    participant TG as Целевая сетка
    participant SM as Исходная модель
    participant TM as Целевая модель
    participant SIM as Sim Thread

    P->>SG: Перетащить предмет из слота
    SG->>SG: Отсоединить виджет предмета, создать визуал перетаскивания

    P->>TG: Бросить на целевой слот
    TG->>TG: CanPlaceAt(Position, ItemSize)?

    alt Валидное размещение
        TG->>SM: OptimisticRemove(Item)
        TG->>TM: OptimisticAdd(Item, Position)
        Note over SM,TM: UI обновляется немедленно

        TG->>SIM: EnqueueCommand(TransferItem)
        Note over SIM: RemoveFromContainer(Source)<br/>AddToContainer(Target)

        alt Sim валидирует ОК
            Note over SIM: Зафиксировано
        else Sim отклоняет
            Note over SIM: Следующая синхронизация TTripleBuffer<br/>откатывает UI
        end
    else Невалидное размещение
        TG->>SG: ReturnItemToOrigin()
        Note over SG: Вернуть в исходный слот
    end
```

### Оптимистичные обновления

!!! info "Оптимистичный паттерн"
    Переносы предметов используют тот же паттерн оптимистичного обновления, что и перемещения внутри сетки. UI обновляется немедленно при бросании, а поток симуляции валидирует асинхронно. Если sim отклоняет перенос (например, контейнер заполнился из другого источника), следующая синхронизация `TTripleBuffer` автоматически исправляет UI.

### Кросс-сеточная валидация

При бросании предмета на целевую сетку:

1. **Проверка границ** -- помещается ли предмет в размеры целевой сетки?
2. **Проверка занятости** -- все ли необходимые ячейки в целевой сетке свободны?
3. **Проверка веса** -- есть ли у целевого контейнера ёмкость для веса предмета?
4. **Проверка количества** -- не превышен ли лимит предметов в целевом контейнере?

Клиент выполняет проверки 1 и 2 мгновенно (используя маску занятости). Проверки 3 и 4 валидируются на стороне сервера (поток симуляции) и могут вызвать откат при ошибке.

---

## Управление моделями

### Две модели, одна панель

Панель лута управляет двумя независимыми экземплярами `UFlecsContainerModel`:

| Модель | Время жизни | Источник |
|--------|------------|---------|
| Модель контейнера игрока | Постоянная (пока существует игрок) | Создаётся при старте игры |
| Модель внешнего контейнера | Временная (длительность открытия панели) | Создаётся при `OpenContainer()`, освобождается при закрытии |

### Соображения о GC

```mermaid
graph TD
    UIS[UFlecsUISubsystem] -->|GCRoots| PM[Модель игрока<br/>Постоянная]
    UIS -->|GCRoots| EM[Внешняя модель<br/>Временная]

    LP[UFlecsLootPanel] -->|ссылка| PM
    LP -->|ссылка| EM

    style UIS fill:#e74c3c,color:#fff
    style PM fill:#3498db,color:#fff
    style EM fill:#2ecc71,color:#fff
```

!!! danger "GC Root внешней модели"
    Внешняя модель контейнера должна быть добавлена в `GCRoots` при создании и удалена при закрытии панели. Если забыть зарутить её, произойдёт сборка мусора живой модели, что приведёт к крашу при попытке сетки обратиться к данным предметов.

---

## Закрытие панели

Панель может быть закрыта:

1. **Нажатием Escape** -- деактивация CommonUI
2. **Повторным нажатием клавиши взаимодействия** -- переключающее поведение
3. **Выходом за пределы дальности** -- система взаимодействия обнаруживает потерю цели

```mermaid
graph TD
    ESC[Клавиша Escape] --> CLOSE[Закрыть панель]
    INT[Клавиша взаимодействия] --> CLOSE
    RANGE[Вне дальности] --> CLOSE

    CLOSE --> DEACT[NativeOnDeactivated]
    DEACT --> UNBIND[Отвязать внешнюю модель]
    DEACT --> RESTORE[Восстановить FPS-ввод]
    DEACT --> CLEAR[Очистить ExternalContainerKey]

    style CLOSE fill:#e74c3c,color:#fff
```

---

## Задействованные ECS-компоненты

| Компонент | Расположение | Роль |
|-----------|-------------|------|
| `FContainerStatic` | Prefab | Размеры сетки, макс. предметов, макс. вес |
| `FContainerInstance` | Экземпляр | Текущий вес, количество, ID сущности-владельца |
| `FItemStaticData` | Prefab | Размер в сетке, вес, макс. стак |
| `FItemInstance` | Экземпляр | Текущий размер стака |
| `FContainedIn` | Экземпляр | В каком контейнере, позиция в сетке, индекс слота |
| `FTagContainer` | Тег | Помечает сущность как открываемый контейнер |
| `FTagInteractable` | Тег | Помечает сущность для рейкаста взаимодействия |
| `FInteractionStatic` | Prefab | Макс. дальность, флаг одноразовости |

---

## Сводка потока данных

```mermaid
graph TB
    subgraph "Sim Thread"
        PC[Контейнер игрока<br/>Flecs Entity] -->|предметы| TB1[TTripleBuffer A]
        EC[Внешний контейнер<br/>Flecs Entity] -->|предметы| TB2[TTripleBuffer B]
    end

    subgraph "Game Thread"
        TB1 -->|чтение| UIS[UFlecsUISubsystem]
        TB2 -->|чтение| UIS
        UIS --> PM[Модель игрока]
        UIS --> EM[Внешняя модель]
        PM --> PG[Виджет сетки игрока]
        EM --> EG[Виджет внешней сетки]
        PG --> LP[UFlecsLootPanel]
        EG --> LP
    end

    subgraph "Действия игрока"
        LP -->|drag-drop| CMD[EnqueueCommand]
        CMD --> PC
        CMD --> EC
    end

    style PC fill:#e74c3c,color:#fff
    style EC fill:#e74c3c,color:#fff
    style LP fill:#9b59b6,color:#fff
```
