# Movement System Plan — Phases 3-7

## Overview

Five phases extending the hierarchical movement state machine.
Pattern: **Profile data** → **Movement component logic** → **Character wiring**.

**Existing (Phases 1-2):** PostureSM (Stand/Crouch/Prone), Sprint, Jump buffer + coyote time, landing compress, Barrage capsule sync, Ability System (`UMovementAbility` base).

**Implementation order:** 3 → 4 → 5 → 6 → 7 (dependencies flow forward)

### Layer Touches Per Phase

| Phase | Profile | Components.h | PostureSM | MovementComp | Character | InputTags | New Files | Barrage |
|-------|---------|-------------|-----------|--------------|-----------|-----------|-----------|---------|
| ~~3 Slide~~ | ~~Done~~ | ~~Done~~ | ~~—~~ | ~~Done~~ | ~~Done~~ | ~~—~~ | ~~SlideAbility.h/cpp~~ | ~~Capsule resize~~ |
| 4 Camera | Add params | — | — | Head bob, slide tilt | Manual rotation + consume offsets | — | — | — |
| 5 Mantle | Add params | Add EMantleType | — | MantleAbility | Mantle trigger (jump) | — | MantleAbility.h/.cpp | SetPosition during mantle |
| 6 Lean | Add params | Already has ELeanDirection | — | Add lean logic | Lean input handlers | Add LeanLeft/Right | — | UE traces for wall |
| 7 Animation | — | Add FAnimMovementData | — | Add getter | — | — | FatumAnimInstance.h/.cpp | — |

---

## Phase 3: Slide — COMPLETE

Implemented as `USlideAbility : UMovementAbility` (game-thread visuals/capsule) + `FSlideInstance` Flecs component (sim-thread physics in `PrepareCharacterStep`).

**What was built:**
- `SlideAbility.h/cpp` — CanActivate, OnActivated (capsule + EnqueueCommand), OnTick (eye height only), OnDeactivated (restore capsule + posture), HandleJumpRequest (slide-cancel jump), OnCrouchInput
- `FSlideInstance` in `FlecsMovementStatic.h` — `CurrentSpeed`, `Timer`, `SlideDirX`, `SlideDirZ` (sim-thread owned)
- `PrepareCharacterStep` — direct velocity control bypassing MoveTowards, deceleration at `SlideDeceleration` cm/s², direction captured from Jolt on first tick, minor steering via `SlideMinAcceleration`
- `SlideActive` atomic (sim→game) + 3-tick grace period for deactivation
- All slide params in `UFlecsMovementProfile` (including `SlideMinAcceleration`)

**Key architectural decisions:**
- Slide is an **Ability** (not inline CMC logic). `OwnsPosture() = true` — suspends PostureSM, ability manages capsule directly.
- Physics runs on **sim thread** via `FSlideInstance`. Game thread is thin wrapper.
- Direction is **locked at entry** with minor steering, not driven by input.

---

## Phase 4: Camera Effects (Advanced)

### Design

**Head bob:** Dual-axis procedural sine oscillation. Vertical = 2× frequency of horizontal → natural figure-8 Lissajous. Amplitude scales with speed. Suppressed during slide/mantle/ADS/fall.

```
BobVertical   = sin(timer * VertFreq * 2PI) * AmpV * SpeedScale
BobHorizontal = sin(timer * HorizFreq * 2PI) * AmpH * SpeedScale   // HorizFreq = VertFreq/2
```

**Slide tilt:** Camera roll toward lateral velocity direction during slide (2-5 degrees). FInterpTo transition.

**Camera rotation pipeline change:** Currently `bUsePawnControlRotation = true` — UE auto-rotates camera from ControlRotation. Switch to `bUsePawnControlRotation = false` + manually apply `SetWorldRotation(Pitch, Yaw, Roll)` to support additive roll effects (slide tilt, lean roll, head bob roll).

**BaseFOV fix:** Currently hardcoded as `float BaseFOV = 90.f` local in Tick(). Move to `UPROPERTY` on `AFlecsCharacter` for editor tuning.

### Current Camera Pipeline (before Phase 4)
```cpp
// BeginPlay (first-person):
FollowCamera->bUsePawnControlRotation = true;   // UE auto-rotates
bUseControllerRotationYaw = true;

// Tick() camera section:
FollowCamera->SetFieldOfView(90.f + FOVOffset);
FVector BaseEyePos(0.f, 0.f, EyeHeight + LandingOffset);
FollowCamera->SetRelativeLocation(BaseEyePos);
// No rotation calls — UE handles Pitch/Yaw automatically
```

