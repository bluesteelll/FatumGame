# FatumGame

Unreal Engine 5.7 game project using **Flecs ECS** for gameplay logic and **Jolt Physics** (via Barrage) for high-performance physics simulation.

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
FSimulationWorker -> Simulation thread (60Hz, lock-free)
  DrainCommandQueue -> StackUp -> StepWorld -> BroadcastContactEvents -> progress()
Barrage            -> Jolt Physics integration
Flecs              -> Gameplay ECS (health, damage, items, weapons)
Game Thread        -> Cosmetics, rendering, ISM spawns
```

### Key Technologies

| Component | Description |
|-----------|-------------|
| **Flecs** | High-performance ECS via UnrealFlecs plugin |
| **Jolt Physics** | Fast physics via Barrage plugin |
| **FSimulationWorker** | 60Hz simulation thread (physics + ECS) |
| **SkeletonKey** | Type-safe entity identity system |

## Project Structure

```
Source/FatumGame/
  Flecs/
    Subsystem/      - Simulation subsystem (sim thread, collisions, binding)
    Components/     - ECS components (health, damage, items, weapons)
    Definitions/    - Entity definitions and profiles
    Spawner/        - Unified entity spawning API + render manager
    Character/      - FlecsCharacter implementation
    Library/        - Blueprint function libraries

Plugins/
    Barrage/        - Jolt Physics wrapper
    FlecsBarrage/   - Flecs-Barrage bridge (bidirectional binding)
    FlecsIntegration/ - UnrealFlecs, SolidMacros, FlecsLibrary
    SkeletonKey/    - Entity identity system
    LocomoCore/     - Spatial utilities
    BarrageTests/   - Physics test suite
```

## Features

- **Unified Entity System** - Data-driven entity spawning with composable profiles
- **Lock-Free Physics Binding** - Bidirectional Entity-Physics mapping without locks
- **Collision Pair System** - Data-driven collision handling via ECS
- **Item Prefab System** - Shared static data for items via Flecs prefabs
- **Container System** - Grid/Slot/List containers for inventory
- **Weapon System** - Fire rate, burst, reload, ammo with sim-thread firing

## Documentation

See [CLAUDE.md](CLAUDE.md) for detailed technical documentation including:
- Development principles
- Quick start guide
- API reference
- Debugging tips
- Known issues

## Credits

- **Barrage:** Hedra, Breach Dogs, Oversized Sun Inc.
- **Jolt Physics:** Jorrit Rouwe
- **Flecs ECS:** Sander Mertens
