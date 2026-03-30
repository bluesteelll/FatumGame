# Data Assets & Profiles

> All entity types are configured via `UFlecsEntityDefinition` data assets in the editor. Each definition references profiles that control specific aspects (physics, health, rendering, etc.). This page lists every profile and its fields.

---

## UFlecsEntityDefinition

The master data asset. Created in Content Browser → Data Asset → `FlecsEntityDefinition`.

### Profile References

| Property | Type | Purpose |
|----------|------|---------|
| `ItemDefinition` | `UFlecsItemDefinition*` | Item metadata (name, icon, stack) |
| `PhysicsProfile` | `UFlecsPhysicsProfile*` | Collision, mass, gravity |
| `RenderProfile` | `UFlecsRenderProfile*` | Mesh, material, scale |
| `HealthProfile` | `UFlecsHealthProfile*` | HP, armor, regen |
| `DamageProfile` | `UFlecsDamageProfile*` | Damage, type, area |
| `ProjectileProfile` | `UFlecsProjectileProfile*` | Speed, lifetime, bounces |
| `WeaponProfile` | `UFlecsWeaponProfile*` | Fire rate, ammo, recoil |
| `ContainerProfile` | `UFlecsContainerProfile*` | Grid, slots, weight |
| `MagazineProfile` | `UFlecsMagazineProfile*` | Caliber, capacity, ammo types |
| `AmmoTypeDefinition` | `UFlecsAmmoTypeDefinition*` | Loose ammo item type |
| `QuickLoadProfile` | `UFlecsQuickLoadProfile*` | Stripper clip / speedloader |
| `InteractionProfile` | `UFlecsInteractionProfile*` | Range, type, prompt |
| `NiagaraProfile` | `UFlecsNiagaraProfile*` | Attached/death VFX |
| `DestructibleProfile` | `UFlecsDestructibleProfile*` | Fragmentation, constraints |
| `DoorProfile` | `UFlecsDoorProfile*` | Hinge, motor, auto-close |
| `MovementProfile` | `UFlecsMovementProfile*` | Speeds, jump, abilities |
| `AbilityLoadout` | `UFlecsAbilityLoadout*` | Ability slot assignments |
| `ResourcePoolProfile` | `UFlecsResourcePoolProfile*` | Mana, stamina, etc. |
| `ClimbProfile` | `UFlecsClimbProfile*` | Climb speed, exit velocity |
| `RopeSwingProfile` | `UFlecsRopeSwingProfile*` | Swing force, radius |
| `StealthLightProfile` | `UFlecsStealthLightProfile*` | Light type, intensity |
| `NoiseZoneProfile` | `UFlecsNoiseZoneProfile*` | Surface noise zone |
| `VitalsProfile` | `UFlecsVitalsProfile*` | Hunger, thirst, warmth |
| `TemperatureZoneProfile` | `UFlecsTemperatureZoneProfile*` | Ambient temperature zone |

### Tag Flags

| Property | Type | Sets Tag |
|----------|------|----------|
| `bPickupable` | `bool` | `FTagPickupable` |
| `bDestructible` | `bool` | `FTagDestructible` |
| `bHasLoot` | `bool` | `FTagHasLoot` |
| `bIsCharacter` | `bool` | `FTagCharacter` |

---

## Profile Details

### UFlecsPhysicsProfile

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `CollisionRadius` | `float` | 10.0 | Sphere/capsule radius (cm) |
| `CollisionHalfHeight` | `float` | 0.0 | Capsule half-height (0 = sphere) |
| `Layer` | `EFlecsPhysicsLayer` | MOVING | Jolt object layer |
| `Mass` | `float` | 1.0 | Body mass (kg) |
| `Restitution` | `float` | 0.3 | Bounciness |
| `Friction` | `float` | 0.5 | Surface friction |
| `LinearDamping` | `float` | 0.0 | Linear velocity damping |
| `AngularDamping` | `float` | 0.0 | Angular velocity damping |
| `GravityFactor` | `float` | 1.0 | Gravity multiplier (0 = no gravity) |
| `bIsSensor` | `bool` | false | Trigger only, no collision response |
| `bIsKinematic` | `bool` | false | Kinematic body (script-controlled) |
| `bUseCCD` | `bool` | false | Continuous collision detection |

### UFlecsRenderProfile

| Field | Type | Description |
|-------|------|-------------|
| `Mesh` | `UStaticMesh*` | Visual mesh |
| `MaterialOverride` | `UMaterialInterface*` | Material (null = mesh default) |
| `Scale` | `FVector` | Mesh scale |
| `RotationOffset` | `FRotator` | Mesh rotation offset |
| `bCastShadow` | `bool` | Cast shadows |
| `bVisible` | `bool` | Initially visible |
| `CustomDepthStencilValue` | `int32` | Custom depth for outline effects |

