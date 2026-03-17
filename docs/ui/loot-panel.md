# Loot Panel

The loot panel displays a side-by-side view of the player's inventory and an external container (chest, crate, loot drop). It opens when the player interacts with a container entity and allows drag-and-drop item transfer between the two.

## Overview

```mermaid
graph LR
    subgraph "UFlecsLootPanel"
        subgraph "Left Side"
            PG[Player Inventory<br/>UFlecsContainerGridWidget]
        end
        subgraph "Right Side"
            CG[External Container<br/>UFlecsContainerGridWidget]
        end
    end

    PG <-->|Drag-Drop Transfer| CG

    style PG fill:#3498db,color:#fff
    style CG fill:#2ecc71,color:#fff
```

---

## UFlecsLootPanel

`UFlecsLootPanel` extends `UFlecsUIPanel` (`UCommonActivatableWidget`) and manages two `UFlecsContainerGridWidget` instances side-by-side.

### Opening the Panel

The panel is opened through the interaction system when the player interacts with a `FTagContainer` entity:

```mermaid
sequenceDiagram
    participant P as Player
    participant IS as Interaction System
    participant LP as UFlecsLootPanel
    participant PM as Player Container Model
    participant CM as External Container Model

    P->>IS: Interact (E key)
    IS->>IS: Raycast -> Container Entity
    IS->>LP: OpenContainer(ContainerKey)
    LP->>PM: Bind Player Inventory
    LP->>CM: Create/Bind External Model
    LP->>LP: NativeOnActivated()
    Note over LP: Cursor visible, UI input
```

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `PlayerGridWidget` | `UFlecsContainerGridWidget*` | Left grid — player's inventory |
| `ExternalGridWidget` | `UFlecsContainerGridWidget*` | Right grid — external container |
| `PlayerContainerModel` | `UFlecsContainerModel*` | Model for player's items |
| `ExternalContainerModel` | `UFlecsContainerModel*` | Model for container's items |
| `ExternalContainerKey` | `FSkeletonKey` | BarrageKey of the opened container entity |

---

## Lifecycle

### Construction

Both grid widgets are built in `Initialize()`:

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

!!! danger "Build in Initialize(), NOT NativeConstruct()"
    Both grids must be created in `Initialize()`. By the time `NativeConstruct()` fires, CommonUI may have already triggered activation callbacks that reference these widgets.

### Opening a Container

```cpp
void UFlecsLootPanel::OpenContainer(FSkeletonKey ContainerKey)
{
    check(ContainerKey.IsValid());
    ExternalContainerKey = ContainerKey;

    // Bind player inventory (always available)
    PlayerGridWidget->BindModel(PlayerContainerModel);

    // Create model for external container
    ExternalContainerModel = UISubsystem->CreateContainerModel(ContainerKey);
    ExternalGridWidget->BindModel(ExternalContainerModel);

    // Activate the panel (CommonUI stack)
    ActivateWidget();
}
```

### Activation / Deactivation

```cpp
void UFlecsLootPanel::NativeOnActivated()
{
    Super::NativeOnActivated();

    // MUST set input state manually (CommonUI quirk)
    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->SetShowMouseCursor(true);
        PC->SetInputMode(FInputModeUIOnly());
    }
}

void UFlecsLootPanel::NativeOnDeactivated()
{
    Super::NativeOnDeactivated();

    // MUST restore FPS state manually (CommonUI quirk)
    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->SetShowMouseCursor(false);
        PC->SetInputMode(FInputModeGameOnly());
    }

    // Unbind external container
    ExternalGridWidget->UnbindModel();
    ExternalContainerKey = FSkeletonKey{};
}
```

