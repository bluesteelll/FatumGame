# FatumGame - Project Documentation

---

## CURRENT STATUS (January 2025)

**Enace → Flecs Migration: COMPLETE**
**Phosphorus/BarrageCollision → Flecs: COMPLETE**
**Ensure Errors Fix: COMPLETE**

### What Was Done:
1. ✅ Deleted `Plugins/Enace/` entirely
2. ✅ Created `UFlecsArtillerySubsystem` - bridge running Flecs on Artillery's 120Hz thread
3. ✅ Created `FlecsComponents.h/cpp` - ECS components (FItemData, FHealthData, FBarrageBody, etc.)
4. ✅ Created `FlecsGameplayLibrary.h/cpp` - Blueprint API for spawn/damage/heal
5. ✅ Removed Enace references from `ArtilleryCharacter.h/cpp`
6. ✅ Updated `FatumGame.Build.cs` with Flecs dependencies
7. ✅ **Deleted `Plugins/Phosphorus/`** - replaced with Flecs collision handling
8. ✅ **Deleted `Plugins/BarrageCollision/`** - replaced with Flecs collision handling
9. ✅ Collision handling in `UFlecsArtillerySubsystem::OnBarrageContact()`
10. ✅ Removed Phosphorus from `Artillery.uplugin` dependencies
11. ✅ Removed Phosphorus from `ArtilleryRuntime.Build.cs`
12. ✅ Removed BarrageCollision from `FatumGame.Build.cs`
13. ✅ Added `RegisterComponentType<T>()` calls in `SetupFlecsSystems()` - **REQUIRED** before using components
14. ✅ Created `DA_FlecsWorldSettings` Data Asset
15. ✅ Created `FlecsTestMap`
16. ✅ **Fixed Super::OnWorldBeginPlay() ensure errors** in 6 subsystem files (editor hang on PIE exit)

### Fixed Subsystem Files (Super::OnWorldBeginPlay added):
- `Plugins/Cabling/Source/Cabling/Private/UCablingWorldSubsystem.cpp`
- `Plugins/Bristlecone/Source/Bristlecone/Private/UBristleconeWorldSubsystem.cpp`
- `Plugins/Artillery/Source/ArtilleryRuntime/Private/ArtilleryDispatch.cpp`
- `Plugins/Artillery/Source/ArtilleryRuntime/Private/CanonicalInputStreamECS.cpp`
- `Plugins/Thistle/Source/ThistleRuntime/Private/ThistleDispatch.cpp`
- `Plugins/sunflower/Source/SunflowerRuntime/Private/SunflowerDispatch.cpp`

### What Needs To Be Done:
1. ✅ ~~Set Project Settings → World Settings Class = `AFlecsWorldSettings`~~ (NO LONGER NEEDED - using direct flecs::world)
2. ❌ **RECOMPILE AND TEST** - direct flecs::world should fix threading crash
3. ✅ ~~Open FlecsTestMap → World Settings → Set Default World~~ (NO LONGER NEEDED)
4. ❌ Create `BP_FlecsTestGameMode` with `ABarragePlayerController`
5. ❌ Create `BP_FlecsTestCharacter` from `AArtilleryCharacter`
6. ❌ Test that Flecs systems run on Artillery thread (check Output Log for "FlecsArtillerySubsystem: Online (direct flecs::world, no plugin tick functions)")

### Key Files:
- `Source/FatumGame/FlecsArtillerySubsystem.h/cpp` - Bridge subsystem + collision handling + component registration
- `Source/FatumGame/FlecsComponents.h/cpp` - ECS components + tags (FTagProjectile, FTagCharacter, FFlecsCollisionEvent)
- `Source/FatumGame/FlecsGameplayLibrary.h/cpp` - Blueprint API

### Critical Implementation Details:

**Component Registration (IMPORTANT):**
Components MUST be registered in Flecs world before use. In `SetupFlecsSystems()`:
```cpp
// Using direct flecs::world API (NOT UFlecsWorld wrapper)
World.component<FItemData>();
World.component<FHealthData>();
// ... all other components
```

**Collision Flow:**
```
Barrage → OnBarrageContactAddedDelegate → UFlecsArtillerySubsystem::OnBarrageContact()
    → Get FBLet bodies → Extract KeyOutOfBarrage (FSkeletonKey)
    → Lookup Flecs entity via BarrageKeyIndex
    → Apply damage/destruction directly (runs on Artillery thread)
```

### Broken Assets (reference deleted Enace types):
- `Content/BP_MyArtilleryCharacter.uasset` - has broken refs
- `Content/DA_EnaceCont.uasset` - orphaned
- `Content/DA_MyItemDef.uasset` - orphaned