### Target Camera Pipeline (after Phase 4)
```cpp
// BeginPlay (first-person):
FollowCamera->bUsePawnControlRotation = false;   // CHANGED: we control rotation
bUseControllerRotationYaw = true;                 // actor still follows yaw

// Tick() camera section:
FollowCamera->SetFieldOfView(BaseFOV + FOVOffset);
FVector CameraPos(BobH, 0.f, EyeHeight + LandingOffset + BobV);
FollowCamera->SetRelativeLocation(CameraPos);
FRotator ControlRot = GetControlRotation();
FRotator CameraRot(ControlRot.Pitch, ControlRot.Yaw, SlideTilt);  // Phase 6 adds + LeanRoll
FollowCamera->SetWorldRotation(CameraRot);
```

### Profile Params (new in UFlecsMovementProfile)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `BobVerticalAmplitude` | 0.6 | Walk vertical bob (cm) |
| `BobHorizontalAmplitude` | 0.3 | Walk horizontal bob (cm) |
| `BobVerticalFrequency` | 12.0 | Vertical oscillation (Hz) |
| `BobSprintMultiplier` | 1.4 | Amplitude scale during sprint |
| `BobCrouchMultiplier` | 0.5 | Amplitude scale during crouch |
| `BobInterpSpeed` | 10.0 | Fade in/out speed |
| `SlideCameraTiltAngle` | 5.0 | Camera roll during slide (degrees) |
| `SlideTiltInterpSpeed` | 8.0 | Roll transition speed |

### Architecture

**New state in UFatumMovementComponent:**
```cpp
// Head bob
float HeadBobTimer = 0.f;
float HeadBobVerticalOffset = 0.f;
float HeadBobHorizontalOffset = 0.f;
float HeadBobAmplitudeScale = 0.f;  // lerped 0→1 when moving

// Slide tilt
float SlideTiltCurrent = 0.f;
```

**New methods in UFatumMovementComponent:**
```cpp
void UpdateHeadBob(float DeltaTime);       // called from UpdateCameraEffects
void UpdateSlideTilt(float DeltaTime);     // called from UpdateCameraEffects
float GetHeadBobVerticalOffset() const;
float GetHeadBobHorizontalOffset() const;
float GetSlideTiltAngle() const;
```

**New member on AFlecsCharacter:**
```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
float BaseFOV = 90.f;
```

**Modified:**
- `UFatumMovementComponent::UpdateCameraEffects()` — add `UpdateHeadBob()` and `UpdateSlideTilt()` calls
- `AFlecsCharacter::BeginPlay()` — `FollowCamera->bUsePawnControlRotation = false` (first-person path)
- `AFlecsCharacter::Tick()` camera section — apply bob offsets to position, apply tilt + ControlRotation to rotation

### Implementation Steps
1. Add `BaseFOV` UPROPERTY to `AFlecsCharacter`, replace hardcoded 90.f
2. Add head bob + slide tilt params to `UFlecsMovementProfile`
3. Add bob/tilt state + UpdateHeadBob/UpdateSlideTilt to `UFatumMovementComponent::UpdateCameraEffects`
4. Switch to manual camera rotation in `AFlecsCharacter::BeginPlay` + `Tick()`

### Gotchas
- Do NOT reset BobTimer on mode change — causes visible snap
- Lerp HeadBobAmplitudeScale to zero on stop (0.15s), don't snap offsets
- Clamp total offset (5cm V, 3cm H) to prevent additive stacking extremes
- Motion sickness: keep roll under 0.3 degrees for regular bob. Slide roll OK at 2-5 degrees (intentional player action)
- Aim direction sync: `GetControlRotation()` is unaffected by manual camera rotation — aim stays correct
- Third-person mode: skip bob/tilt entirely (camera is on boom, not head)

---

## Phase 5: Mantle / Vault

### Design

**Vault** (50-120cm): Fast pop-over obstacle, ~0.35s. Keep momentum.
**Mantle** (120-200cm): Pull up onto ledge, ~0.55s. Slower.
**Below 50cm:** Jolt CharacterVirtual step-up handles natively.
**Above 200cm:** Rejected.

**Triggered on jump input when facing obstacle + moving forward.** NOT continuous per-frame.

**Detection algorithm (3 UE traces, game thread):**
1. **Forward sweep** (capsule, 75cm): obstacle ahead?
2. **Height trace** (ray down from above): find top surface
3. **Clearance check** (overlap at destination): can capsule fit?

**Execution:** `UMantleAbility` (game-thread thin wrapper) + `FMantleInstance` (sim-thread Flecs component) + `MantlePositionSystem` (Flecs system, runs AFTER StepWorld).

