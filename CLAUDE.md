# FatumGame - Project Documentation

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
│   │   └── BarrageCollisionProcessors.*  # Collision system
│   ├── FatumGame.Target.cs          # Game build target
│   └── FatumGameEditor.Target.cs    # Editor build target
├── Plugins/                         # Project plugins (14 total)
│   ├── Artillery/                   # Weapons & abilities system
│   ├── Barrage/                     # Jolt Physics integration
│   ├── BarrageCollision/            # Collision dispatch system
│   ├── Bristlecone/                 # Network protocol
│   ├── Cabling/                     # Input/controls system
│   ├── Enace/                       # Item management system (Entity Ace)
│   ├── SkeletonKey/                 # Entity identity system
│   ├── LocomoCore/                  # Locomotion math library
│   ├── Phosphorus/                  # Event dispatch framework
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
│   ├── Locomo -> SkeletonKey
│   └── Phosphorus -> GameplayAbilities
├── Barrage
├── SkeletonKey
├── Cabling
└── Phosphorus

Optional:
├── Enace (Gameplay Data) -> SkeletonKey, Barrage, ArtilleryRuntime
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
- `UBarrageCharacterMovement` - Auto character movement (no BP code needed)
- `BarrageEntitySpawner` - Drag-and-drop physics entity spawner
- `UBarrageCollisionProcessors` - Dual-dispatch collision system

**Collision Dispatch Methods:**
1. **Entity Type Dispatch** - O(1) matrix lookup (fast, known cases)
2. **Tag-Based Dispatch** - Phosphorus pattern (flexible, dynamic)

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

### 5. Phosphorus (Event Dispatch)

**Purpose:** Matrix-based event dispatch with tag hierarchy support.

### 6. Enace (Gameplay Data Dispatch)

**Purpose:** Thread-safe storage for structured gameplay data (items, health, damage, loot) indexed by SkeletonKey. Uses libcuckoo maps like TransformDispatch/BarrageDispatch.

**Location:** `Plugins/Enace/`

**Architecture:**
```
SkeletonKey → BarrageDispatch   (physics bodies)
SkeletonKey → TransformDispatch (transforms)
SkeletonKey → ArtilleryDispatch (tags, abilities)
SkeletonKey → EnaceDispatch     (gameplay data)
```

**Key Classes:**
- `UEnaceDispatch` - World subsystem for gameplay data (libcuckoo maps)
- `UEnaceItemDefinition` - Data asset defining item types
- `FEnaceItemData`, `FEnaceHealthData`, `FEnaceDamageData` - Data structs
- `FItemKey` - SkeletonKey type for items (SFIX_ITEM = 0xC)

**Thread Safety:** Uses libcuckoo concurrent hash maps. Safe to read/write from Artillery thread without locks.

**Data Types:**
```cpp
FEnaceItemData   { Definition*, Count, DespawnTimer }
FEnaceHealthData { CurrentHP, MaxHP, Armor }
FEnaceDamageData { Damage, DamageType, bAreaDamage, AreaRadius }
FEnaceLootData   { LootTable*, MinDrops, MaxDrops }
```

**Usage:**
```cpp
UEnaceDispatch* Enace = UEnaceDispatch::SelfPtr;

// Spawn item (creates Barrage physics + registers data)
FSkeletonKey Key = Enace->SpawnWorldItem(StoneDefinition, Location, 5);

// Query in collision handler
FEnaceItemData ItemData;
if (Enace->TryGetItemData(Key, ItemData))
{
    // Pickup item
    Inventory->Add(ItemData.Definition, ItemData.Count);
    Enace->DestroyItem(Key);
}

// Health system
Enace->RegisterHealth(EnemyKey, 100.f);
bool bKilled = Enace->ApplyDamage(EnemyKey, 25.f);
if (bKilled)
{
    Enace->SpawnLoot(EnemyKey, Location);
}
```

**See Also:** `.claude/ENACE_IMPLEMENTATION_PLAN.md` for full implementation details.

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

