# Build Setup

> Requirements and configuration for building FatumGame from source.

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| **Unreal Engine** | 5.7 | Source or launcher build |
| **Visual Studio** | 2022 | VC++ toolset 14.44 |
| **Windows SDK** | 10.0.22621 | Win11 SDK |
| **CMake** | 3.30.2+ | Required for Jolt Physics build (via UE4CMake plugin) |
| **Git LFS** | Latest | Content assets are tracked with LFS |

---

## Module Dependencies

`FatumGame.Build.cs` declares these public dependencies:

### Engine Modules

| Module | Purpose |
|--------|---------|
| `Core`, `CoreUObject`, `Engine` | UE fundamentals |
| `InputCore`, `EnhancedInput` | Input system |
| `UMG`, `Slate`, `SlateCore` | UI rendering |
| `CommonUI` | Activatable panels, input routing |
| `GameplayTags` | Tag-based classification |
| `Niagara` | Particle VFX |

### Project Plugins

| Plugin | Module(s) | Purpose |
|--------|-----------|---------|
| **Barrage** | `Barrage` | Jolt Physics integration |
| **SkeletonKey** | `SkeletonKey` | 64-bit entity identity |
| **FlecsIntegration** | `UnrealFlecs`, `FlecsLibrary` | Flecs ECS |
| **FlecsBarrage** | `FlecsBarrage` | ECS ↔ Physics bridge |
| **FlecsUI** | `FlecsUI` | UI framework |
| **SolidMacros** | `SolidMacros` | Macro utilities |

### Include Paths

The build file adds Public and Private include paths for all 16 domain folders:

```
Core, Character, Movement, Weapon, Abilities, Climbing, Stealth,
Destructible, Door, Item, Interaction, Spawning, Rendering, UI,
Input, Utils, Definitions, Vitals
```

Each domain's `Public/` directory is added as a public include path. `Private/` directories are added as private includes to prevent header leaking.

---

## Plugin Build Chain

### Jolt Physics (via Barrage)

Jolt is built from source via CMake. The `UE4CMake` plugin handles the CMake → UE build integration:

```
Plugins/UE4CMake/  → CMake target for Jolt
Plugins/Barrage/   → UE wrapper around Jolt APIs
```

Jolt is compiled as a static library linked into the Barrage module. No separate Jolt DLL or runtime dependency.

### Flecs

Flecs is compiled as part of the `UnrealFlecs` module (source included in plugin). The `FlecsLibrary` module provides UE-specific helpers.

---

## First Build

1. Clone the repository (ensure Git LFS is initialized):
   ```bash
   git clone <repo-url>
   cd FatumGame
   git lfs pull
   ```

2. Generate project files:
   ```bash
   # Using UE's batch file
   "C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/GenerateProjectFiles.bat" \
       "D:/Unreal Engine Projects/FatumGame/FatumGame.uproject" -Game
   ```

3. Open `FatumGame.sln` in Visual Studio 2022

4. Build configuration: **Development Editor** / **Win64**

5. Build and run

---

## Common Build Issues

### CMake Not Found

Jolt build fails if CMake is not on PATH. Install CMake 3.30.2+ and ensure it's accessible from the VS Developer Command Prompt.

### Git LFS Objects Missing

If `.uasset` files show as pointer files (small text files), run:
```bash
git lfs install
git lfs pull
```

### Intermediate Corruption

If you encounter unexplained build errors (especially after engine version changes):
```bash
# Delete generated files — forces full rebuild
rm -rf Binaries/ Intermediate/
rm -rf Plugins/*/Binaries/ Plugins/*/Intermediate/
```

### TUniquePtr in UHT Headers

`TUniquePtr<ForwardDeclaredType>` in UCLASS headers causes C4150 because UHT-generated `.gen.cpp` files instantiate constructor/destructor. Use raw pointer with manual `new`/`delete` in `Initialize()`/`Deinitialize()` instead.
