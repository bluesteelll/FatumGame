# Plan: UI Event System + UMG HUD для FatumGame

## Context

FatumGame имеет полную ECS симуляцию (Flecs на sim thread, Barrage physics) с health/weapons/interaction — но ноль UI инфраструктуры. Нужна:
1. **Своя message-система** (аналог Lyra GameplayMessageRouter, ~200 строк) — pub/sub через GameplayTag + USTRUCT
2. **MPSC мост** sim thread → game thread (паттерн уже есть: PendingProjectileSpawns)
3. **UMG HUD** с event-driven обновлениями

## Архитектура

```
Sim Thread (Flecs systems):
  DamageObserver     → MPSC Queue (FUIEvent)
  WeaponFireSystem   → MPSC Queue (FUIEvent)
  WeaponReloadSystem → MPSC Queue (FUIEvent)
  DeathCheckSystem   → MPSC Queue (FUIEvent)

Game Thread (UFlecsUISubsystem::Tick):
  drain MPSC queue → BroadcastMessage(GameplayTag, USTRUCT)
  poll interaction (10Hz) → BroadcastMessage(...)

HUD Widget (subscriber):
  RegisterListener<FHealthMessage>(TAG, callback) → update UI
```

---

## Step 1: Build.cs

**File:** `Source/FatumGame/FatumGame.Build.cs`

- Add `"UMG"`, `"Slate"`, `"SlateCore"` to PublicDependencyModuleNames
- Add `"FatumGame/Flecs/UI"` to PublicIncludePaths

## Step 2: Message System (свой аналог GameplayMessageRouter)

**NEW:** `Source/FatumGame/Flecs/UI/FlecsMessageSystem.h`

Lightweight pub/sub: ~200 строк, GameplayTag каналы, template USTRUCT payload.

```
UFlecsMessageSubsystem : UGameInstanceSubsystem
├── BroadcastMessage<T>(FGameplayTag Channel, const T& Message)
│   → iterate ListenerMap[Tag] → type check → invoke callbacks
│
├── RegisterListener<T>(FGameplayTag, Callback) → FMessageListenerHandle
├── RegisterListener<T>(FGameplayTag, UObject*, MemberFunc) → FMessageListenerHandle (weak ptr)
├── UnregisterListener(FMessageListenerHandle)
│
├── Internal:
│   TMap<FGameplayTag, FChannelListeners> ListenerMap
│   struct FListenerData { TFunction<void(Tag, UScriptStruct*, void*)>, HandleID, StructType }
│
└── K2_BroadcastMessage() — Blueprint CustomThunk wrapper
```

**Отличия от Lyra:**
- Нет PartialMatch (упрощение — не нужен tag hierarchy walk)
- Нет AsyncAction (добавим позже если нужно)
- ~200 строк вместо ~400

**API:**
```cpp
// Broadcast (game thread only)
auto& Msg = UFlecsMessageSubsystem::Get(this);
Msg.BroadcastMessage(TAG_HUD_Health, FHealthMessage{EntityId, 75.f, 100.f});

// Listen (auto weak-ptr check)
Handle = Msg.RegisterListener<FHealthMessage>(
    TAG_HUD_Health, this, &UMyWidget::OnHealth);

// Cleanup
Handle.Unregister();
```

## Step 3: UI Event Structs

**NEW:** `Source/FatumGame/Flecs/UI/FlecsUIMessages.h`

```cpp
// GameplayTag channels (define in code or NativeGameplayTags)
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Health,      "UI.Health");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Death,       "UI.Death");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Ammo,        "UI.Weapon.Ammo");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Reload,      "UI.Weapon.Reload");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Interaction, "UI.Interaction");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_WeaponEquip, "UI.Weapon.Equip");

USTRUCT(BlueprintType)
struct FUIHealthMessage
{
    GENERATED_BODY()
    int64 EntityId = 0;
    float CurrentHP = 0.f;
    float MaxHP = 0.f;
};

USTRUCT(BlueprintType)
struct FUIDeathMessage
{
    GENERATED_BODY()
    int64 EntityId = 0;
    int64 KillerEntityId = 0;
};

USTRUCT(BlueprintType)
struct FUIAmmoMessage
{
    GENERATED_BODY()
    int64 WeaponEntityId = 0;
    int32 CurrentAmmo = 0;
    int32 MagazineSize = 0;
    int32 ReserveAmmo = 0;
};

USTRUCT(BlueprintType)
struct FUIReloadMessage
{
    GENERATED_BODY()
    int64 WeaponEntityId = 0;
    bool bStarted = true;  // true=started, false=finished
    int32 NewAmmo = 0;     // valid when bStarted=false
    int32 MagazineSize = 0;
};

USTRUCT(BlueprintType)
struct FUIInteractionMessage
{
    GENERATED_BODY()
    bool bHasTarget = false;
    FText Prompt;
    FSkeletonKey TargetKey;
};
```

