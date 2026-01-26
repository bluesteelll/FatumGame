# BarrageCollision

Collision processing system integrating Barrage physics with Phosphorus event dispatch.

## Overview

BarrageCollision provides a clean integration layer between:
- **Barrage** - Jolt Physics collision events
- **Phosphorus** - Generic event dispatch framework
- **SkeletonKey** - Entity identification (EEntityType)
- **Artillery** - Tag system for entities

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Barrage Physics                          │
│                  (Jolt collision events)                    │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│              UBarrageCollisionSubsystem                     │
│  • Listens to contact events                                │
│  • Builds FBarrageCollisionPayload                          │
│  • Provides Blueprint API                                   │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│           FBarrageCollisionDispatcher                       │
│  (TPhosphorusEntityDispatcher<FBarrageCollisionPayload,14>) │
├─────────────────────────────────────────────────────────────┤
│  Type Dispatch (O(1))  │  Tag Dispatch (O(log N))          │
│  EEntityType matrix    │  TPhosphorusDispatcher            │
└─────────────────────────────────────────────────────────────┘
```

## Usage

### Blueprint

```cpp
// Get subsystem
auto* Collision = UBarrageCollisionSubsystem::Get(GetWorld());

// Register type handler
Collision->RegisterTypeHandler(EEntityType::Projectile, EEntityType::Actor, MyHandler);

// Register tag handler
Collision->RegisterTag(TAG_Enemy);
Collision->RegisterTagHandler(TAG_Enemy, TAG_Weapon, EnemyWeaponHandler);
```

### C++ (Native)

```cpp
auto* Collision = UBarrageCollisionSubsystem::Get(GetWorld());

// Direct dispatcher access (no Blueprint delegate overhead)
Collision->GetDispatcher().RegisterTypeHandler(
    EEntityType::Projectile,
    EEntityType::Actor,
    FNativeCollisionHandler::CreateLambda([](const FBarrageCollisionPayload& P) {
        FSkeletonKey HitEntity = P.GetKeyOfType(EEntityType::Actor);
        // Handle collision
        return true; // consumed
    })
);
```

## Dispatch Priority

1. **Type handlers** (O(1)) - Checked first, based on EEntityType
2. **Native tag handlers** (O(log N)) - Higher priority tag handlers
3. **Blueprint tag handlers** (O(log N)) - Lower priority tag handlers

Handlers return `true` to consume the event (stop chain) or `false` to continue.

## Files

| File | Description |
|------|-------------|
| `BarrageCollision.h` | Convenience header (includes all) |
| `BarrageCollisionTypes.h` | FBarrageCollisionPayload, delegates, type aliases |
| `BarrageCollisionSubsystem.h` | UWorldSubsystem with Blueprint API |
| `BarrageCollisionModule.h` | Module definition |

## Dependencies

- Phosphorus (event dispatch framework)
- Barrage (physics events)
- SkeletonKey (entity types)
- ArtilleryRuntime (tag lookups)
- GameplayTags
