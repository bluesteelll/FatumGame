# Coding Standards

This document defines the coding standards for FatumGame. Every contributor must follow these rules. They exist to prevent bugs, keep the codebase consistent, and make code reviewable.

---

## Core Principles

### No Workarounds

!!! danger "NEVER use workarounds"
    Find and fix the **root cause**. A workaround today becomes a mysterious crash next month. If the fix requires an architectural change, escalate to the architect -- do not paper over the problem.

```cpp
// WRONG: "Fix" a null pointer by adding a null check
if (Entity.try_get<FHealthInstance>())
{
    // silently skip if missing
}

// RIGHT: Understand WHY the component is missing and fix the spawn/setup path
check(Entity.has<FHealthInstance>());  // Crash at point of error
const auto& Health = Entity.get<FHealthInstance>();
```

### Fail-Fast

!!! warning "Use `check()`, `ensure()`, `checkf()` for every precondition"
    A crash at the point of error is infinitely better than silent corruption that manifests three systems later. NEVER silently return. NEVER use default values to hide bugs.

| Macro | Behavior | Use When |
|-------|----------|----------|
| `check(expr)` | Fatal crash in all builds | Invariant that must NEVER be violated |
| `checkf(expr, fmt, ...)` | Fatal crash with message | Same, but needs context for debugging |
| `ensure(expr)` | Assert in dev, continues in shipping | Recoverable but unexpected condition |
| `ensureMsgf(expr, fmt, ...)` | Assert with message | Same, but needs context |

```cpp
void UFlecsArtillerySubsystem::BindEntityToBarrage(flecs::entity Entity, FSkeletonKey Key)
{
    checkf(Entity.is_alive(), TEXT("Cannot bind dead entity to Barrage key 0x%llX"), Key);
    checkf(Key != 0, TEXT("Cannot bind entity %llu to null BarrageKey"), Entity.id());

    FBLet* Prim = CachedBarrageDispatch->GetShapeRef(Key);
    checkf(Prim != nullptr, TEXT("No Barrage primitive for key 0x%llX"), Key);

    // ...
}
```

### Avoid Boilerplate

Extract repeated patterns into functions or templates. Use modern C++ features to reduce noise:

- **`auto`** for iterator types and long template return types
- **Range-based `for`** instead of index-based loops
- **Structured bindings** for pairs and tuples
- **Helper functions** for any pattern used more than twice

```cpp
// WRONG: Repeated pattern
if (auto* Health = Entity.try_get_mut<FHealthInstance>())
{
    Health->CurrentHP = FMath::Clamp(Health->CurrentHP - Damage, 0.f, Static.MaxHP);
    if (Health->CurrentHP <= 0.f)
    {
        Entity.add<FTagDead>();
    }
}

// RIGHT: Extracted to a function used by all damage sources
void ApplyDamageToEntity(flecs::entity Entity, float Damage, const FHealthStatic& Static);
```

---

## USTRUCT Rules

!!! danger "No aggregate initialization with GENERATED_BODY()"
    UHT-generated structs have hidden members. Aggregate initialization `{value}` silently assigns to the wrong field or fails to compile depending on the compiler.

```cpp
// WRONG: Aggregate init
entity.set<FItemInstance>({ 5 });

// WRONG: Designated initializers on USTRUCT (unreliable with GENERATED_BODY)
entity.set<FItemInstance>({ .Count = 5 });

// RIGHT: Named field assignment
FItemInstance Instance;
Instance.Count = 5;
entity.set<FItemInstance>(Instance);
```

!!! note "Default member initializers are fine"
    Always provide defaults in the struct definition itself:
    ```cpp
    USTRUCT()
    struct FHealthInstance
    {
        GENERATED_BODY()

        UPROPERTY()
        float CurrentHP = 0.f;

        UPROPERTY()
        float RegenAccumulator = 0.f;
    };
    ```

---

## Architecture Approval

!!! warning "Non-trivial changes require approval"
    Before implementing any feature that touches multiple files or involves design decisions, present the architecture plan and get explicit confirmation. Use plan mode for any such feature.

"Non-trivial" includes:

