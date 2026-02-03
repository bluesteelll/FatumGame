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
Bristlecone   -> Network inputs (UDP, deterministic rollback)
Cabling       -> Local inputs & keymapping
Artillery     -> Core simulation (~120Hz, separate thread)
Barrage       -> Jolt Physics integration
Flecs         -> Gameplay ECS (health, damage, items, containers)
Game Thread   -> Cosmetics, rendering (non-deterministic)
```

### Key Technologies

| Component | Description |
|-----------|-------------|
| **Flecs** | High-performance ECS via UnrealFlecs plugin |
| **Jolt Physics** | Fast physics via Barrage plugin |
| **Artillery** | 120Hz deterministic simulation thread |
| **Bristlecone** | Deterministic rollback networking |

## Project Structure

```
Source/FatumGame/
  Flecs/
    Subsystem/      - FlecsArtillerySubsystem (ECS-Physics bridge)
    Components/     - ECS components (health, damage, items)
    Definitions/    - Entity definitions and profiles
    Spawner/        - Unified entity spawning API
    Character/      - FlecsCharacter implementation
    Library/        - Blueprint function libraries

Plugins/
    Artillery/      - 120Hz simulation core
    Barrage/        - Jolt Physics wrapper
    Bristlecone/    - Deterministic networking
    Cabling/        - Input handling
    SkeletonKey/    - Entity identity system
    UnrealFlecs/    - Flecs ECS integration
```

## Features

- **Unified Entity System** - Data-driven entity spawning with composable profiles
- **Lock-Free Physics Binding** - Bidirectional Entity-Physics mapping without locks
- **Collision Pair System** - Data-driven collision handling via ECS
- **Item Prefab System** - Shared static data for items via Flecs prefabs
- **Container System** - Grid/Slot/List containers for inventory

## Documentation

See [CLAUDE.md](CLAUDE.md) for detailed technical documentation including:
- Development principles
- Quick start guide
- API reference
- Debugging tips
- Known issues

## Credits

- **Artillery/Barrage/Bristlecone:** Hedra, Breach Dogs, Oversized Sun Inc.
- **Jolt Physics:** Jorrit Rouwe
- **Flecs ECS:** Sander Mertens
