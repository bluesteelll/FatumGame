# Folder Structure

> FatumGame uses a vertical, domain-based folder layout. Each gameplay domain (Weapon, Item, Destructible, etc.) has its own top-level folder containing components, systems, and libraries. This page is the complete map.

---

## Source Layout

```
Source/FatumGame/
├── FatumGame.h / .cpp                     Module entry point
├── FatumGame.Build.cs                     Build rules & dependencies
│
├── Core/                                  Simulation core
│   ├── Public/
│   │   ├── FlecsArtillerySubsystem.h      Central hub (UTickableWorldSubsystem)
│   │   ├── FSimulationWorker.h            60 Hz simulation thread
│   │   ├── FLateSyncBridge.h              Lock-free game→sim latest-value bridge
│   │   ├── FSimStateCache.h               Lock-free sim→game scalar cache
│   │   ├── FlecsGameTags.h                All ECS tags + EContainerType enum
│   │   ├── FlecsLibraryHelpers.h          Internal ECS query helpers
│   │   └── Components/
│   │       ├── FlecsHealthComponents.h    FHealthStatic, FDamageStatic, FHealthInstance, FPendingDamage
│   │       ├── FlecsEntityComponents.h    FEntityDefinitionRef, FFocusCameraOverride, FLootStatic
│   │       └── FlecsInteractionComponents.h  FInteractionStatic, FInteractionInstance
│   └── Private/
│       ├── FlecsArtillerySubsystem.cpp              Init, Tick, Deinitialize
│       ├── FlecsArtillerySubsystem_Binding.cpp      Bidirectional entity↔Barrage binding
│       ├── FlecsArtillerySubsystem_Collision.cpp    OnBarrageContact callback
│       ├── FlecsArtillerySubsystem_CollisionSystems.cpp  Collision system setup
│       ├── FlecsArtillerySubsystem_Items.cpp        Prefab registry, item spawning
│       ├── FlecsArtillerySubsystem_Systems.cpp      RegisterComponents + SetupFlecsSystems
│       ├── FSimulationWorker.cpp                    Sim thread Run() loop
│       ├── FLateSyncBridge.cpp
│       ├── FSimStateCache.cpp
│       └── FlecsGameTags.cpp
│
├── Character/                             Player / NPC character
│   ├── Public/
│   │   ├── FlecsCharacter.h               AFlecsCharacter (ACharacter subclass)
│   │   ├── FatumMovementComponent.h       Custom CMC with Barrage physics
│   │   └── FPostureStateMachine.h         Stand/Crouch/Prone state machine
│   └── Private/
│       ├── FlecsCharacter.cpp             Init, Tick, BeginPlay
│       ├── FlecsCharacter_ADS.cpp         Aim-down-sights spring
│       ├── FlecsCharacter_Combat.cpp      Fire input, aim integration
│       ├── FlecsCharacter_Input.cpp       Enhanced Input binding
│       ├── FlecsCharacter_Interaction.cpp Interaction state machine
│       ├── FlecsCharacter_Physics.cpp     ReadAndApplyBarragePosition
│       ├── FlecsCharacter_Recoil.cpp      Visual recoil (kick, shake, pattern)
│       ├── FlecsCharacter_RopeVisual.cpp  Rope swing visual rendering
│       ├── FlecsCharacter_Test.cpp        Test spawn / debug commands
│       ├── FlecsCharacter_UI.cpp          HUD wiring, SimStateCache reads
│       ├── FlecsCharacter_WeaponCollision.cpp  Weapon collision avoidance
│       ├── FlecsCharacter_WeaponMotion.cpp     Weapon sway, bob, landing impact
│       ├── FatumMovementComponent.cpp
│       └── FPostureStateMachine.cpp
│
├── Movement/                              Character locomotion
│   ├── Public/
│   │   ├── FlecsCharacterTypes.h          Atomic wrappers, input/state atomics
│   │   └── Components/
│   │       ├── FlecsMovementComponents.h  ECharacterPosture, FCharacterMoveState
│   │       └── FlecsMovementStatic.h      Movement profile → ECS component
│   └── Private/
│       ├── FlecsArtillerySubsystem_Character.cpp  PrepareCharacterStep (sim thread)
│       └── Components/FlecsMovementStatic.cpp
│
├── Weapon/                                Weapons & projectiles
│   ├── Public/
│   │   ├── Components/
│   │   │   ├── FlecsWeaponComponents.h    FAimDirection, FWeaponStatic/Instance
│   │   │   ├── FlecsProjectileComponents.h  FProjectileStatic/Instance
│   │   │   ├── FlecsRecoilTypes.h         FShotFiredEvent
│   │   │   └── FlecsRecoilState.h         FWeaponRecoilState (game thread)
│   │   └── Library/
│   │       ├── FlecsDamageLibrary.h       BP: ApplyDamage, Heal, Kill, GetHealth
│   │       └── FlecsWeaponLibrary.h       BP: StartFiring, StopFiring, Reload
│   └── Private/
│       ├── Components/
│       ├── Library/
│       └── Systems/
│           ├── FlecsArtillerySubsystem_WeaponSystems.cpp   Fire, Reload, Tick
│           └── FlecsArtillerySubsystem_DamageCollision.cpp DamageCollision, BounceCollision
│
├── Abilities/                             Ability system
│   ├── Public/
│   │   ├── AbilityLifecycleManager.h      Per-character ability slot ticking
│   │   ├── AbilityTickFunctions.h         Per-type tick dispatch table
│   │   ├── AbilityCapsuleHelper.h         Capsule resize for abilities
│   │   └── Components/
│   │       ├── FlecsAbilityTypes.h        EAbilityTypeId, FAbilitySlot, FAbilitySystem
│   │       ├── FlecsAbilityStates.h       Per-ability phase state structs
│   │       └── FlecsResourceTypes.h       FResourcePool, FAbilityCostEntry
│   └── Private/
│       ├── AbilityLifecycleManager.cpp
│       ├── AbilityTickFunctions.cpp
│       ├── AbilityCapsuleHelper.cpp
│       ├── TickTelekinesis.cpp
│       └── Components/
│
├── Climbing/                              Climb & rope swing abilities
│   ├── Public/Components/
│   │   ├── FlecsClimbableComponents.h     FClimbableStatic, FClimbInstance
│   │   └── FlecsSwingableComponents.h     FSwingableStatic, FRopeSwingInstance
│   └── Private/
│       ├── TickClimb.cpp
│       └── TickRopeSwing.cpp
│
├── Stealth/                               Stealth system
│   ├── Public/
│   │   ├── FlecsStealthTypes.h            ESurfaceNoise, EStealthLightType
│   │   ├── FlecsLightSourceActor.h        Gameplay light actor
│   │   ├── FlecsNoiseZoneActor.h          Noise zone actor
│   │   └── Components/FlecsStealthComponents.h
│   └── Private/
│       ├── FlecsLightSourceActor.cpp
│       ├── FlecsNoiseZoneActor.cpp
│       └── Systems/FlecsArtillerySubsystem_StealthSystems.cpp
│
├── Destructible/                          Destructible objects
│   ├── Public/
│   │   ├── FDebrisPool.h                  Pre-allocated body pool
│   │   ├── FlecsDestructibleSpawner.h     Fragment entity spawning
│   │   ├── FlecsConstrainedGroupSpawner.h Constraint-linked group spawning
│   │   ├── Components/FlecsDestructibleComponents.h
│   │   └── Library/FlecsConstraintLibrary.h  BP constraint creation
│   └── Private/
│       ├── FDebrisPool.cpp
│       └── Systems/
│           ├── FlecsArtillerySubsystem_DestructibleCollision.cpp
│           └── FlecsArtillerySubsystem_FragmentationSystems.cpp
│
├── Door/                                  Door system
│   ├── Public/Components/FlecsDoorComponents.h  FDoorStatic, FDoorInstance
│   └── Private/
│       └── Systems/FlecsArtillerySubsystem_DoorSystems.cpp
│
├── Item/                                  Items & containers
│   ├── Public/
│   │   ├── ItemRegistry.h                 TypeId→prefab singleton registry
│   │   ├── Components/FlecsItemComponents.h  Item/Container static+instance
│   │   └── Library/FlecsContainerLibrary.h   BP: AddItem, Remove, Transfer, Pickup
│   └── Private/
│       ├── ItemRegistry.cpp
│       ├── Library/FlecsContainerLibrary.cpp
│       └── Systems/FlecsArtillerySubsystem_PickupCollision.cpp
│
├── Interaction/                           Interaction system
│   ├── Public/
│   │   ├── FlecsInteractionTypes.h        EInteractionType, EInstantAction, EInteractionState
│   │   └── Library/FlecsInteractionLibrary.h
│   └── Private/Library/FlecsInteractionLibrary.cpp
│
├── Spawning/                              Entity spawn API
│   ├── Public/
│   │   ├── FlecsEntitySpawner.h           FEntitySpawnRequest + UFlecsEntityLibrary
│   │   ├── FlecsEntitySpawnerActor.h      Level-placeable spawner actor
│   │   └── Library/FlecsSpawnLibrary.h    BP spawn helpers
│   └── Private/
│
├── Rendering/                             ISM & VFX management
│   ├── Public/
│   │   ├── FlecsRenderManager.h           ISM lifecycle + interpolation
│   │   ├── FlecsNiagaraManager.h          Niagara Array DI integration
│   │   └── FRopeVisualRenderer.h          Rope/chain spline rendering
│   └── Private/
│
├── UI/                                    Game UI (HUD, Inventory, Loot)
│   ├── Public/
│   │   ├── FlecsUISubsystem.h             Model factory, triple-buffer state
│   │   ├── FlecsMessageSubsystem.h        Typed pub/sub message bus
│   │   ├── FlecsUIMessages.h              All UI message structs
│   │   ├── FlecsHUDWidget.h               HUD widget (message subscriber)
│   │   ├── FlecsInventoryWidget.h         Inventory panel
│   │   ├── FlecsInventoryItemWidget.h     Drag-drop item tile
│   │   ├── FlecsInventorySlotWidget.h     Grid cell widget
│   │   ├── FlecsContainerGridWidget.h     Full grid renderer
│   │   ├── FlecsInventoryDragPayload.h    Drag operation payload
│   │   └── FlecsLootPanel.h              Dual-container loot view
│   └── Private/
│
├── Input/                                 Input system
│   ├── Public/
│   │   ├── FatumInputConfig.h             InputAction→GameplayTag mapping asset
│   │   ├── FatumInputComponent.h          Enhanced Input component
│   │   └── FatumInputTags.h              Gameplay tag constants
│   └── Private/
│
├── Vitals/                                Vitals / survival system
│   ├── Public/
│   │   ├── Components/FlecsVitalsComponents.h  FVitalsStatic, FVitalsInstance, FVitalsItemStatic
│   │   └── Library/FlecsVitalsLibrary.h        BP: vitals queries and mutations
│   └── Private/
│       ├── Components/FlecsVitalsComponents.cpp
│       ├── Library/FlecsVitalsLibrary.cpp
│       └── Systems/FlecsArtillerySubsystem_VitalsSystems.cpp
│
├── Definitions/                           ALL Data Assets & Profiles
│   └── Public/
│       ├── FlecsEntityDefinition.h        Master data asset
│       ├── FlecsItemDefinition.h          Item metadata asset
│       ├── FlecsHealthProfile.h
│       ├── FlecsDamageProfile.h
│       ├── FlecsPhysicsProfile.h
│       ├── FlecsRenderProfile.h
│       ├── FlecsProjectileProfile.h
│       ├── FlecsWeaponProfile.h
│       ├── FlecsContainerProfile.h
│       ├── FlecsInteractionProfile.h
│       ├── FlecsNiagaraProfile.h
│       ├── FlecsDestructibleProfile.h
│       ├── FlecsDestructibleGeometry.h
│       ├── FlecsMovementProfile.h
│       ├── FlecsAbilityDefinition.h
│       ├── FlecsAbilityLoadout.h
│       ├── FlecsResourcePoolProfile.h
│       ├── FlecsDoorProfile.h
│       ├── FlecsClimbProfile.h
│       ├── FlecsRopeSwingProfile.h
│       ├── FlecsStealthLightProfile.h
│       ├── FlecsNoiseZoneProfile.h
│       ├── FlecsContainerDefinition.h
│       ├── FlecsProjectileDefinition.h
│       ├── FlecsConstrainedGroupDefinition.h
│       ├── FlecsCaliberRegistry.h         Caliber name registry singleton
│       ├── FlecsAmmoTypeDefinition.h      Ammo type data asset
│       ├── FlecsMagazineProfile.h         Internal magazine profile
│       ├── FlecsQuickLoadProfile.h        Quick-load device profile (speedloaders, stripper clips)
│       ├── FlecsVitalsProfile.h           Character vitals (hunger, thirst, etc.)
│       ├── FlecsVitalsItemProfile.h       Item vitals effects
│       └── FlecsTemperatureZoneProfile.h  Temperature zone data
│
└── Utils/                                 Shared utilities
    ├── Public/
    │   ├── FTimeDilationStack.h           Time dilation priority stack
    │   ├── ConeImpulse.h                 Radial cone impulse helper
    │   ├── LedgeDetector.h               Barrage-based ledge detection
    │   └── BarrageSpawnUtils.h           Body creation convenience wrappers
    └── Private/
```