!!! warning "Manual PC State in Both Callbacks"
    Due to CommonUI quirks (no ActionDomainTable reset, stale ActiveInputConfig), both `NativeOnActivated()` and `NativeOnDeactivated()` must manually configure the player controller. See [FlecsUI Plugin](../plugins/flecs-ui.md#commonui-input-quirks).

---

## Dual Grid Layout

```mermaid
graph TD
    subgraph "UFlecsLootPanel"
        direction LR
        subgraph "LeftPanel (UHorizontalBox Slot)"
            PH[Header: 'Inventory']
            PG[UFlecsContainerGridWidget<br/>Player Container<br/>e.g., 8x6 grid]
        end
        subgraph "RightPanel (UHorizontalBox Slot)"
            CH[Header: 'Container Name']
            CG[UFlecsContainerGridWidget<br/>External Container<br/>e.g., 4x4 grid]
        end
    end

    style PG fill:#3498db,color:#fff
    style CG fill:#2ecc71,color:#fff
```

The two grids can have **different dimensions**. The player's inventory might be 8x6 while a small chest is 4x4. Each grid independently manages its own slot widgets and occupancy mask.

---

## Item Transfer (Drag-Drop Between Grids)

The core interaction: dragging an item from one grid and dropping it onto the other.

### Transfer Flow

```mermaid
sequenceDiagram
    participant P as Player
    participant SG as Source Grid
    participant TG as Target Grid
    participant SM as Source Model
    participant TM as Target Model
    participant SIM as Sim Thread

    P->>SG: Drag item from slot
    SG->>SG: Detach item widget, create drag visual

    P->>TG: Drop on target slot
    TG->>TG: CanPlaceAt(Position, ItemSize)?

    alt Valid Placement
        TG->>SM: OptimisticRemove(Item)
        TG->>TM: OptimisticAdd(Item, Position)
        Note over SM,TM: UI updates immediately

        TG->>SIM: EnqueueCommand(TransferItem)
        Note over SIM: RemoveFromContainer(Source)<br/>AddToContainer(Target)

        alt Sim Validates OK
            Note over SIM: Committed
        else Sim Rejects
            Note over SIM: Next TTripleBuffer sync<br/>rolls back UI
        end
    else Invalid Placement
        TG->>SG: ReturnItemToOrigin()
        Note over SG: Snap back to original slot
    end
```

### Optimistic Updates

!!! info "Optimistic Pattern"
    Item transfers use the same optimistic update pattern as within-grid moves. The UI updates immediately on drop, and the simulation thread validates asynchronously. If the sim rejects the transfer (e.g., container became full from another source), the next `TTripleBuffer` sync automatically corrects the UI.

### Cross-Grid Validation

When dropping an item on the target grid:

1. **Bounds check** — Does the item fit within the target grid dimensions?
2. **Occupancy check** — Are all required cells in the target grid free?
3. **Weight check** — Does the target container have capacity for the item's weight?
4. **Count check** — Is the target container under its max item count?

The client performs checks 1 and 2 instantly (using the occupancy mask). Checks 3 and 4 are validated server-side (simulation thread) and may cause a rollback if they fail.

---

## Model Management

### Two Models, One Panel

The loot panel manages two independent `UFlecsContainerModel` instances:

| Model | Lifetime | Source |
|-------|----------|--------|
| Player container model | Persistent (as long as player exists) | Created at game start |
| External container model | Transient (panel open duration) | Created on `OpenContainer()`, released on close |

### GC Considerations

```mermaid
graph TD
    UIS[UFlecsUISubsystem] -->|GCRoots| PM[Player Model<br/>Persistent]
    UIS -->|GCRoots| EM[External Model<br/>Transient]

    LP[UFlecsLootPanel] -->|reference| PM
    LP -->|reference| EM

    style UIS fill:#e74c3c,color:#fff
    style PM fill:#3498db,color:#fff
    style EM fill:#2ecc71,color:#fff
```

!!! danger "External Model GC Root"
    The external container model must be added to `GCRoots` on creation and removed when the panel closes. Forgetting to root it causes garbage collection of a live model, leading to crashes when the grid tries to access item data.

---

## Closing the Panel

The panel can be closed by:

1. **Pressing Escape** — CommonUI deactivation
2. **Pressing the interact key again** — Toggle behavior
3. **Walking out of range** — Interaction system detects target lost

```mermaid
graph TD
    ESC[Escape Key] --> CLOSE[Close Panel]
    INT[Interact Key] --> CLOSE
    RANGE[Out of Range] --> CLOSE

    CLOSE --> DEACT[NativeOnDeactivated]
    DEACT --> UNBIND[Unbind External Model]
    DEACT --> RESTORE[Restore FPS Input]
    DEACT --> CLEAR[Clear ExternalContainerKey]

    style CLOSE fill:#e74c3c,color:#fff
```

---

## ECS Components Involved

| Component | Location | Role |
|-----------|----------|------|
| `FContainerStatic` | Prefab | Grid dimensions, max items, max weight |
| `FContainerInstance` | Instance | Current weight, count, owner entity ID |
| `FItemStaticData` | Prefab | Grid size, weight, max stack |
| `FItemInstance` | Instance | Current stack count |
| `FContainedIn` | Instance | Which container, grid position, slot index |
| `FTagContainer` | Tag | Marks entity as openable container |
| `FTagInteractable` | Tag | Marks entity for interaction raycast |
| `FInteractionStatic` | Prefab | Max range, single-use flag |

---

## Data Flow Summary

```mermaid
graph TB
    subgraph "Sim Thread"
        PC[Player Container<br/>Flecs Entity] -->|items| TB1[TTripleBuffer A]
        EC[External Container<br/>Flecs Entity] -->|items| TB2[TTripleBuffer B]
    end

    subgraph "Game Thread"
        TB1 -->|read| UIS[UFlecsUISubsystem]
        TB2 -->|read| UIS
        UIS --> PM[Player Model]
        UIS --> EM[External Model]
        PM --> PG[Player Grid Widget]
        EM --> EG[External Grid Widget]
        PG --> LP[UFlecsLootPanel]
        EG --> LP
    end

    subgraph "Player Actions"
        LP -->|drag-drop| CMD[EnqueueCommand]
        CMD --> PC
        CMD --> EC
    end

    style PC fill:#e74c3c,color:#fff
    style EC fill:#e74c3c,color:#fff
    style LP fill:#9b59b6,color:#fff
```