### Flecs World Setup Requirements:
**NO LONGER NEEDED** - We now create a direct `flecs::world` in `FlecsArtillerySubsystem`, completely bypassing the UnrealFlecs plugin's `UFlecsWorldSubsystem`. This avoids all the plugin's tick functions and internal threading.

~~1. Project Settings → Engine → General Settings → World Settings Class = `AFlecsWorldSettings`~~
~~2. Map's World Settings → Default World = `DA_FlecsWorldSettings`~~
~~3. DA_FlecsWorldSettings → Game Loops = empty~~
~~4. Don't use task threads~~

**Current approach:** Direct `flecs::world` created in `RegistrationImplementation()` with `set_threads(0)` for single-threaded Artillery execution.

### Recent Crashes Fixed:
1. `Assertion failed: World settings must be of type AFlecsWorldSettings` → Set World Settings Class in Project Settings
2. `flecs::_::type_impl<FItemData>::id()` crash → Added `RegisterComponentType<T>()` calls before creating systems
3. `OnWorldBeginPlay has not been called for subsystem X` ensure errors (editor hang on PIE exit) → Added `Super::OnWorldBeginPlay(InWorld);` to all subsystem OnWorldBeginPlay overrides
4. **Editor hang on PIE exit (Artillery thread race condition)** → Multiple fixes required (see below)
5. **"cannot begin frame while frame is already in progress" crash** → Switched to direct `flecs::world` (see below)
6. **`EXCEPTION_ACCESS_VIOLATION` at `FArtilleryTicklitesThread.h:247`** → Added null-checks and atomic running flag (see below)

### PIE Exit Hang Fix (Artillery Thread Race Condition)

**Root Cause:** Artillery busy worker thread (~120Hz) holds raw `ITickHeavy*` pointers to subsystems and calls `ArtilleryTick()` on them. During PIE exit, subsystems are destroyed but the thread may still be running. Additionally, subsystem deinitialization order was incorrect - Cabling/Bristlecone were deinitializing before Artillery, leaving BusyWorker accessing freed memory.

**Fix 1: Made `running` flag atomic in FArtilleryBusyWorker**
```cpp
// In FArtilleryBusyWorker.h:
#include <atomic>
// ...
private:
    std::atomic<bool> running;  // Was: bool running;
```

**Fix 2: Correct deinitialize order - Stop() FIRST, then wait, then clear pointers**
```cpp
// In ArtilleryDispatch.cpp Deinitialize():
// 1. Signal thread to stop FIRST
ArtilleryAsyncWorldSim.Stop();
// 2. Trigger events so other threads wake up
StartTicklitesSim->Trigger();
// 3. Wait for thread to finish
WorldSim_Thread->Kill(); WorldSim_Thread.Reset();
// 4. NOW safe to clear pointers
ArtilleryAsyncWorldSim.FlecsSystemPointer = nullptr;
// ... etc
```

**Fix 3: Declare subsystem dependencies for correct deinitialize order**
```cpp
// In ArtilleryDispatch.cpp Initialize():
void UArtilleryDispatch::Initialize(FSubsystemCollectionBase& Collection)
{
    // CRITICAL: These must be declared BEFORE Super::Initialize()!
    // Ensures Artillery deinitializes BEFORE Cabling/Bristlecone
    Collection.InitializeDependency<UTransformDispatch>();
    Collection.InitializeDependency<UCablingWorldSubsystem>();
    Collection.InitializeDependency<UBristleconeWorldSubsystem>();
    Collection.InitializeDependency<UCanonicalInputStreamECS>();
    Collection.InitializeDependency<UBarrageDispatch>();

    Super::Initialize(Collection);
    // ...
}

// Similarly in FlecsArtillerySubsystem.cpp Initialize():
Collection.InitializeDependency<UArtilleryDispatch>();
Collection.InitializeDependency<UBarrageDispatch>();
```

**Fix 4: Each subsystem clears its own pointer in Deinitialize()**
```cpp
void UFlecsArtillerySubsystem::Deinitialize()
{
    if (UArtilleryDispatch* ArtilleryDispatch = UArtilleryDispatch::SelfPtr)
    {
        ArtilleryDispatch->SetFlecsDispatch(nullptr);
    }
    // ... cleanup ...
    Super::Deinitialize();
}
```