## Step 4: MPSC Event Queue (sim thread → game thread)

**NEW:** `Source/FatumGame/Flecs/UI/FlecsUIBridge.h`

Lightweight queue struct для кросс-поточной передачи:

```cpp
// Generic UI event for MPSC queue (POD, no UObject refs)
struct FPendingUIEvent
{
    FGameplayTag Channel;
    // Union-like storage for different message types
    uint8 PayloadType;  // enum index
    int64 EntityId = 0;
    float Float1 = 0.f;
    float Float2 = 0.f;
    int32 Int1 = 0;
    int32 Int2 = 0;
    int32 Int3 = 0;
    bool Bool1 = false;
    FText Text1;        // for interaction prompt
    FSkeletonKey Key1;  // for interaction target
};
```

## Step 5: UFlecsUISubsystem — event hub

**NEW:** `Source/FatumGame/Flecs/UI/FlecsUISubsystem.h/.cpp`

```
UFlecsUISubsystem : UTickableWorldSubsystem
├── Initialize(): InitializeDependency<UFlecsArtillerySubsystem>
├── OnWorldBeginPlay():
│   → Register Flecs health observer (via EnqueueCommand)
│   → Start 10Hz interaction timer
├── Tick():
│   → Drain PendingUIEvents MPSC queue
│   → Convert FPendingUIEvent → typed USTRUCT
│   → UFlecsMessageSubsystem::BroadcastMessage(Tag, Struct)
├── Deinitialize(): cleanup
│
├── MPSC Queue:
│   TQueue<FPendingUIEvent, EQueueMode::Mpsc> PendingUIEvents
│   void EnqueueUIEvent(FPendingUIEvent&&)  // sim thread safe
│
├── Queries (BlueprintPure, game-thread safe):
│   float GetHealthPercent(int64 EntityId)
│   void GetAmmoInfo(int64 WeaponId, ...)
│   bool IsReloading(int64 WeaponId)
│   float GetReloadProgress(int64 WeaponId)
│   FSkeletonKey GetInteractionTarget()
│   FText GetInteractionPrompt()
│
├── Interaction (moved from AFlecsCharacter):
│   PerformInteractionTrace() — 10Hz timer
│   FSkeletonKey CurrentInteractionTarget
│   FText CachedInteractionPrompt
│
└── static UFlecsUISubsystem* SelfPtr  // for sim thread access
```

## Step 6: Register Flecs Observers

В `OnWorldBeginPlay()`, через EnqueueCommand:

**Health Observer** — fires on `FHealthInstance` OnSet:
```cpp
World.observer<FHealthInstance, FHealthStatic>("UIHealthObserver")
    .event(flecs::OnSet)
    .each([this](flecs::entity E, const FHealthInstance& H, const FHealthStatic& S) {
        FPendingUIEvent Evt;
        Evt.Channel = TAG_UI_Health;
        Evt.EntityId = static_cast<int64>(E.id());
        Evt.Float1 = H.CurrentHP;
        Evt.Float2 = S.MaxHP;
        PendingUIEvents.Enqueue(MoveTemp(Evt));
    });
```

**Death** — push from DeathCheckSystem when adding FTagDead.

## Step 7: Integrate with existing Flecs systems

**File:** `FlecsArtillerySubsystem_Systems.cpp`