---

## Plugin Layout

```
Plugins/
├── Barrage/                  Jolt Physics integration
│   ├── Barrage/              Runtime module (UBarrageDispatch, FBarragePrimitive, layers)
│   └── BarrageEditor/        Visual debugger
│
├── SkeletonKey/              Entity identity system
│                             FSkeletonKey, ISkeletonLord, LibCuckoo hash map
│
├── FlecsIntegration/         Flecs ECS for UE
│   ├── UnrealFlecs/          Core Flecs wrapper
│   └── FlecsLibrary/         UE-specific helpers
│
├── FlecsBarrage/             ECS ↔ Physics bridge
│                             FBarrageBody, FISMRender, FCollisionPair, collision tags
│
├── FlecsUI/                  UI framework
│                             UFlecsUIPanel, UFlecsUIWidget, UFlecsContainerModel,
│                             UFlecsValueModel, UFlecsActionRouter, TTripleBuffer
│
├── LocomoCore/               Math utilities (SIMD sort, LSH grouping)
│
├── UE4CMake/                 CMake integration for Jolt build
│
└── NiagaraUIRenderer/        Third-party: Niagara effects in UMG
```

---

## Content Layout

```
Content/
├── FlecsDA/                  Data Assets (entity definitions)
│   ├── DA_PlayerCharacter.uasset
│   ├── DA_Bullet.uasset
│   ├── DA_Bread.uasset
│   └── ...
│
├── Widgets/                  UMG widget blueprints
│   └── WBP_MainHUD.uasset
│
├── FlecsTestMap2.umap        Primary test level
│
├── Input/                    Input action / mapping assets
└── Materials/                Shared materials
```

---

## Naming Conventions

| Type | Pattern | Example |
|------|---------|---------|
| Data Asset | `DA_<Name>` | `DA_Bullet`, `DA_PlayerCharacter` |
| Widget Blueprint | `WBP_<Name>` | `WBP_MainHUD` |
| Static Component | `F<Name>Static` | `FHealthStatic`, `FWeaponStatic` |
| Instance Component | `F<Name>Instance` | `FHealthInstance`, `FWeaponInstance` |
| Tag | `FTag<Name>` | `FTagDead`, `FTagProjectile` |
| Profile | `UFlecs<Name>Profile` | `UFlecsHealthProfile` |
| Blueprint Library | `UFlecs<Name>Library` | `UFlecsContainerLibrary` |
| Subsystem | `UFlecs<Name>Subsystem` | `UFlecsArtillerySubsystem` |
| System setup method | `Setup<Domain>Systems()` | `SetupWeaponSystems()` |
| Sim thread method | `FlecsArtillerySubsystem_<Domain>.cpp` | `_WeaponSystems.cpp` |
| Character partial | `FlecsCharacter_<Aspect>.cpp` | `_Combat.cpp`, `_Interaction.cpp` |