### UBarrageCollisionSubsystem

**Location:** `Plugins/BarrageCollision/Source/BarrageCollision/Public/BarrageCollisionSubsystem.h`

World subsystem for collision handling. Integrates Barrage physics with Phosphorus event dispatch.

```cpp
// Get subsystem
auto* Collision = UBarrageCollisionSubsystem::Get(GetWorld());

// Entity Type Dispatch (O(1), fast)
void RegisterTypeHandler(EEntityType A, EEntityType B, FBPCollisionHandler)
void UnregisterTypeHandler(EEntityType A, EEntityType B)
bool HasTypeHandler(EEntityType A, EEntityType B) const

// Tag-Based Dispatch (flexible)
void RegisterTag(FGameplayTag Tag, FGameplayTag Parent = FGameplayTag())
void RegisterTagHandler(FGameplayTag A, FGameplayTag B, FBPCollisionHandler)

// Native access (C++)
FBarrageCollisionDispatcher& GetDispatcher()
```

**Collision Payload:**
```cpp
struct FBarrageCollisionPayload {
    int64 EntityA, EntityB           // Skeleton keys
    FVector ContactPoint             // World space
    EEntityType TypeA, TypeB         // Fast dispatch
    FGameplayTag TagA, TagB          // Tag dispatch

    // Helpers
    FSkeletonKey GetKeyA/B()
    bool IsAOfType(EEntityType)
    FSkeletonKey GetKeyOfTag(FGameplayTag)
}
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

```cpp
// In your game code initialization
UBarrageCollisionProcessors* Processors = GetWorld()->GetSubsystem<UBarrageCollisionProcessors>();

// Entity type dispatch (fast)
Processors->RegisterTypeHandler(EEntityType::Projectile, EEntityType::Actor,
    [](const FBarrageCollisionPayload& Payload) {
        // Handle collision
        return true; // Consume event
    });

// Tag-based dispatch (flexible)
Processors->RegisterTagHandler(
    FGameplayTag::RequestGameplayTag("Barrage.Projectile"),
    FGameplayTag::RequestGameplayTag("Barrage.Destructible"),
    MyBPCollisionHandler);
```

### Creating Custom Abilities

1. Inherit from `UArtilleryPerActorAbilityMinimum`
2. Assign to `FArtilleryGun` phases (prefire/fire/postfire/cosmetic)
3. Executes on Artillery thread (~120Hz)

### Item Pickup in Collision Handler

```cpp
void HandleCollision(const FBarrageCollisionPayload& Payload)
{
    UEnaceDispatch* Enace = UEnaceDispatch::SelfPtr;
    FSkeletonKey ItemKey = Payload.GetKeyA();
    FSkeletonKey PlayerKey = Payload.GetKeyB();

    // Check if this is an item (tag check is O(1))
    FEnaceItemData ItemData;
    if (Enace->TryGetItemData(ItemKey, ItemData))
    {
        // Get item definition
        UEnaceItemDefinition* Def = ItemData.Definition;

        if (Def->ItemId == "HealthPotion")
        {
            // Apply healing
            Enace->Heal(PlayerKey, 50.f);
        }
        else
        {
            // Add to inventory
            PlayerInventory->AddItem(Def, ItemData.Count);
        }

        // Destroy item (removes physics, render, and data)
        Enace->DestroyItem(ItemKey);
    }

    // Damage handling
    FEnaceDamageData DamageData;
    FEnaceHealthData HealthData;
    if (Enace->TryGetDamageData(Payload.GetKeyA(), DamageData) &&
        Enace->TryGetHealthData(Payload.GetKeyB(), HealthData))
    {
        bool bKilled = Enace->ApplyDamage(Payload.GetKeyB(), DamageData.Damage);
        if (bKilled)
        {
            // Spawn loot from killed entity
            FVector Location = /* get from TransformDispatch */;
            Enace->SpawnLoot(Payload.GetKeyB(), Location);
        }
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