В WeaponFireSystem (после декремента ammo):
```cpp
if (auto* UI = UFlecsUISubsystem::SelfPtr)
{
    FPendingUIEvent Evt;
    Evt.Channel = TAG_UI_Ammo;
    Evt.EntityId = static_cast<int64>(Entity.id());
    Evt.Int1 = Instance.CurrentAmmo;
    Evt.Int2 = Static.MagazineSize;
    Evt.Int3 = Instance.ReserveAmmo;
    UI->EnqueueUIEvent(MoveTemp(Evt));
}
```

В WeaponReloadSystem (начало/конец reload) — аналогично с TAG_UI_Reload.

В DeathCheckSystem (при добавлении FTagDead) — TAG_UI_Death.

## Step 8: GetWeaponReloadProgress

**File:** `FlecsWeaponLibrary.h/.cpp`

Add: `static float GetWeaponReloadProgress(UObject*, int64 WeaponId)` — returns 0.0–1.0 (-1.0 if not reloading). Reads `FWeaponInstance.ReloadTimeRemaining / FWeaponStatic.ReloadTime`.

## Step 9: Move interaction from AFlecsCharacter

Перенести `PerformInteractionTrace()` в UFlecsUISubsystem:
- 10Hz timer, Barrage SphereCast, cache target + prompt
- Broadcast `TAG_UI_Interaction` при смене таргета
- AFlecsCharacter::OnSpawnItem читает target из subsystem

**File:** `FlecsCharacter.h/.cpp` — удалить:
- `PerformInteractionTrace()`, `InteractionTraceTimerHandle`
- `CurrentInteractionTarget`, `CachedInteractionPrompt`
- `CheckHealthChanges()` из Tick (теперь observer)

Оставить: `TryInteract()`, `GetInteractionTarget()` (делегирует в subsystem)

## Step 10: UFlecsHUDWidget — base widget class

**NEW:** `Source/FatumGame/Flecs/UI/FlecsHUDWidget.h/.cpp`

```
UFlecsHUDWidget : UUserWidget (Blueprintable, Abstract)
├── NativeConstruct():
│   → Get UFlecsMessageSubsystem
│   → RegisterListener<FUIHealthMessage>(TAG_UI_Health, ...)
│   → RegisterListener<FUIAmmoMessage>(TAG_UI_Ammo, ...)
│   → RegisterListener<FUIReloadMessage>(TAG_UI_Reload, ...)
│   → RegisterListener<FUIInteractionMessage>(TAG_UI_Interaction, ...)
│   → RegisterListener<FUIDeathMessage>(TAG_UI_Death, ...)
│
├── NativeDestruct(): Unregister all handles
│
├── Private handlers → forward to BlueprintImplementableEvent:
│   HandleHealth(Tag, FUIHealthMessage)  → OnHealthChanged(Current, Max, Percent)
│   HandleAmmo(Tag, FUIAmmoMessage)      → OnAmmoChanged(Current, Mag, Reserve)
│   HandleReload(Tag, FUIReloadMessage)  → OnReloadStarted() / OnReloadFinished()
│   HandleInteraction(Tag, ...)          → OnInteractionChanged(bHas, Prompt)
│   HandleDeath(Tag, FUIDeathMessage)    → OnPlayerDeath()
│
├── BlueprintImplementableEvent (override in WBP_MainHUD):
│   OnHealthChanged(float Current, float Max, float Percent)
│   OnPlayerDeath()
│   OnAmmoChanged(int32 Current, int32 Magazine, int32 Reserve)
│   OnReloadStarted()
│   OnReloadFinished(int32 NewAmmo, int32 Magazine)
│   OnInteractionChanged(bool bHasTarget, FText Prompt)
│
└── BlueprintPure queries (delegate to UFlecsUISubsystem):
    GetHealthPercent(), GetAmmoInfo(), IsReloading(),
    GetReloadProgress(), HasInteractionTarget(), GetInteractionPrompt()
```

Фильтрация: HandleHealth проверяет EntityId == local player entity.

## Step 11: Wire to AFlecsCharacter (minimal)

**File:** `FlecsCharacter.h/.cpp`

```cpp
UPROPERTY(EditAnywhere, Category = "HUD")
TSubclassOf<UFlecsHUDWidget> HUDWidgetClass;

UPROPERTY(BlueprintReadOnly, Category = "HUD")
TObjectPtr<UFlecsHUDWidget> HUDWidget;
```