### UFlecsHealthProfile

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `MaxHealth` | `float` | 100 | Maximum HP |
| `StartingHealth` | `float` | 0 | Starting HP (0 = MaxHealth) |
| `Armor` | `float` | 0.0 | Damage reduction [0, 1] |
| `RegenPerSecond` | `float` | 0.0 | HP regen rate |
| `RegenDelay` | `float` | 0.0 | Delay before regen after damage |
| `InvulnerabilityTime` | `float` | 0.0 | I-frames after damage |
| `bDestroyOnDeath` | `bool` | true | Auto-destroy on death |

### UFlecsDamageProfile

| Field | Type | Description |
|-------|------|-------------|
| `Damage` | `float` | Base damage per hit |
| `DamageType` | `FGameplayTag` | Damage classification |
| `CriticalMultiplier` | `float` | Crit multiplier |
| `CriticalChance` | `float` | Chance [0, 1] |
| `bAreaDamage` | `bool` | Apply in radius |
| `AreaRadius` | `float` | Area radius (cm) |
| `AreaFalloff` | `float` | Falloff exponent |
| `bDestroyOnHit` | `bool` | Destroy source on contact |
| `bCanHitSameTargetMultipleTimes` | `bool` | Multi-hit |
| `MultiHitCooldown` | `float` | Cooldown between multi-hits |

### UFlecsProjectileProfile

| Field | Type | Description |
|-------|------|-------------|
| `DefaultSpeed` | `float` | Projectile speed (cm/s) |
| `bMaintainSpeed` | `bool` | Keep constant speed |
| `Lifetime` | `float` | Max lifetime (seconds) |
| `MaxBounces` | `int32` | Max wall bounces (0 = destroy on hit) |
| `MinVelocity` | `float` | Kill if speed drops below this |
| `GracePeriod` | `float` | Frames before velocity check activates |
| `bOrientToVelocity` | `bool` | Rotate mesh to face travel direction |
| `bPenetrating` | `bool` | Pass through targets |
| `MaxPenetrations` | `int32` | Max targets to penetrate |

### UFlecsWeaponProfile

Large profile — key fields:

| Group | Fields |
|-------|--------|
| **Firing** | FireMode, FireRate, BurstCount, ProjectilesPerShot, AmmoPerShot, PelletRings (`TArray<FPelletRing>`) |
| **Trigger Pull** | bEnableTriggerPull, TriggerPullTime, bTriggerPullEveryShot |
| **Ammo & Reload** | AcceptedCalibers, AmmoPerShot, bHasChamber, bUnlimitedAmmo, RemoveMagTime, InsertMagTime, ChamberTime, ReloadMoveSpeedMultiplier |
| **Reload Type** | ReloadType (Magazine / SingleRound), OpenTime, InsertRoundTime, CloseTime, InternalMagazineDefinition |
| **Quick-Load Devices** | bAcceptStripperClips, bAcceptSpeedloaders, bDisableQuickLoadDevices, OpenTimeDevice, CloseTimeDevice |
| **Cycling** | bRequiresCycling, CycleTime |
| **Projectile** | ProjectileDefinition, ProjectileSpeedMultiplier, DamageMultiplier |
| **Muzzle** | MuzzleOffset, MuzzleSocketName |
| **Bloom** | BaseSpread, SpreadPerShot, MaxSpread, SpreadDecayRate, SpreadRecoveryDelay, per-state multipliers (FWeaponStateMultipliers) |
| **Recoil** | KickPitch/Yaw Min/Max, KickRecoverySpeed, KickDamping, RecoilPatternCurve, PatternScale |
| **Screen Shake** | ShakeAmplitude, ShakeFrequency, ShakeDecaySpeed |
| **ADS** | ADSFOV, ADSTransitionIn/OutSpeed, ADSSensitivityMultiplier, SightAnchorSocket |
| **Weapon Motion** | InertiaStiffness, InertiaDamping, IdleSway, WalkBob, StrafeTilt, LandingImpact, SprintPose |
| **Collision** | CollisionTraceDistance, RetractDistances, ReadyPoseOffsets |
| **Visuals** | EquippedMesh, AttachSocket, AttachOffset, DroppedMesh, DroppedScale |
| **Animations** | FireMontage, ReloadMontage, EquipMontage, EquipTime |

### UFlecsQuickLoadProfile