**Files Modified:**
- `Plugins/Artillery/Source/ArtilleryRuntime/Public/Systems/Threads/FArtilleryBusyWorker.h` - made `running` atomic
- `Plugins/Artillery/Source/ArtilleryRuntime/Private/FArtilleryBusyWorker.cpp` - use atomic memory ordering
- `Plugins/Artillery/Source/ArtilleryRuntime/Private/ArtilleryDispatch.cpp` - fixed deinit order + added InitializeDependency calls
- `Source/FatumGame/FlecsArtillerySubsystem.cpp` - added InitializeDependency calls + clears SetFlecsDispatch(nullptr)
- `Plugins/Artillery/.../NiagaraParticleDispatch.cpp` - clears SetParticleDispatch(nullptr)
- `Plugins/Artillery/.../ArtilleryProjectileDispatch.cpp` - clears SetProjectileDispatch(nullptr)
- `Plugins/Artillery/.../UEventLogSystem.cpp` - added Deinitialize() with SetEventLogSystem(nullptr)
- `Plugins/Artillery/.../UEventLogSystem.h` - added Deinitialize() declaration

### Ticklites/AI Thread Crash Fix (EXCEPTION_ACCESS_VIOLATION at line 247)

**Root Cause:** The `FArtilleryTicklitesWorker` and `FStateTreesWorker` threads access `FSharedEventRef` members (`StartTicklitesApply`, `StartTicklitesSim`, `RunAheadStateTrees`) without null-checks. During shutdown, these events may become invalid while the thread is still running, causing `->Reset()` to dereference nullptr.

**Fix 1: Made `running` flag atomic in both thread workers**
```cpp
// In FArtilleryTicklitesThread.h and FArtilleryStateTreesThread.h:
#include <atomic>
// ...
private:
    std::atomic<bool> running{false};  // Was: bool running;
```

**Fix 2: Added null-checks before using FSharedEventRef in Run() loop**
```cpp
// In FArtilleryTicklitesThread.h Run():
while(running.load(std::memory_order_acquire)) {
    // ... processing ...

    // SAFETY: Check that event is still valid before waiting
    if (!StartTicklitesApply.IsValid() || !running.load(std::memory_order_acquire))
    {
        break;
    }

    StartTicklitesApply->Wait();

    // SAFETY: Check again after wait (shutdown may have occurred while waiting)
    if (!StartTicklitesApply.IsValid() || !running.load(std::memory_order_acquire))
    {
        break;
    }
    StartTicklitesApply->Reset();
    // ... apply logic ...
}
```

**Fix 3: Use atomic memory ordering for all running flag accesses**
```cpp
// Store operations (in Init/Stop/Exit/Cleanup):
running.store(true, std::memory_order_release);
running.store(false, std::memory_order_release);

// Load operations (in while loop conditions):
while(running.load(std::memory_order_acquire)) { ... }
```

**Files Modified:**
- `Plugins/Artillery/Source/ArtilleryRuntime/Public/Systems/Threads/FArtilleryTicklitesThread.h` - atomic running, null-checks
- `Plugins/Artillery/Source/ArtilleryRuntime/Public/Systems/Threads/FArtilleryStateTreesThread.h` - atomic running, null-checks

### IMPORTANT: Subsystem OnWorldBeginPlay Pattern
All UWorldSubsystem-derived classes MUST call `Super::OnWorldBeginPlay(InWorld);` in their OnWorldBeginPlay override:
```cpp
void UMySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);  // REQUIRED!
    // Your code here...
}
```
Missing this call causes ensure errors and editor hangs when exiting PIE.

### Flecs "cannot begin frame while frame is already in progress" Fix

**Root Cause:** The UnrealFlecs plugin (`Plugins/FlecsIntegration/`) has a `FFlecsWorldSettingsInfo` constructor that **automatically adds 5 tick functions** (MainLoop, PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork). These tick functions call `World.progress()` on the game thread every frame. When our `ArtilleryTick()` also calls `World.progress()` on the Artillery thread, we get a race condition crash.

Additionally, the plugin's `UFlecsWorldSubsystem::CreateWorld()` calls `SetThreads(NumberOfCores - 2)` which enables Flecs' internal worker threads, further complicating the threading model.

**Solution: Create our own flecs::world directly, bypassing the plugin's UFlecsWorld wrapper**

```cpp
// FlecsArtillerySubsystem.h:
#include "flecs.h"
// ...
TUniquePtr<flecs::world> FlecsWorld;  // Direct flecs::world, NOT UFlecsWorld*

// FlecsArtillerySubsystem.cpp:
bool UFlecsArtillerySubsystem::RegistrationImplementation()
{
    // Create our own flecs::world directly - no plugin tick functions, no worker threads
    FlecsWorld = MakeUnique<flecs::world>();

    // CRITICAL: Disable Flecs' internal threading - Artillery thread is our only executor
    FlecsWorld->set_threads(0);

    // Register components with direct flecs API
    FlecsWorld->component<FItemData>();
    FlecsWorld->component<FHealthData>();
    // ... etc
}

void UFlecsArtillerySubsystem::ArtilleryTick()
{
    FlecsWorld->progress(1.0 / 120.0);  // Only caller of progress()
}
```

