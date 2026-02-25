# Movement System Plan — Phases 3-7

## Overview

Five phases extending the hierarchical movement state machine.
Pattern: **Profile data** → **Movement component logic** → **Character wiring**.

**Existing (Phases 1-2):** PostureSM (Stand/Crouch/Prone), Sprint, Jump buffer + coyote time, landing compress, Barrage capsule sync.

**Implementation order:** 3 → 4 → 5 → 6 → 7 (dependencies flow forward)

### Layer Touches Per Phase

| Phase | Profile | Components.h | PostureSM | MovementComp | Character | InputTags | New Files | Barrage |
|-------|---------|-------------|-----------|--------------|-----------|-----------|-----------|---------|
| 3 Slide | Add params | — | — | Add slide logic | Modify RequestCrouch | — | — | Capsule resize |
| 4 Camera | Add params | — | — | Head bob, slide tilt | Consume new offsets + manual rotation | — | — | — |
| 5 Mantle | Add params | Add EMantleType | — | Add mantle SM | Mantle trigger (jump) | — | FMantleStateMachine.h/.cpp | UE traces |
| 6 Lean | Add params | Already has ELeanDirection | — | Add lean logic | Lean input handlers | Add LeanLeft/Right | — | UE traces for wall |
| 7 Animation | — | Add FAnimMovementData | — | Add getter | — | — | FatumAnimInstance.h/.cpp | — |

---

## Phase 3: Slide

### Design (from mechanics research)

**Key insight:** Slide is a **movement mode** (`ECharacterMoveMode::Slide`), NOT a posture. Uses crouch capsule but has velocity-dependent entry/exit. Based on Apex Legends / Titanfall 2 / CoD approach: friction-decelerating, slope-sensitive, cancellable by jump.

**Entry conditions (ALL):**
1. Currently sprinting
2. Crouch input pressed
3. Grounded
4. Horizontal speed >= `SlideMinEntrySpeed` (default 500 cm/s)

**Per-tick:** Decelerate by `SlideDeceleration` cm/s^2. Slope factor modifies decel (downhill = slower decel, uphill = faster).

**Exit conditions (ANY):**
- Speed < `SlideMinExitSpeed` (150 cm/s)
- Timer expired (`SlideMaxDuration` = 1.5s safety cap)
- Player jumps → **slide-jump** (preserve horizontal momentum + jump Z velocity)
- Player releases crouch (hold mode) → stand if ceiling allows
- Leaves ground → Fall mode
- `bSlideRequiresForwardInput` && no forward input

**Slide-jump interaction:** Horizontal speed preserved at jump moment. This is the core advanced technique.

**Camera:** Eye height transitions to `SlideEyeHeight` (25cm, lower than crouch). Faster transition speed (16 vs 14 for crouch).

### Architecture

**Profile params (UFlecsMovementProfile):**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `SlideMinEntrySpeed` | 500.0 | Min horizontal speed to start slide (cm/s) |
| `SlideInitialSpeedBoost` | 50.0 | Speed added on slide start (cm/s) |
| `SlideDeceleration` | 400.0 | Deceleration on flat ground (cm/s^2) |
| `SlideMinExitSpeed` | 100.0 | Speed below which slide auto-ends (cm/s) |
| `SlideMaxDuration` | 1.5 | Safety cap on flat ground (seconds) |
| `SlideJumpVelocity` | 500.0 | Jump Z velocity from slide-cancel (cm/s) |
| `SlideEyeHeight` | 25.0 | Camera height during slide (cm) |
| `SlideTransitionSpeed` | 16.0 | Eye height interp speed (FInterpTo) |
| `SlideGroundFriction` | 0.1 | BrakingDeceleration override during slide |
| `bSlideRequiresForwardInput` | false | If true, releasing W ends slide |

**State in UFatumMovementComponent:**
```
bool bIsSliding, float SlideTimer, float SlideCurrentSpeed, bool bSlideCrouchHeld
```

**HSM flow:**
```
TickHSM → UpdatePosture → UpdateSlide [NEW] → UpdateMovementLayer
```

