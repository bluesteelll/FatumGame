# Subsystems

> FatumGame uses four UE subsystems to bridge the ECS simulation with UE's game loop.

---

## UFlecsArtillerySubsystem

**Type:** `UTickableWorldSubsystem`
**Header:** `Core/Public/FlecsArtillerySubsystem.h`

The central hub. Owns the simulation thread, Flecs world, and all cross-thread communication.

### Responsibilities

- Start/stop `FSimulationWorker`
- Register Flecs components and systems
- Create and cache entity prefabs
- Manage `FCharacterPhysBridge` per character
- Drive `UFlecsRenderManager::UpdateTransforms()` each game tick
- Drain sim→game MPSC queues (projectile spawns, fragment spawns, shot events)
- Compute render interpolation alpha
- Provide `EnqueueCommand()` for game→sim mutations

### Key API

| Method | Thread | Description |
|--------|--------|-------------|
| `EnqueueCommand(TFunction<void()>)` | Game | Queue mutation for sim thread |
| `BindEntityToBarrage(Entity, Key)` | Sim | Create bidirectional binding |
| `UnbindEntityFromBarrage(Entity)` | Sim | Remove binding |
| `GetEntityForBarrageKey(Key)` | Any | Resolve physics key → Flecs entity |
| `GetOrCreateEntityPrefab(Def)` | Sim | Get/create prefab for definition |
| `RegisterCharacter(Character)` | Game | Create character bridge |
| `UnregisterCharacter(Character)` | Game | Remove character bridge |

### Initialization Dependencies

```cpp
void Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UBarrageDispatch>();  // BEFORE Super!
    Super::Initialize(Collection);
}
```

---

## UFlecsRenderManager

**Type:** `UWorldSubsystem`
**Header:** `Rendering/Public/FlecsRenderManager.h`

Manages all ISM components for ECS entities. Not self-ticking — driven by `UFlecsArtillerySubsystem::Tick()`.

### Responsibilities

- Create/manage ISM components grouped by (mesh, material)
- Track per-entity transform state (prev/curr position for interpolation)
- Interpolate and update ISM transforms each frame
- Handle instance addition/removal with swap-and-pop

### Key API

| Method | Description |
|--------|-------------|
| `AddInstance(Key, Mesh, Material, Transform)` | Register new entity visual |
| `RemoveInstance(Key)` | Queue instance removal |
| `UpdateTransforms(Alpha, SimTick)` | Interpolate and update all ISM instances |
| `GetOrCreateISM(Mesh, Material)` | Get/create ISM component for mesh+material pair |

---

## UFlecsUISubsystem

**Type:** `UTickableWorldSubsystem`
**Header:** `UI/Public/FlecsUISubsystem.h`

Bridge between ECS container state and UI widgets. Manages models, triple buffers, and ref-counted state.

### Responsibilities

- Factory for `UFlecsContainerModel` and `UFlecsValueModel`
- Maintain `FContainerSharedState` per container (triple buffer + SimVersion atomic + OpResults MPSC queue)
- Ref-counted model lifecycle (models destroyed when last view disconnects)
- GC root management for model `UObject`s

### Key API

| Method | Description |
|--------|-------------|
| `GetOrCreateContainerModel(ContainerEntityId)` | Get/create model for container |
| `GetOrCreateValueModel(EntityId, Channel)` | Get/create scalar value model |
| `ReleaseContainerModel(ContainerEntityId)` | Decrement ref count |

### GC Safety

Models are plain `UObject`s stored in structs (not `UPROPERTY`). To prevent garbage collection:

```cpp
UPROPERTY()
TArray<TObjectPtr<UObject>> GCRoots;
```

---

## UFlecsMessageSubsystem

**Type:** `UGameInstanceSubsystem`
**Header:** `UI/Public/FlecsMessageSubsystem.h`

Typed publish/subscribe message bus for UI communication.

### Responsibilities

- Type-safe message publishing from any thread
- Listener registration with `FMessageListenerHandle` for lifecycle management
- Channel-based filtering (health, ammo, reload, interaction, etc.)

### Key API

```cpp
// Publish (any thread)
MessageSubsystem->Publish(FUIHealthMessage{ .CurrentHP = 75.f, .MaxHP = 100.f });

// Subscribe (game thread, typically in widget Initialize)
Handle = MessageSubsystem->Subscribe<FUIHealthMessage>(
    [this](const FUIHealthMessage& Msg)
    {
        UpdateHealthBar(Msg.CurrentHP, Msg.MaxHP);
    });

// Auto-unsubscribe when handle destroyed
```

### Message Types

| Message | Fields | Published By |
|---------|--------|-------------|
| `FUIHealthMessage` | CurrentHP, MaxHP | SimStateCache read |
| `FUIDeathMessage` | EntityId | DeadEntityCleanupSystem |
| `FUIAmmoMessage` | CurrentMag, MagSize, Reserve | SimStateCache read |
| `FUIReloadMessage` | bStarted, bComplete | WeaponReloadSystem |
| `FUIInteractionMessage` | PromptText, bHasTarget | Interaction trace |
| `FUIHoldProgressMessage` | Progress [0,1] | Hold interaction tick |
| `FUIInteractionStateMessage` | NewState | State machine transition |
| `FResourceBarData` | ResourceType, Current, Max, Percent | SimStateCache read |