Data asset for quick-load devices (stripper clips, speedloaders). Attach to `UFlecsEntityDefinition` to make an item a quick-load device.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `DeviceType` | `EQuickLoadDeviceTypeUI` | StripperClip | Stripper Clip or Speedloader |
| `RoundsHeld` | `int32` | 5 | Rounds loaded per use (1-30) |
| `Caliber` | `FName` | — | Must match CaliberRegistry entry |
| `AmmoTypeDefinition` | `UFlecsAmmoTypeDefinition*` | — | Ammo type pre-loaded in device |
| `InsertTime` | `float` | 0.8 | Batch insert duration (seconds) |
| `bRequiresEmptyMagazine` | `bool` | false | Magazine must be empty (speedloaders) |

### UFlecsContainerProfile

| Field | Type | Description |
|-------|------|-------------|
| `ContainerName` | `FName` | Internal name |
| `DisplayName` | `FText` | UI display name |
| `ContainerType` | `EContainerType` | Grid, Slot, or List |
| `GridWidth` / `GridHeight` | `int32` | Grid dimensions (Grid type) |
| `Slots` | `TArray<FContainerSlotDefinition>` | Named slots (Slot type) |
| `MaxListItems` | `int32` | Max items (List type) |
| `AllowedItemFilter` | `FGameplayTagQuery` | Restrict item types |
| `MaxWeight` | `float` | Weight limit |
| `bAllowNestedContainers` | `bool` | Allow containers inside |
| `bAutoStackOnAdd` | `bool` | Auto-stack matching items |

### UFlecsInteractionProfile

| Field | Type | Description |
|-------|------|-------------|
| `InteractionPrompt` | `FText` | UI prompt text |
| `InteractionRange` | `float` | Max interaction distance (cm) |
| `bSingleUse` | `bool` | Remove interactable after use |
| `InteractionType` | `EInteractionType` | Instant, Focus, or Hold |
| `bRestrictAngle` | `bool` | Angle-based restriction |
| `InteractionAngle` | `float` | Max angle from entity forward |

**Instant-specific:** `InstantAction`, `CustomEventTag`

**Focus-specific:** `bMoveCamera`, `FocusWidgetClass`, `FocusCameraPosition/Rotation`, `FocusFOV`, `TransitionIn/OutTime`

**Hold-specific:** `HoldDuration`, `bCanCancel`, `CompletionAction`, `HoldCompletionEventTag`

### UFlecsMovementProfile

See [Movement System](../systems/movement-system.md) for full field listing.

### Other Profiles

| Profile | Key Fields |
|---------|-----------|
| `UFlecsNiagaraProfile` | AttachedEffect, DeathEffect, Scale, Offset |
| `UFlecsDestructibleProfile` | See [Destructible System](../systems/destructible-system.md) |
| `UFlecsDoorProfile` | See [Door System](../systems/door-system.md) |
| `UFlecsAbilityDefinition` | See [Ability System](../systems/ability-system.md) |
| `UFlecsResourcePoolProfile` | See [Ability System](../systems/ability-system.md) |
| `UFlecsClimbProfile` | ClimbSpeed, TopExitDirection, TopExitVelocity, DetachAngle |
| `UFlecsRopeSwingProfile` | AttachOffset, SwingRadius, MaxSwingForce, AngularDamping |
| `UFlecsStealthLightProfile` | LightType, Intensity, Radius, ConeAngles, Direction |
| `UFlecsNoiseZoneProfile` | Extent (half-extents), SurfaceType |

---

## UFlecsItemDefinition

Standalone data asset for item metadata:

| Field | Type | Description |
|-------|------|-------------|
| `ItemTypeId` | `int32` | Auto-hash from ItemName |
| `ItemName` | `FName` | Internal name (for TypeId hashing) |
| `DisplayName` | `FText` | UI display name |
| `Description` | `FText` | Item description |
| `ItemTags` | `FGameplayTagContainer` | Classification tags |
| `MaxStackSize` | `int32` | Stack limit |
| `GridSize` | `FIntPoint` | Size in inventory grid (W × H) |
| `Weight` | `float` | Weight per unit |
| `BaseValue` | `int32` | Currency value |
| `RarityTier` | `int32` | Rarity level |
| `Icon` | `UTexture2D*` | Inventory icon |
| `IconTint` | `FLinearColor` | Icon color tint |
| `Actions` | `TArray<FItemAction>` | Context menu actions |
| `EquipmentSlot` | `FGameplayTag` | Equipment slot type |
| `MaxDurability` | `float` | Durability (0 = no durability) |

---

## Quick Start: Creating an Entity

1. **Content Browser** → Right-click → Data Asset → `FlecsEntityDefinition`
2. Name it `DA_MyEntity`
3. Add profiles as needed:
   - PhysicsProfile → Set collision radius, mass, layer
   - RenderProfile → Set mesh and material
   - HealthProfile → Set max HP if destructible
4. Place `AFlecsEntitySpawnerActor` in level
5. Set `EntityDefinition = DA_MyEntity`
6. Play!