**Key Changes:**
- `FlecsArtillerySubsystem.h`: Changed `TObjectPtr<UFlecsWorld> FlecsWorld` to `TUniquePtr<flecs::world> FlecsWorld`
- `FlecsArtillerySubsystem.h`: Added `#include "flecs.h"`
- `FlecsArtillerySubsystem.cpp`: Removed `#include "Worlds/FlecsWorld.h"` and `#include "Worlds/FlecsWorldSubsystem.h"`
- `FlecsArtillerySubsystem.cpp`: Create world with `MakeUnique<flecs::world>()`
- `FlecsArtillerySubsystem.cpp`: Call `FlecsWorld->set_threads(0)` to disable internal threading
- `FlecsArtillerySubsystem.cpp`: Use `World.component<T>()` instead of `RegisterComponentType<T>()`
- `FlecsGameplayLibrary.cpp`: Changed `UFlecsWorld*` to `flecs::world*`

**Result:** No more plugin tick functions, no internal worker threads, only Artillery thread calls `progress()`.

**Note:** The `DA_FlecsWorldSettings` data asset and `AFlecsWorldSettings` world settings class are NO LONGER NEEDED. The plugin's world subsystem is completely bypassed.

---

## Quick Reference

| Property | Value |
|----------|-------|
| **Engine** | Unreal Engine 5.7 |
| **Architecture** | ECS-like (Artillery/Barrage/Mass) |
| **Physics** | Jolt Physics (via Barrage) |
| **Networking** | Bristlecone (deterministic rollback) |
| **Primary Module** | FatumGame (Runtime) |
| **Target Platforms** | Windows, Mac, Linux, LinuxArm64, HoloLens |

---

## Project Overview

FatumGame is a high-performance game built on Unreal Engine 5.7 using a custom ECS-like architecture. The project combines multiple specialized plugins to achieve deterministic multiplayer gameplay with advanced physics simulation.

### Core Architecture Philosophy

```
Bristlecone       -> Network inputs (UDP, deterministic)
Cabling           -> Local inputs & keymapping
Artillery         -> Core game simulation (~120Hz, separate thread)
Barrage           -> Jolt Physics integration
UE Game Thread    -> Cosmetics, animation, rendering (non-deterministic)
```

**Key Concept:** The simulation runs at ~120Hz on a dedicated thread (ArtilleryBusyWorker), separate from the game thread, enabling deterministic rollback networking.

---

## Directory Structure

```
FatumGame/
├── Source/                          # C++ source code
│   ├── FatumGame/                   # Main game module
│   │   ├── ArtilleryCharacter.*     # Base character class
│   │   ├── FlecsArtillerySubsystem.*    # Flecs-Artillery bridge + collision handling
│   │   ├── FlecsComponents.*        # ECS components and tags
│   │   └── FlecsGameplayLibrary.*   # Blueprint API for Flecs
│   ├── FatumGame.Target.cs          # Game build target
│   └── FatumGameEditor.Target.cs    # Editor build target
├── Plugins/                         # Project plugins
│   ├── Artillery/                   # Weapons & abilities system
│   ├── Barrage/                     # Jolt Physics integration
│   ├── Bristlecone/                 # Network protocol
│   ├── Cabling/                     # Input/controls system
│   ├── FlecsIntegration/            # Flecs ECS (UnrealFlecs, FlecsLibrary, SolidMacros)
│   ├── SkeletonKey/                 # Entity identity system
│   ├── LocomoCore/                  # Locomotion math library
│   ├── Thistle/                     # AI system
│   ├── sunflower/                   # UI system
│   ├── MassCommunitySample/         # Mass ECS sample
│   ├── NiagaraUIRenderer/           # Niagara UI particles
│   ├── BarrageTests/                # Physics tests
│   └── UE4CMake/                    # CMake build support
├── Content/                         # Game assets
│   ├── BP_MyArtilleryCharacter.uasset
│   ├── BP_StdPlayerController.uasset
│   ├── DA_BouncingBullet.uasset
│   └── NewMap.umap
├── Config/                          # Configuration files
├── .claude/                         # Detailed system documentation
│   ├── ARTILLERY_BARRAGE_DOCUMENTATION.md  # Full architecture docs
│   ├── BARRAGE_BLUEPRINT_INTEGRATION.md    # Blueprint API reference
│   ├── BARRAGE_INTEGRATION_SETUP.md        # Setup guide
│   └── ... (additional guides)
└── Images/                          # Documentation images
```

---

## Plugin Dependency Hierarchy