Phase 1 (Rise): Lerp up to grab point. 40% of time. EaseOut curve.
Phase 2 (Pull): Lerp forward onto ledge. 60% of time. EaseInOut curve.
Phase 3 (Land): Brief settle. Remove FMantleInstance.

**Cancel:** Crouch during Rising phase → `DeactivateAbility()` → `remove<FMantleInstance>` → gravity resumes.

### Barrage Integration (Flecs system approach — consistent with pipeline)

Mantle follows the same pattern as Slide: game-thread ability wrapper + sim-thread Flecs component + sim-thread logic. **No pipeline inversion.** Position readback stays unchanged.

```
PrepareCharacterStep:
  if entity has FMantleInstance → mLocomotionUpdate = Vec3::sZero() (suppress locomotion)

StepWorld:
  Jolt steps character with zero velocity + gravity → doesn't matter, gets overridden

progress() → MantlePositionSystem (runs AFTER StepWorld):
  for each entity with (FMantleInstance, FBarrageBody):
    compute lerp position from phase/timer/start/end
    CharacterVirtual->SetPosition(lerpedJoltPos)    // override Jolt result
    tick timer, advance phases
    if done → remove<FMantleInstance>

Game thread GetPosition():
  reads overridden position → normal Alpha-lerp → SetActorLocation
  No special flags needed. Pipeline unchanged.
```

### Profile Params

| Parameter | Default | Description |
|-----------|---------|-------------|
| `MantleForwardReach` | 60.0 | Forward trace distance (cm) |
| `MantleVaultMaxHeight` | 80.0 | Max vault height (cm) |
| `MantleMidMaxHeight` | 140.0 | Max mantle height (cm) |
| `MantleHighMaxHeight` | 200.0 | Max climb height (cm) |
| `MantleRiseDuration` | 0.25 | Rise phase duration (s) |
| `MantlePullDuration` | 0.3 | Pull phase duration (s) |
| `MantleLandDuration` | 0.1 | Land settle duration (s) |
| `MantleVaultSpeedMultiplier` | 0.8 | Speed preservation after vault |
| `bAutoMantleEnabled` | false | Auto-detect while falling |

### Architecture

**New files:** `MantleAbility.h/.cpp`

**New Flecs component** in FlecsMovementStatic.h:
```cpp
struct FMantleInstance
{
    float StartX, StartY, StartZ;   // Jolt coords, feet position at activation
    float EndX, EndY, EndZ;         // Jolt coords, target ledge top
    float Timer = 0.f;              // elapsed time in current phase
    float RiseDuration = 0.25f;     // from FMovementStatic
    float PullDuration = 0.3f;
    float LandDuration = 0.1f;
    uint8 Phase = 0;                // 0=Rise, 1=Pull, 2=Land
    uint8 MantleType = 0;           // EMantleType
};
```

**New enum** in FlecsMovementComponents.h:
```cpp
enum class EMantleType : uint8 { None, Vault, Mantle, HighClimb };
```

**New Flecs system:** `MantlePositionSystem` — registered during world setup, runs in `progress()` (after StepWorld). Reads `FMantleInstance` + `FBarrageBody`, sets `CharacterVirtual->SetPosition()`.

**`UMantleAbility : UMovementAbility` overrides (game-thread thin wrapper):**
```cpp
CanActivate()       — 3-trace detection (game-thread UE traces)
OnActivated()       — EnqueueCommand → entity.set<FMantleInstance>(start/end/durations)
OnTick(DeltaTime)   — eye height only (like SlideAbility)
OnDeactivated()     — EnqueueCommand → entity.remove<FMantleInstance>
HandleJumpRequest() — false (jump doesn't cancel, crouch does)
OwnsPosture()       — true (capsule may change during mantle)
GetMoveMode()       — ECharacterMoveMode::Mantle (or Vault)
```

**Integration points:**
- `RequestJump()` — `if (FindAbility<UMantleAbility>()->CanActivate()) ActivateAbility(...)` before normal jump
- `RequestCrouch()` — if mantle active → `DeactivateAbility()` (abort)
- `PrepareCharacterStep` — if entity has `FMantleInstance` → `mLocomotionUpdate = Vec3::sZero()`
- No changes to `ApplyBarrageSync` — pipeline unchanged

