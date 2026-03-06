# FatumGame

Unreal Engine 5.7 game project using **Flecs ECS** for gameplay logic and **Jolt Physics** (via Barrage) for high-performance physics simulation on a dedicated 60Hz simulation thread.

## Requirements

- **Unreal Engine 5.7**
- **Visual Studio 2022** (VC++ 14.44, Win11 SDK 22621)
- **CMake 3.30.2+** (for Jolt Physics)
- **Git LFS** (required for assets)

## Setup

```bash
git clone <repository-url>
cd FatumGame
git lfs pull
```

Open `FatumGame.uproject` in Unreal Engine 5.7.

## Architecture

```
FSimulationWorker -> Simulation thread (60Hz)
  DrainCommandQueue -> PrepareCharacterStep(RealDT, DilatedDT)
    -> StackUp -> StepWorld(DilatedDT) -> BroadcastContactEvents -> progress(DilatedDT)
Barrage            -> Jolt Physics integration
Flecs              -> Gameplay ECS (health, damage, items, weapons, destructibles)
Game Thread        -> Cosmetics, rendering, ISM spawns, UI
Time Dilation      -> FTimeDilationStack (game thread) -> atomics -> sim thread DT splitting
```

### Key Technologies

| Component | Description |
|-----------|-------------|
| **Flecs** | High-performance ECS via FlecsIntegration plugin |
| **Jolt Physics** | Fast physics via Barrage plugin |
| **FSimulationWorker** | 60Hz simulation thread (physics + ECS) |
| **SkeletonKey** | Type-safe entity identity system |
| **FTimeDilationStack** | Multi-source time dilation with min-wins resolution |
| **Render Interpolation** | Sub-tick smoothing via Prev/Curr lerp with alpha |

## Project Structure

Domain-based vertical folder layout:

```
Source/FatumGame/
  Core/             - Simulation core (subsystem, sim worker, late sync bridge, tags)
    Components/     - Health, entity, interaction ECS components
  Abilities/        - Ability components, lifecycle, tick functions
  Character/        - FlecsCharacter, movement component, posture state machine
  Definitions/      - All Data Assets and Profiles (~30 files)
  Destructible/     - Destructible components, fragmentation, debris pool
  Door/             - Door components and systems
  Input/            - Input config, input component, input tags
  Interaction/      - Interaction types and library
  Item/             - Item components, pickup collision, container library, registry
  Movement/         - Movement components, character movement systems
  Rendering/        - Render manager, Niagara manager, ISM transforms
  Spawning/         - Entity spawner, spawner actor, spawn library
  UI/               - FlecsUI subsystem, message subsystem, HUD, inventory, loot
  Utils/            - Time dilation stack, cone impulse, ledge detector, spawn utils
  Weapon/           - Weapon/projectile components, damage/weapon systems, libraries

Plugins/
  Artillery/          - Core Artillery framework
  Barrage/            - Jolt Physics wrapper
  BarrageCollision/   - Collision detection utilities
  BarrageTests/       - Physics test suite
  Bristlecone/        - Networking
  Cabling/            - Constraint system
  Enace/              - Entity framework utilities
  FlecsBarrage/       - Flecs-Barrage bridge (bidirectional binding)
  FlecsIntegration/   - UnrealFlecs, SolidMacros, FlecsLibrary
  FlecsUI/            - Model/View UI framework (CommonUI, lock-free sync)
  ImGui/              - Debug UI
  LocomoCore/         - Spatial utilities
  NiagaraUIRenderer/  - Niagara UI rendering
  Phosphorus/         - Rendering utilities
  SkeletonKey/        - Entity identity system
  Thistle/            - Utility plugin
  UE4CMake/           - CMake integration for UE
```

## Features

- **Unified Entity System** - Data-driven entity spawning with composable profiles (physics, render, health, damage, projectile, weapon, interaction, container, destructible)
- **Lock-Free Physics Binding** - Bidirectional Entity-Physics mapping without locks
- **Collision Pair System** - Tag-based collision handling (damage, bounce, pickup, destructible)
- **Weapon System** - Fire rate, burst, reload, ammo with sim-thread firing
- **Item & Container System** - Grid/Slot/List containers, inventory UI with drag-drop
- **Destructible Objects** - Constraint-based fragmentation, debris pool, world anchors
- **Interaction System** - Barrage raycast-based interaction with ECS entities
- **Time Dilation** - Multi-source time dilation stack with player speed compensation
- **Render Interpolation** - Sub-tick transform smoothing for characters and ISM entities
- **Door System** - Physics-based doors with ECS state
- **Abilities** - Ability resources, lifecycle management, tick functions

## Documentation

See [CLAUDE.md](CLAUDE.md) for detailed technical documentation including:
- Development principles and architecture
- ECS patterns (Static/Instance prefab inheritance)
- Quick start guide (projectiles, characters, spawners)
- Unified Spawn API and Blueprint API reference
- Collision pair system and system execution order
- Time dilation and render interpolation
- Debugging tips and known issues

## Credits

- **Barrage:** Hedra, Breach Dogs, Oversized Sun Inc.
- **Jolt Physics:** Jorrit Rouwe
- **Flecs ECS:** Sander Mertens