```
FatumGame (Main Module)
├── ArtilleryRuntime
│   ├── GameplayAbilities (Engine)
│   ├── Bristlecone
│   │   ├── Cabling -> SkeletonKey, Locomo
│   │   └── SkeletonKey
│   ├── Niagara (Engine)
│   ├── SkeletonKey -> GameplayAbilities
│   ├── Barrage -> CMakeTarget, SkeletonKey, Locomo
│   └── Locomo -> SkeletonKey
├── Barrage
├── SkeletonKey
├── Cabling
├── UnrealFlecs (Flecs ECS)
├── FlecsLibrary
└── SolidMacros

ECS Integration:
├── UnrealFlecs -> FlecsLibrary, SolidMacros, GameplayTags
├── FlecsLibrary (Flecs C++ wrapper)
└── SolidMacros (utility macros)

Optional:
├── Thistle (AI) -> Artillery, MassGameplay, StateTree, SmartObjects
├── Sunflower (UI) -> SkeletonKey, Artillery, Thistle, MassAI
└── MassCommunitySample -> MassGameplay, MassAI, MassCrowd
```

---

## Core Systems

### 1. Artillery System (Core ECS Framework)

**Purpose:** Backbone of the game - manages entity attributes, ticklites, state trees, and projectiles.

**Key Classes:**
- `UArtilleryDispatch` - World subsystem managing 120Hz simulation
- `FArtilleryGun` - Data-driven gun/ability instances
- `FArtilleryShell` - Input container from Bristlecone

**Thread Workers:**
- `FArtilleryBusyWorker` - Core 120Hz simulation
- `FArtilleryStateTreesThread` - AI behavior trees
- `FArtilleryTicklitesThread` - Lightweight game mechanics

### 2. Barrage Physics (Jolt Integration)

**Purpose:** High-performance physics via Jolt engine.

**Key Classes:**
- `UBarrageDispatch` - World subsystem managing Jolt physics
- `UBarrageCharacterMovement` - Auto character movement (no BP code needed)
- `BarrageEntitySpawner` - Drag-and-drop physics entity spawner

**Collision Events:**
- `OnBarrageContactAddedDelegate` - Fires when two bodies start touching
- `OnBarrageContactRemovedDelegate` - Fires when contact ends
- Events broadcast on Artillery thread (120Hz)

### 3. SkeletonKey (Entity Identity)

**Purpose:** 64-bit universal keys for all entities.

**Features:**
- Embeds entity type in 4-bit nibble (bits 60-63)
- 28 bits for metadata (bits 28-59)
- 32 bits for hash (bits 0-27)
- Cross-machine deterministic
- O(1) type checking

**Key Structure:**
```
[TTTT][MMMM MMMM MMMM MMMM MMMM MMMM MMMM][HHHH HHHH HHHH HHHH HHHH HHHH HHHH HHHH]
 Type         Metadata (28 bits)                    Hash (32 bits)
```

**Type Nibbles (SFIX values):**
| Nibble | Type | Description |
|--------|------|-------------|
| 0x0 | NONE | Invalid/unset |
| 0x1 | ART_GUNS | Gun archetypes |
| 0x2 | ART_1GUN | Gun instances |
| 0x3 | GUN_SHOT | Projectiles |
| 0x4 | BAR_PRIM | Barrage primitives |
| 0x5 | ART_ACTS | Actors |
| 0x6 | ART_FCMS | FCMs |
| 0x7 | ART_FACT | Facts |
| 0x8 | PLAYERID | Players |
| 0x9 | BONEKEY | Bone keys |
| 0xA | MASSIDP | Mass entities |
| 0xB | STELLAR | Constellations |
| 0xC | ITEM | Items (Enace) |
| 0xD | (unused) | Reserved |
| 0xE | (unused) | Reserved |
| 0xF | SK_LORD | Lords/Components |

**Key Types:**
- `FSkeletonKey` - Base universal key
- `ActorKey` - For actors (SFIX_ART_ACTS)
- `FBoneKey` - For bone components (SFIX_BONEKEY)
- `FItemKey` - For items (SFIX_ITEM)
- `FGunInstanceKey` - For gun instances (SFIX_ART_1GUN)
- `FProjectileInstanceKey` - For projectiles (SFIX_GUN_SHOT)

```cpp
FSkeletonKey MyKey = KeyCarry->GetMyKey();
bool valid = MyKey.IsValid();

// O(1) type checking
if (EntityTypeUtils::IsItem(Key)) { ... }
if (EntityTypeUtils::IsProjectile(Key)) { ... }
```

### 4. Bristlecone (Networking)

**Purpose:** Deterministic UDP network protocol for rollback networking.

### 5. Flecs ECS (Replaced Enace + Phosphorus + BarrageCollision)

**Purpose:** Single source of truth for ALL gameplay data. Runs on Artillery's 120Hz thread.