### Gotchas
- `CharacterVirtual::SetPosition()` in MantlePositionSystem — must run AFTER StepWorld (Flecs system ordering)
- Post-mantle: `ApplyForce(OtherForce)` ~250 cm/s forward via `EnqueueCommand` from `OnDeactivated`
- Camera during mantle: follows actor position via eye height. Allow free look (don't lock pitch/yaw)
- Thin walls: use capsule sweep (not line trace) for forward detection
- Ledge detection coordinates: UE traces give UE coords → convert to Jolt for FMantleInstance start/end
- Flecs worker thread access: MantlePositionSystem needs `EnsureBarrageAccess()` for Barrage calls

---

## Phase 6: Lean

### Design

**Camera-only lean.** Capsule and Barrage body do NOT move. Q/E (or custom keys) to peek. Hold-mode by default.

**NOT an ability** — lean is a parallel state layer (like sprint), not a movement mode override. Does not displace active abilities.

**Algorithm:**
```
TargetAlpha = -1 (left) / 0 (none) / +1 (right)
CurrentLeanAlpha = FInterpTo → TargetAlpha (speed 12)
WallClamp = line trace sideways, proportional distance clamping
LateralOffset = LeanAlpha * LeanMaxOffset * WallClamp * PostureMultiplier
RollAngle = LeanAlpha * LeanMaxRoll * WallClamp
```

**Constraints:** No lean during sprint, slide, mantle, prone. Reduced lean in crouch (0.6× multiplier).

### Profile Params

| Parameter | Default | Description |
|-----------|---------|-------------|
| `LeanMaxCameraOffset` | 30.0 | Max lateral camera offset (cm) |
| `LeanMaxRollAngle` | 8.0 | Max camera roll (degrees) |
| `LeanInterpSpeed` | 12.0 | Transition speed |
| `LeanCrouchMultiplier` | 0.6 | Lean reduction in crouch |
| `LeanWallCheckDistance` | 50.0 | Wall trace distance (cm) |

### Architecture

**New input tags:** `InputTag.LeanLeft`, `InputTag.LeanRight`

**State in UFatumMovementComponent (NOT an ability):**
```cpp
bool bWantsLeanLeft = false;
bool bWantsLeanRight = false;
float CurrentLeanAlpha = 0.f;   // -1..+1
float LeanWallClamp = 1.f;
```

**New methods:**
- `RequestLeanLeft(bool)`, `RequestLeanRight(bool)` — commands
- `UpdateLean(DeltaTime)` — called from UpdateCameraEffects, interp + wall check
- `GetLeanCameraOffset()`, `GetLeanRollAngle()` — queries for camera

**Camera (requires Phase 4 manual rotation):**
```cpp
FVector CameraPos(BobH + LeanOffset, 0.f, EyeHeight + LandingOffset + BobV);
FRotator CameraRot(ControlRot.Pitch, ControlRot.Yaw, SlideTilt + LeanRoll);
```

### Gotchas
- Direction switch: don't snap through center. FInterpTo handles this naturally
- Wall check every frame while leaning (doors can open)
- Sprint suppresses lean (smooth return to center, not snap)
- Lean is **suppressed** when `ActiveAbility != nullptr` (slide, mantle block lean)

---

## Phase 7: Animation Hooks

### Design

Custom `UAnimInstance` subclass with **NativeUpdateAnimation** data gathering. AnimBP reads UPROPERTY fields via Property Access. **PULL model** (AnimInstance reads from character, never written to from outside).

### Architecture

**New files:** `FatumAnimInstance.h/.cpp`

**Data exposed:**

| Field | Type | Source |
|-------|------|--------|
| Posture | ECharacterPosture | FatumMovement->GetCurrentPosture() |
| MoveMode | ECharacterMoveMode | FatumMovement->GetCurrentMoveMode() |
| Speed | float | Character->GetVelocity().Size2D() |
| VerticalSpeed | float | Character->GetVelocity().Z |
| MoveDirection | float | Computed angle relative to aim (-180..180) |
| bIsGrounded | bool | FatumMovement->IsMovingOnGround() |
| bIsSprinting | bool | FatumMovement->IsSprinting() |
| bIsSliding | bool | MoveMode == Slide |
| bIsMantling | bool | MoveMode == Mantle or Vault |
| LeanAlpha | float | FatumMovement->GetLeanAlpha() |
| AimPitch | float | Controller rotation |
| AimYaw | float | Relative to actor forward |
| PostureBlendAlpha | float | PostureSM transition progress |

**Thread safety:** NEVER read from Flecs/ECS. All data from game-thread character and movement component.

### Gotchas
- NativeUpdateAnimation can fire before BeginPlay — null-check character
- Use `TryGetPawnOwner()` and cache it
- Derive booleans like `bJustStartedSliding` using prev-frame comparison for state machine triggers

---

## Cross-Phase Interaction Matrix

| | Sprint | Crouch | Prone | Jump | Slide | HeadBob | Mantle | Lean |
|---|---|---|---|---|---|---|---|---|
| **Sprint** | — | End sprint | End sprint | Sprint-jump | Entry condition | Sprint bob | Mantle check | Disables lean |
| **Crouch** | End sprint | — | Transition | Crouch-jump | Uses crouch capsule | Crouch bob | Allowed trigger | Reduced lean |
| **Prone** | N/A | Transition | — | Blocked | Blocked | Prone bob | Blocked | Blocked |
| **Jump** | Preserve speed | Crouch-jump | Blocked | — | Slide-jump | Suppressed | Mid-air mantle | Suppressed |
| **Slide** | Entry | Exit→crouch | Blocked | Slide-jump exit | — | Dampened | Blocked | Blocked |
| **HeadBob** | Sprint params | Crouch params | Prone params | Suppressed | Slide-specific | — | Suppressed | Active |
| **Mantle** | N/A | Cancel | N/A | Trigger | Blocked | Suppressed | — | Blocked |
| **Lean** | Blocked | Active | Blocked | Suppressed | Blocked | Active | Blocked | — |

---

## Implementation Steps (Ordered)

### Phase 3 — Slide (COMPLETE)
~~1. Add slide params to `UFlecsMovementProfile`~~
~~2. Create `USlideAbility` with sim-thread `FSlideInstance`~~
~~3. Wire: RequestCrouch gate, HandleJumpRequest, PrepareCharacterStep direct velocity control~~

### Phase 4 — Camera Effects
4. Add `BaseFOV` UPROPERTY to `AFlecsCharacter`, replace hardcoded 90.f
5. Add head bob + slide tilt params to `UFlecsMovementProfile`
6. Implement UpdateHeadBob, UpdateSlideTilt in `UFatumMovementComponent::UpdateCameraEffects`
7. Switch `FollowCamera->bUsePawnControlRotation = false` (first-person path)
8. Update `AFlecsCharacter::Tick()` — apply bob position + manual rotation with tilt roll

### Phase 5 — Mantle/Vault
9. Add `EMantleType` + `FMantleInstance` to `FlecsMovementComponents.h` / `FlecsMovementStatic.h`
10. Add mantle params to `UFlecsMovementProfile` + `FMovementStatic`
11. Create `MantleAbility.h/.cpp` (game-thread: 3-trace detection, EnqueueCommand, eye height)
12. Register `MantlePositionSystem` in Flecs world (sim-thread: phase lerp, SetPosition, exit)
13. Register ability in CMC, wire into RequestJump (before normal jump) and RequestCrouch (abort)
14. In `PrepareCharacterStep`: suppress `mLocomotionUpdate` when `FMantleInstance` present

### Phase 6 — Lean
15. Add lean params to `UFlecsMovementProfile`
16. Add `InputTag.LeanLeft`, `InputTag.LeanRight` to `FatumInputTags`
17. Implement lean logic in `UFatumMovementComponent` (UpdateLean in UpdateCameraEffects, wall check)
18. Wire lean input in `AFlecsCharacter` + consume lean offsets in camera update (Position Y + Roll)

### Phase 7 — Animation Hooks
19. Create `FatumAnimInstance.h/.cpp` with NativeUpdateAnimation data gathering
20. Add `FAnimMovementData` struct to `FlecsMovementComponents.h` (optional, for struct-based access)

---

## Sources
- [Brink SMART System — GDC 2012](https://gdcvault.com/play/1016220/Vault-Slide-Mantle-Building-Brink) — Vault/Slide/Mantle architecture
- [Apex Legends Slide Tech](https://apexmovement.tech/) — Friction-based slide, slide-jump
- [Call of Duty MW3 Slide](https://www.dexerto.com/call-of-duty/how-to-slide-cancel-in-modern-warfare-3-2332662/) — Slide-cancel
- [Valve Camera Bob](https://developer.valvesoftware.com/wiki/Camera_Bob) — Sine-wave bob standard
- [Microsoft Accessibility 117](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117) — Motion sickness: disable option required
- [UE5 Property Access](https://dev.epicgames.com/documentation/en-us/unreal-engine/property-access-in-unreal-engine) — AnimBP data flow
- [Lyra Locomotion Tutorial](https://dev.epicgames.com/community/learning/tutorials/7e3m/) — ThreadSafe animation pattern
- [R6S Lean Discussion](https://steamcommunity.com/app/359550/discussions/0/2268068817155075755/) — Lean mechanics reference