**Modified methods:**
- `RequestCrouch()` — if sprinting + fast enough → `BeginSlide()`, else normal crouch
- `RequestJump()` — if sliding → `EndSlide()` + slide-jump
- `GetMaxSpeed()` — case Slide: return `SlideCurrentSpeed`
- `GetMaxBrakingDeceleration()` — if sliding: return low friction
- `GetMaxAcceleration()` — if sliding: return 100 (minimal, direction tweaking only)
- `UpdateMovementLayer()` — if `bIsSliding` → `TransitionMoveMode(Slide)`, skip normal logic

**Capsule:** Uses crouch dimensions. PostureSM stays at Standing — slide manages capsule directly. On exit, restores based on whether crouch is held.

### Edge Cases
- Slide off ledge → end slide, enter Fall, preserve momentum
- Slide into wall → speed drops below min → end slide
- Sprint + crouch when slow → normal crouch (speed gate)
- Slide + prone → blocked (slide has priority)

---

## Phase 4: Camera Effects (Advanced)

### Design

**Head bob:** Dual-axis procedural sine oscillation. Vertical = 2× frequency of horizontal → natural figure-8 Lissajous. Amplitude scales with speed. Suppressed during slide/mantle/ADS/fall.

```
BobVertical   = sin(timer * VertFreq * 2PI) * AmpV * SpeedScale
BobHorizontal = sin(timer * HorizFreq * 2PI) * AmpH * SpeedScale   // HorizFreq = VertFreq/2
```

**Slide tilt:** Camera roll toward lateral velocity direction during slide (2-5 degrees). FInterpTo transition.

**Camera rotation pipeline change:** `bUsePawnControlRotation = false` for first-person camera. Manually set `FollowCamera->SetWorldRotation(ControlRot.Pitch, ControlRot.Yaw, AdditiveRoll)` to support roll effects (slide tilt, lean roll, bob roll).

### Profile Params

| Parameter | Default | Description |
|-----------|---------|-------------|
| `BobVerticalAmplitude` | 0.6 | Walk vertical bob (cm) |
| `BobHorizontalAmplitude` | 0.3 | Walk horizontal bob (cm) |
| `BobVerticalFrequency` | 12.0 | Vertical oscillation (Hz) |
| `BobHorizontalFrequency` | 6.0 | Horizontal oscillation (Hz, half of vertical) |
| `BobSprintMultiplier` | 1.4 | Amplitude scale during sprint |
| `BobCrouchMultiplier` | 0.5 | Amplitude scale during crouch |
| `BobInterpSpeed` | 10.0 | Fade in/out speed |
| `SlideCameraTiltAngle` | 5.0 | Camera roll during slide (degrees) |
| `SlideTiltInterpSpeed` | 8.0 | Roll transition speed |

### Architecture

**New in UFatumMovementComponent:**
```
float HeadBobTimer, HeadBobVertical, HeadBobHorizontal, HeadBobAmplitudeScale
float SlideTiltCurrent
void UpdateHeadBob(DeltaTime), void UpdateSlideTilt(DeltaTime)
Getters: GetHeadBobVerticalOffset(), GetHeadBobHorizontalOffset(), GetSlideTiltAngle()
```

**Camera application in AFlecsCharacter::Tick():**
```cpp
FVector CameraPos(BobH + LeanY, 0.f, EyeH + LandingZ + BobV);
FollowCamera->SetRelativeLocation(CameraPos);

FRotator CameraRot(ControlRot.Pitch, ControlRot.Yaw, SlideTilt + LeanRoll);
FollowCamera->SetWorldRotation(CameraRot);
```

### Gotchas
- Do NOT reset BobTimer on mode change — causes visible snap
- Lerp offsets to zero on stop (0.15s), don't snap
- Clamp total offset (5cm V, 3cm H) to prevent additive stacking extremes
- Motion sickness: keep roll under 0.3 degrees for regular bob. Slide roll OK at 2-5 degrees (intentional player action)

---

## Phase 5: Mantle / Vault

### Design

**Vault** (50-120cm): Fast pop-over obstacle, ~0.35s. Keep momentum.
**Mantle** (120-200cm): Pull up onto ledge, ~0.55s. Slower.
**Below 50cm:** CMC step-up handles natively.
**Above 200cm:** Rejected.

**Triggered on jump input when facing obstacle + moving forward.** NOT continuous per-frame.

**Detection algorithm (3 UE traces, game thread):**
1. **Forward sweep** (capsule, 75cm): obstacle ahead?
2. **Height trace** (ray down from above): find top surface
3. **Clearance check** (overlap at destination): can capsule fit?