**Location:** `Source/FatumGame/` (FlecsArtillerySubsystem, FlecsComponents, FlecsGameplayLibrary)

**Plugin:** `Plugins/FlecsIntegration/` (Unreal-Flecs by Reddy-dev)

**Architecture:**
```
Flecs World        -> ALL gameplay data (items, health, damage, etc.)
Barrage/Jolt       -> Physics simulation (unchanged)
BarrageRenderManager -> ISM rendering (unchanged)
Artillery          -> Orchestrator calling Flecs.Progress() at 120Hz
```

**Key Classes:**
- `UFlecsArtillerySubsystem` - Bridge running Flecs on Artillery thread
- `UFlecsGameplayLibrary` - Blueprint function library for spawn/damage/heal
- `FlecsComponents.h` - ECS component definitions

**Components (in FlecsComponents.h):**
```cpp
FItemData        { Definition*, Count, DespawnTimer }
FHealthData      { CurrentHP, MaxHP, Armor }
FDamageSource    { Damage, DamageType, bAreaDamage, AreaRadius }
FLootData        { MinDrops, MaxDrops }
FBarrageBody     { SkeletonKey }
FISMRender       { Mesh*, Scale }
FFlecsCollisionEvent { OtherKey, OtherFlecsId, ContactPoint, bOtherIsProjectile }
// Tags: FTagItem, FTagPickupable, FTagDestructible, FTagHasLoot, FTagDead, FTagProjectile, FTagCharacter
```

**Thread Safety:** Game thread enqueues commands via `EnqueueCommand()`, Artillery thread executes them before `World.progress()`.

**Usage (Game Thread - Blueprint Safe):**
```cpp
// Spawn world item
UFlecsGameplayLibrary::SpawnWorldItem(WorldContext, ItemDef, Mesh, Location, Count, DespawnTime);

// Apply damage
UFlecsGameplayLibrary::ApplyDamageByBarrageKey(WorldContext, BarrageKey, Damage);

// Heal
UFlecsGameplayLibrary::HealEntityByBarrageKey(WorldContext, BarrageKey, Amount);
```

**Usage (Artillery Thread - Direct Flecs):**
```cpp
UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
flecs::entity Entity = GetFlecsEntityForKey(Sub, BarrageKey);

FHealthData* Health = Entity.try_get_mut<FHealthData>();
if (Health) Health->CurrentHP -= Damage;

if (!Health->IsAlive()) Entity.add<FTagDead>();
```

**Flecs Systems (in FlecsArtillerySubsystem::SetupFlecsSystems):**
- `ItemDespawnSystem` - Decrements timers, tags dead when expired
- `DeathCheckSystem` - Tags entities with HP <= 0 as dead
- `DeadEntityCleanupSystem` - Destroys dead entities, unregisters from Barrage

**Collision Handling (in UFlecsArtillerySubsystem::OnBarrageContact):**
- Subscribes to `UBarrageDispatch::OnBarrageContactAddedDelegate`
- Automatically handles projectile damage (FDamageSource → FHealthData)
- Destroys FTagDestructible entities when hit by projectiles
- Runs directly on Artillery thread (no queueing needed)

---

## Main C++ Classes

### AArtilleryCharacter

**Location:** `Source/FatumGame/ArtilleryCharacter.h`

Base character class integrating Artillery/Barrage systems.

```cpp
// Key Components
UPlayerKeyCarry* KeyCarry              // Artillery identity
UBarrageCharacterMovement* BarrageMovement  // Jolt physics movement

// Key Properties (BlueprintReadWrite)
UProjectileDefinition* ProjectileDefinition
FVector MuzzleOffset                   // Default: (100, 0, 50)
float ProjectileSpeedOverride
bool bUseStandardCameraControl

// Key Methods (BlueprintCallable)
FSkeletonKey FireProjectile()
FSkeletonKey FireProjectileInDirection(FVector Direction)
TArray<FSkeletonKey> FireProjectileSpread(int32 Count, float SpreadAngle)
void AddCameraPitchInput(float Value)
void AddCameraYawInput(float Value)
```

### ABarragePlayerController

**Location:** `Plugins/Artillery/Source/ArtilleryRuntime/Public/EssentialTypes/ABarragePlayerController.h`

Two-sided controller hiding Artillery machinery.

```cpp
// Aim Modulation Properties
float PitchScale = 1.15f
float YawScale = 1.15f
UCurveVector* AimModulator           // Custom aim curves

// Key Methods
virtual void OnPossess(APawn* aPawn)
virtual FRotator ModulateRotation(FRotator)
virtual void ArtilleryTick()
```

### UBarrageCharacterMovement

**Location:** `Plugins/Barrage/Source/Barrage/Public/`

Automatic character movement - no Blueprint code needed.