- BeginPlay (после weapon spawn): `CreateWidget → InitializeHUD → AddToViewport`
- EndPlay: `RemoveFromParent`
- Убрать CheckHealthChanges, PerformInteractionTrace, связанные cached vars
- Оставить OnDamageTaken/OnHealed/OnDeath BlueprintImplementableEvents (backward compat)

## Step 12: WBP_MainHUD (UMG Designer, в редакторе)

Blueprint Widget, parent: UFlecsHUDWidget:

```
CanvasPanel
├── [Bottom-Left] Health: ProgressBar + Text "100/100"
├── [Bottom-Right] Ammo: Text "30/30" + "Reserve: 270"
├── [Center] Crosshair: Image 32x32
├── [Bottom-Center] Interaction: Text "[E] Open Chest" (Collapsed)
└── [Center+40Y] Reload: Text "RELOADING" + ProgressBar (Collapsed)
```

Event Graph: override BlueprintImplementableEvents → set widget properties.

---

## Files Summary

| Action | File | Purpose |
|--------|------|---------|
| Modify | `FatumGame.Build.cs` | +UMG/Slate/SlateCore, +UI include path |
| Create | `Flecs/UI/FlecsMessageSystem.h` | Pub/sub система (~200 строк) |
| Create | `Flecs/UI/FlecsMessageSystem.cpp` | Реализация broadcast/register/unregister |
| Create | `Flecs/UI/FlecsUIMessages.h` | GameplayTags + USTRUCT messages |
| Create | `Flecs/UI/FlecsUIBridge.h` | FPendingUIEvent struct для MPSC |
| Create | `Flecs/UI/FlecsUISubsystem.h` | Event hub: queue + observers + interaction |
| Create | `Flecs/UI/FlecsUISubsystem.cpp` | Tick drain, queries, interaction trace |
| Create | `Flecs/UI/FlecsHUDWidget.h` | Widget base: auto-subscribe, BP events |
| Create | `Flecs/UI/FlecsHUDWidget.cpp` | Listener setup, event forwarding |
| Modify | `FlecsArtillerySubsystem_Systems.cpp` | Push UI events from weapon/death systems |
| Modify | `FlecsWeaponLibrary.h/.cpp` | +GetWeaponReloadProgress() |
| Modify | `FlecsCharacter.h/.cpp` | HUD creation, remove polling→subsystem |
| Create (editor) | `Content/Widgets/WBP_MainHUD` | UMG layout |

## Data Flow Summary

```
[Sim Thread]                    [MPSC Queue]              [Game Thread]
DamageObserver ──────────────→ FPendingUIEvent ──→ BroadcastMessage(TAG_UI_Health)
WeaponFireSystem ────────────→ FPendingUIEvent ──→ BroadcastMessage(TAG_UI_Ammo)
WeaponReloadSystem ──────────→ FPendingUIEvent ──→ BroadcastMessage(TAG_UI_Reload)
DeathCheckSystem ────────────→ FPendingUIEvent ──→ BroadcastMessage(TAG_UI_Death)
                                                   PerformInteractionTrace() (10Hz poll)
                                                     → BroadcastMessage(TAG_UI_Interaction)
                                                          │
                                                   UFlecsHUDWidget (listener)
                                                     → OnHealthChanged()
                                                     → OnAmmoChanged()
                                                     → etc.
```

## Verification

1. **Compile** — no errors
2. **Message system**: BroadcastMessage → RegisterListener callback fires
3. **Health**: Take damage → observer → queue → broadcast → bar updates
4. **Ammo**: Fire → system push → queue → broadcast → counter decrements
5. **Reload**: R → ReloadSystem push → broadcast → indicator appears/disappears
6. **Interaction**: Look at object → 10Hz poll → broadcast → prompt appears
7. **Crosshair**: Always visible at center
8. **Death**: HP→0 → DeathCheck push → broadcast → death event
9. **PIE exit**: No crashes (weak pointers, proper unsubscribe in NativeDestruct)
10. **Decoupling**: Any new widget can subscribe to same tags independently