- New ECS components or tags
- New systems or changes to system ordering
- New threading primitives or cross-thread data flow
- New data asset types or profile structs
- Changes to the spawn pipeline
- Changes to the collision pipeline

---

## Naming Conventions

### ECS Components

| Pattern | Meaning | Example |
|---------|---------|---------|
| `FNameStatic` | Data on the **prefab** (shared by all instances of this type) | `FHealthStatic`, `FWeaponStatic` |
| `FNameInstance` | Data on **each entity** (unique per instance) | `FHealthInstance`, `FWeaponInstance` |
| `FTagName` | Zero-size tag (no data, just a marker) | `FTagDead`, `FTagProjectile`, `FTagItem` |
| `FPendingName` | Queued data awaiting processing | `FPendingDamage` |
| `FNameData` | General data struct (not static/instance split) | `FFragmentationData` |

### Files

| Pattern | Use | Example |
|---------|-----|---------|
| `FlecsCharacter_Aspect.cpp` | Partial implementation files for `AFlecsCharacter` | `FlecsCharacter_Combat.cpp`, `FlecsCharacter_UI.cpp` |
| `FlecsArtillerySubsystem_Domain.cpp` | System registration grouped by domain | `FlecsArtillerySubsystem_Systems.cpp`, `FlecsArtillerySubsystem_Items.cpp` |
| `FlecsXxxLibrary.h/cpp` | Blueprint function libraries | `FlecsContainerLibrary`, `FlecsDamageLibrary` |
| `FlecsXxxComponents.h` | Domain component headers | `FlecsHealthComponents.h`, `FlecsWeaponComponents.h` |
| `FlecsXxxProfile.h` | Data asset profile classes | `FlecsHealthProfile.h`, `FlecsWeaponProfile.h` |

### General C++ Naming

- **UE conventions**: `F` prefix for structs, `U` for UObjects, `A` for Actors, `E` for enums, `I` for interfaces
- **UPROPERTY specifiers**: `EditAnywhere` for editor-tunable, `BlueprintReadOnly` for BP access, always include `Category`
- **Boolean members**: `bPrefixName` (e.g., `bDestroyOnHit`, `bAreaDamage`)

---

## Include Order

Follow the existing codebase pattern:

```cpp
#include "MyHeader.h"           // 1. Matching header (FIRST)

#include "OtherProjectHeader.h" // 2. Project headers
#include "Domain/Components.h"

#include "PluginHeader.h"       // 3. Plugin headers (FlecsIntegration, FlecsBarrage, FlecsUI)

#include "Engine/Header.h"      // 4. UE Engine headers

#include <jolt/header.h>        // 5. Third-party headers (rare)
```

!!! note "Forward-declare where possible"
    Only `#include` what you actually use. If a pointer or reference is sufficient, forward-declare the type in the header and include in the `.cpp`.

---

## Comments

- **Do NOT comment the obvious**: `// Set health to max` adds no value
- **DO comment the non-obvious**: `// Must run before StepWorld because constraint anchors need committed positions`
- **DO comment workaround-for-engine-bugs** with a link or explanation of what the engine bug is
- **DO comment threading concerns**: Which thread runs this? Why is this atomic?

---

## UPROPERTY / UFUNCTION Macros

```cpp
// Data Asset field (editable in editor, visible in BP)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health")
float MaxHP = 100.f;

// Runtime-only field (not editable, not visible)
UPROPERTY()
float CurrentHP = 0.f;

// Blueprint-callable function
UFUNCTION(BlueprintCallable, Category = "Damage", meta = (WorldContext = "WorldContext"))
static void ApplyDamage(UObject* WorldContext, FSkeletonKey TargetKey, float Amount);
```

---

## Performance Guidelines

- **Zero allocations on hot paths** -- pre-allocate, use pools, reserve TArrays
- **Cache-friendly access** -- iterate components contiguously (Flecs does this naturally), avoid pointer chasing
- **No unnecessary copies** -- pass large structs by `const&`, use `MoveTemp()` for temporaries
- **No O(N^2) where O(N) or O(1) is possible** -- use maps for lookups, not linear scans
- **Prefer `TArray` over `TMap`** for small collections (cache locality beats hash overhead)