**Execution:** 3-phase lerp of actor position. CMC disabled (`MOVE_None`). Capsule collision stays active. Barrage body follows via normal Tick sync.

Phase 1 (Rise): Lerp up to grab point. 40% of time. EaseOut curve.
Phase 2 (Pull): Lerp forward onto ledge. 60% of time. EaseInOut curve.
Phase 3 (Land): Brief settle. Restore CMC to Walking.

**Cancel:** Crouch during Rising phase → abort, drop with gravity.

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

**New files:** `FMantleStateMachine.h/.cpp`

**New enum** in FlecsMovementComponents.h:
```cpp
enum class EMantleType : uint8 { None, Vault, Mantle, HighClimb };
```

**State machine phases:** None → Rising → Pulling → Landing → None

**Integration:**
- `TickHSM()` — if mantling, call `UpdateMantle()` and skip all other HSM updates
- `RequestJump()` — `TryBeginMantle()` before normal jump logic
- `RequestCrouch()` — if mantling → `AbortMantle()`
- `GetMaxSpeed()` — case Mantle/Vault: return 0

### Gotchas
- Barrage body: normal Tick sync handles it (actor moves → Barrage follows)
- Camera during mantle: follows actor position. Allow free look (don't lock pitch/yaw)
- Thin walls: use capsule sweep (not line trace) for forward detection
- Post-mantle: give ~250 cm/s forward velocity, don't start from zero

---

## Phase 6: Lean

### Design

**Camera-only lean.** Capsule and Barrage body do NOT move. Q/E (or custom keys) to peek. Hold-mode by default.

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

**State in UFatumMovementComponent:**
```
ELeanDirection CurrentLeanDirection, float CurrentLeanAlpha (-1..1)
bool bWantsLeanLeft, bWantsLeanRight, float LeanWallClamp
```

**New methods:**
- `RequestLeanLeft(bool)`, `RequestLeanRight(bool)` — commands
- `UpdateLean(DeltaTime)` — interp + wall check
- `GetLeanCameraOffset()`, `GetLeanRollAngle()` — queries for camera

**Camera:** `LeanY` and `LeanRoll` consumed in `AFlecsCharacter::Tick()` alongside head bob and slide tilt.

### Gotchas
- Direction switch: don't snap through center. FInterpTo handles this naturally
- Wall check every frame while leaning (doors can open)
- Sprint suppresses lean (smooth return to center, not snap)

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
| bIsMantling | bool | FatumMovement->IsMantling() |
| LeanDirection | ELeanDirection | FatumMovement->GetLeanDirection() |
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

### Phase 3 — Slide
1. Add slide params to `UFlecsMovementProfile`
2. Add slide state + logic to `UFatumMovementComponent` (BeginSlide, EndSlide, UpdateSlide)
3. Wire into existing: modify RequestCrouch, RequestJump, GetMaxSpeed, GetMaxBrakingDeceleration, TickHSM, UpdateMovementLayer

### Phase 4 — Camera Effects
4. Add head bob + slide tilt params to `UFlecsMovementProfile`
5. Implement UpdateHeadBob, UpdateSlideTilt in `UFatumMovementComponent`
6. Update `AFlecsCharacter::Tick()` — consume bob/tilt offsets, switch to manual camera rotation (`bUsePawnControlRotation = false`)

### Phase 5 — Mantle/Vault
7. Add `EMantleType` to `FlecsMovementComponents.h`
8. Add mantle params to `UFlecsMovementProfile`
9. Create `FMantleStateMachine.h/.cpp` (DetectLedge, Begin, Tick, Abort)
10. Integrate into `UFatumMovementComponent` (UpdateMantle, TryBeginMantle, modify RequestJump/RequestCrouch, TickHSM)

### Phase 6 — Lean
11. Add lean params to `UFlecsMovementProfile`
12. Add `InputTag.LeanLeft`, `InputTag.LeanRight` to `FatumInputTags`
13. Implement lean logic in `UFatumMovementComponent` (UpdateLean, wall check)
14. Wire lean input in `AFlecsCharacter` + consume lean offsets in camera update

### Phase 7 — Animation Hooks
15. Create `FatumAnimInstance.h/.cpp` with NativeUpdateAnimation data gathering
16. Add `FAnimMovementData` struct to `FlecsMovementComponents.h` (optional, for struct-based access)

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