```cpp
// Auto Properties
bool bAutoProcessInput = true
bool bAutoSyncPosition = true
float MovementSpeed = 1000.0f
float AirControlMultiplier = 0.3f
bool bEnableSprint
float SprintSpeedMultiplier = 2.0f
```

---

## Key Interfaces

### IArtilleryLocomotionInterface

```cpp
bool LocomotionStateMachine(FArtilleryShell Previous, FArtilleryShell Current, bool RunAtLeastOnce, bool Smear)
void LookStateMachine(FRotator& IN_OUT_LookVector)
FSkeletonKey GetMyKey() const
bool IsReady()
void PrepareForPossess()
void PrepareForUnPossess()
```

### ITickHeavy

```cpp
void ArtilleryTick(FArtilleryShell Previous, FArtilleryShell Current, bool RunAtLeastOnce, bool Smear)
void ArtilleryTick(uint64_t TicksSoFar)
void ArtilleryTick()  // Parameterless
FSkeletonKey GetMyKey() const
```

---

## Blueprint Integration

### Creating a Custom Character

1. Create Blueprint inheriting from `BP_MyArtilleryCharacter` or `AArtilleryCharacter`
2. Set `ProjectileDefinition` data asset
3. Configure `MuzzleOffset` and `ProjectileSpeedOverride`
4. Use `ABarragePlayerController` as controller class

### Firing Projectiles

```cpp
// Single shot
FSkeletonKey Key = FireProjectile();

// Directional shot
FSkeletonKey Key = FireProjectileInDirection(GetActorForwardVector());

// Spread/shotgun (5 pellets, 15 degree spread)
TArray<FSkeletonKey> Keys = FireProjectileSpread(5, 15.0f);
```

### Physics Entity Spawning

Use `BarrageEntitySpawner` actor:
- Set `Mesh` - rendering mesh
- Set `bAutoCollider` = true for auto collision
- Set `PhysicsLayer` for physics category
- Set `bDestructible` = true if destroyed by projectiles

---

## Configuration

### Key Console Variables

```ini
# Enable fully parallel Mass processing
mass.FullyParallel=1

# Async Chaos body creation
p.Chaos.EnableAsyncInitBody=1
```

### Default Maps

- Editor Startup: `/MassCommunitySample/MassSample/Maps/MassSampleHall`
- Game Default: `/Game/MassSample/Maps/MassSampleHall`
- Game Mode: `BP_MSGameMode_C`

### Input Bindings (DefaultInput.ini)

- Movement: WASD / Left Stick
- Camera: Mouse / Right Stick
- Jump: Space / Gamepad A
- Primary Action: LMB / Gamepad RT
- Secondary Action: RMB / Gamepad LT

---

## UE 5.7 API Notes

### Mass ECS (Deprecated in 5.5+)
MassEntity plugin is deprecated but still functional. Key API changes:

```cpp
// OLD (pre-5.5):
virtual void ConfigureQueries() override;
Query.ForEachEntityChunk(EntityManager, Context, [](FMassExecutionContext& Context) {...});

// NEW (5.5+):
virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
Query.ForEachEntityChunk(Context, [](FMassExecutionContext& Context) {...});
```

### Barrage Types
- `FBarrageKey` - Not a USTRUCT, cannot use with UPROPERTY
- `FBLet` = `TSharedPtr<FBarragePrimitive>` - Physics body handle
- Include `FBarragePrimitive.h` for FBLet, `FBarrageKey.h` for FBarrageKey

### Physics Layers (EPhysicsLayer)
Available values: `NON_MOVING`, `MOVING`, `HITBOX`, `PROJECTILE`, `ENEMYPROJECTILE`, `ENEMYHITBOX`, `ENEMY`, `CAST_QUERY`, `DEBRIS`

---

## Build Requirements

### Visual Studio 2022
- VC++ 14.44.17.14 toolset
- Windows 11 SDK 22621
- Native game development tools

### CMake (for Jolt Physics)
- cmake-3.30.2 or later
- Required for Barrage plugin compilation

### Git LFS
- Required for binary assets
- Run `git lfs pull` after cloning

---

## Threading Model

| Thread | Frequency | Purpose |
|--------|-----------|---------|
| Artillery Busy Worker | ~120Hz | Core gameplay simulation (deterministic) |
| State Trees Thread | Variable | AI decision-making |
| Ticklites Thread | ~120Hz | Lightweight game mechanics |
| Game Thread | ~60Hz+ | Cosmetics, animation, rendering |

**Determinism Guarantee:** Only for core simulation on Artillery thread. Game thread operations are non-deterministic (cosmetics only).

---

## Detailed Documentation

For in-depth documentation on specific systems, see:

| Document | Purpose |
|----------|---------|
| `.claude/ARTILLERY_BARRAGE_DOCUMENTATION.md` | Complete architecture overview |
| `.claude/BARRAGE_BLUEPRINT_INTEGRATION.md` | Full Blueprint API reference |
| `.claude/BARRAGE_INTEGRATION_SETUP.md` | Compilation & setup guide |
| `.claude/BARRAGE_BLUEPRINT_CHEATSHEET.md` | Quick reference |
| `.claude/BARRAGE_AUTO_MOVEMENT_GUIDE.md` | Movement component docs |
| `README.md` | Mass ECS framework documentation |
| `Plugins/Artillery/README.md` | Artillery plugin overview |
| `Plugins/Barrage/Readme.md` | Barrage/Jolt setup |
| `Plugins/Bristlecone/README.md` | Network protocol spec |

---

## Common Development Tasks

### Adding a New Projectile Type

1. Create `UProjectileDefinition` data asset
2. Set `ProjectileMesh` and `DefaultSpeed`
3. Assign to character's `ProjectileDefinition` property

### Adding Custom Collision Handling

Collision handling is done directly in Flecs via `UFlecsArtillerySubsystem::OnBarrageContact()`.

**Built-in behaviors (automatic):**
- Projectiles with `FDamageSource` deal damage to entities with `FHealthData`
- Entities with `FTagDestructible` are tagged `FTagDead` when hit by projectiles
- Dead entities are cleaned up by `DeadEntityCleanupSystem`

**Adding custom collision logic:**
```cpp
// In UFlecsArtillerySubsystem::OnBarrageContact() add your custom handlers:
void UFlecsArtillerySubsystem::OnBarrageContact(const BarrageContactEvent& Event)
{
    // ... existing code ...

    // Custom: Item pickup when player touches item
    if (FlecsId1 != 0 && FlecsId2 != 0)
    {
        flecs::entity Entity1 = World.entity(FlecsId1);
        flecs::entity Entity2 = World.entity(FlecsId2);

        // Check if one is item and other is character
        if (Entity1.has<FTagItem>() && Entity2.has<FTagCharacter>())
        {
            // Handle pickup
            Entity1.add<FTagDead>();
        }
    }
}
```

**From game thread (Blueprint-safe):**
```cpp
// Use UFlecsGameplayLibrary for thread-safe operations
UFlecsGameplayLibrary::ApplyDamageByBarrageKey(World, TargetKey, 25.f);
UFlecsGameplayLibrary::KillEntityByBarrageKey(World, EntityKey);
```

### Creating Custom Abilities

1. Inherit from `UArtilleryPerActorAbilityMinimum`
2. Assign to `FArtilleryGun` phases (prefire/fire/postfire/cosmetic)
3. Executes on Artillery thread (~120Hz)

### Item Pickup / Damage (Using Flecs)

**Automatic collision handling** is built into `UFlecsArtillerySubsystem::OnBarrageContact()`:
- Projectiles with `FDamageSource` automatically damage entities with `FHealthData`
- Armor is respected: `EffectiveDamage = max(0, Damage - Armor)`
- Projectiles are tagged `FTagDead` after hit (unless area damage)

**From game thread (Blueprint-safe):**
```cpp
// Apply damage via Flecs (thread-safe, enqueues to Artillery thread)
UFlecsGameplayLibrary::ApplyDamageByBarrageKey(World, TargetKey, 25.f);

// Kill entity directly
UFlecsGameplayLibrary::KillEntityByBarrageKey(World, ItemKey);

// Heal entity
UFlecsGameplayLibrary::HealEntityByBarrageKey(World, TargetKey, 50.f);
```

**From Artillery thread (direct Flecs):**
```cpp
// In collision handler or Flecs system - no queue needed
UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
uint64 FlecsId = Sub->GetFlecsEntityForBarrageKey(Key);
flecs::entity Entity = Sub->GetFlecsWorld()->World.entity(FlecsId);

FHealthData* Health = Entity.try_get_mut<FHealthData>();
if (Health)
{
    Health->CurrentHP -= Damage;
    if (!Health->IsAlive())
    {
        Entity.add<FTagDead>(); // Cleanup system handles destruction
    }
}
```

---

## Project Status

- **Stage:** Early development
- **Base:** MassCommunitySample by Megafunk & vorixo
- **Engine:** Unreal Engine 5.7
- **Architecture:** Production-grade deterministic multiplayer

---

## Credits

- **Mass ECS Sample:** Karl Mavko (@Megafunk), Alvaro Jover (@vorixo)
- **Artillery/Barrage/Bristlecone:** Hedra, Breach Dogs, Oversized Sun Inc.
- **Jolt Physics:** Jorrit Rouwe
- **NiagaraUIRenderer:** Michal Smolen
