# Actors

> UE Actor classes used in FatumGame. Most gameplay entities are Flecs entities (not actors), but characters, spawners, and stealth elements use actor wrappers.

---

## AFlecsCharacter

**Header:** `Character/Public/FlecsCharacter.h`
**Parent:** `ACharacter`

The main player and NPC actor. Bridges UE (input, camera, UI) with the Flecs/Barrage simulation.

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `FollowCamera` | `UCameraComponent*` | Third/first person camera |
| `SpringArm` | `USpringArmComponent*` | Camera boom |
| `DilationStack` | `FTimeDilationStack` | Time dilation priority stack |

### Test Properties

| Property | Type | Description |
|----------|------|-------------|
| `TestContainerDefinition` | `UFlecsEntityDefinition*` | Container to spawn (E key) |
| `TestItemDefinition` | `UFlecsEntityDefinition*` | Item to add (E key) |
| `TestEntityDefinition` | `UFlecsEntityDefinition*` | Generic entity to spawn (E key) |

### Implementation Files

14 `.cpp` files split by concern. See [Character System](../systems/character-system.md) for details.

---

## AFlecsEntitySpawnerActor

**Header:** `Spawning/Public/FlecsEntitySpawnerActor.h`
**Parent:** `AActor`

Level-placeable entity spawner. Creates an ECS entity from a data asset on BeginPlay.

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `EntityDefinition` | `UFlecsEntityDefinition*` | — | Data asset to spawn (required) |
| `InitialVelocity` | `FVector` | (0,0,0) | Initial velocity |
| `bOverrideScale` | `bool` | false | Override scale from RenderProfile |
| `ScaleOverride` | `FVector` | (1,1,1) | Custom scale |
| `bSpawnOnBeginPlay` | `bool` | true | Auto-spawn on play start |
| `bDestroyAfterSpawn` | `bool` | true | Destroy the actor after spawning the entity |
| `bShowPreview` | `bool` | true | Show mesh preview in editor viewport |

### Usage

1. Place in level
2. Set `EntityDefinition` to any data asset
3. Adjust `InitialVelocity` if needed
4. Play — entity spawns and actor self-destructs

For manual control: set `bSpawnOnBeginPlay = false`, call `SpawnEntity()` from Blueprint/C++.

---

## AFlecsLightSourceActor

**Header:** `Stealth/Public/FlecsLightSourceActor.h`
**Parent:** `AActor`

Gameplay-only stealth light source. Registers a Flecs entity with `FStealthLightStatic` on BeginPlay.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `StealthLightProfile` | `UFlecsStealthLightProfile*` | Light configuration |

### Notes

- Does **not** emit visible light — place alongside visual light actors
- Registers ECS entity with `FTagStealthLight` + `FStealthLightStatic` + `FWorldPosition`
- Used by stealth calculation systems to determine character visibility

---

## AFlecsNoiseZoneActor

**Header:** `Stealth/Public/FlecsNoiseZoneActor.h`
**Parent:** `AActor`

Surface noise modifier zone. Registers a Flecs entity with `FNoiseZoneStatic` on BeginPlay.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `NoiseZoneProfile` | `UFlecsNoiseZoneProfile*` | Zone configuration |

### Notes

- Defines a box volume where character footsteps generate specific noise levels
- Registers ECS entity with `FTagNoiseZone` + `FNoiseZoneStatic` + `FWorldPosition`
- Box extent defined by `NoiseZoneProfile.Extent` (half-extents in cm)
