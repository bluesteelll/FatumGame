// Flecs-integrated Character - full ECS integration for health, damage, collision.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SkeletonTypes.h"
#include "FlecsInteractionTypes.h"
#include "FlecsCharacterTypes.h"
#include "FTimeDilationStack.h"
#include "FlecsRecoilState.h"
#include "FlecsCharacter.generated.h"

class UFlecsArtillerySubsystem;
class UFlecsEntityDefinition;
class UFlecsHUDWidget;
class UFlecsInventoryWidget;
class UFlecsLootPanel;
class UFlecsInteractionProfile;
class UFlecsUIPanel;
class UFatumMovementComponent;
class UFatumInputConfig;
enum class ECharacterPosture : uint8;
class UInputMappingContext;
class USpringArmComponent;
class UCameraComponent;
struct FInputActionValue;
class FBarragePrimitive;
struct FRopeVisualAtomics;
class FRopeVisualRenderer;
class UCanvas;

/**
 * Character fully integrated with Flecs ECS.
 *
 * Features:
 * - Barrage physics movement (Jolt engine)
 * - Flecs entity with FHealthStatic + FHealthInstance, FTagCharacter
 * - Automatic damage handling from projectiles
 * - Death callbacks
 *
 * Usage:
 * 1. Create Blueprint child
 * 2. Set ProjectileDefinition
 * 3. Use ABarragePlayerController
 */
UCLASS(Blueprintable)
class FATUMGAME_API AFlecsCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AFlecsCharacter(const FObjectInitializer& ObjectInitializer);
	virtual ~AFlecsCharacter() override;

	// ═══════════════════════════════════════════════════════════════
	// COMPONENTS
	// ═══════════════════════════════════════════════════════════════

	/** Camera boom (only used in third-person mode) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** If true, camera is first-person (attached to mesh). If false, third-person with boom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bFirstPersonCamera = true;

	/** Base field of view (degrees). Sprint/ability FOV offsets are added on top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "60", ClampMax = "130"))
	float BaseFOV = 90.f;

	// ═══════════════════════════════════════════════════════════════
	// ENHANCED INPUT
	// ═══════════════════════════════════════════════════════════════

	/** Gameplay mapping context — active during normal gameplay */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> GameplayMappingContext;

	/** UI mapping context — active when inventory/menu is open (only toggle key) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> InventoryMappingContext;

	/** Input config — maps InputAction assets to GameplayTags */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UFatumInputConfig> InputConfig;

	// ═══════════════════════════════════════════════════════════════
	// CHARACTER DEFINITION (REQUIRED)
	// ═══════════════════════════════════════════════════════════════

	/** Entity definition — single source of all ECS data (health, abilities, resources, movement).
	 *  MUST be set. Create in Content Browser: Data Asset -> FlecsEntityDefinition.
	 *  At minimum: set HealthProfile + AbilityLoadout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Definition")
	TObjectPtr<UFlecsEntityDefinition> CharacterDefinition;

	// ═══════════════════════════════════════════════════════════════
	// HEALTH (read from CharacterDefinition->HealthProfile)
	// ═══════════════════════════════════════════════════════════════

	/** Get current health from Flecs */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health")
	float GetCurrentHealth() const;

	/** Get health as 0-1 percentage */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health")
	float GetHealthPercent() const;

	/** Is the character alive? */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health")
	bool IsAlive() const;

	/** Apply damage (goes through Flecs) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Health")
	void ApplyDamage(float Damage);

	/** Heal (goes through Flecs) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Health")
	void Heal(float Amount);

	// ═══════════════════════════════════════════════════════════════
	// PROJECTILE
	// ═══════════════════════════════════════════════════════════════

	/** Projectile definition (UFlecsEntityDefinition with ProjectileProfile + RenderProfile + optional DamageProfile) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Projectile")
	TObjectPtr<UFlecsEntityDefinition> ProjectileDefinition;

	/** Barrel offset from camera in local aim space (X=forward, Y=right, Z=up).
	 *  Projectile spawns at camera + this offset, aimed at the crosshair target via raycast.
	 *  Set to approximate real weapon barrel position relative to camera eye.
	 *  Example: (50, 15, -10) = 50cm forward, 15cm right, 10cm below eye. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Projectile")
	FVector MuzzleOffset = FVector(50.f, 0.f, 0.f);

	/** Speed override (0 = use definition) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Projectile")
	float ProjectileSpeedOverride = 0.f;

	/** Fire single projectile forward */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FSkeletonKey FireProjectile();

	/** Fire projectile in direction */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FSkeletonKey FireProjectileInDirection(FVector Direction);

	/** Fire spread (shotgun) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	TArray<FSkeletonKey> FireProjectileSpread(int32 Count = 8, float SpreadAngle = 15.f);

	// ═══════════════════════════════════════════════════════════════
	// MOVEMENT
	// ═══════════════════════════════════════════════════════════════

	/** Cached typed pointer to our custom CMC (set MovementProfile on this component) */
	UPROPERTY(BlueprintReadOnly, Category = "Flecs|Movement")
	TObjectPtr<UFatumMovementComponent> FatumMovement;

	// ═══════════════════════════════════════════════════════════════
	// ENTITY SPAWNING (TEST)
	// ═══════════════════════════════════════════════════════════════

	/** Entity definition for test spawning (E key) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Entity")
	TObjectPtr<UFlecsEntityDefinition> TestEntityDefinition;

	/** Distance in front of character to spawn entity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Entity")
	float SpawnDistance = 200.f;

	/** Spawn entity from TestEntityDefinition in front of character */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity")
	FSkeletonKey SpawnTestEntity();

	/** Destroy the last spawned test entity */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity")
	void DestroyLastSpawnedEntity();

	/** Get all spawned test entities */
	UFUNCTION(BlueprintPure, Category = "Flecs|Entity")
	TArray<FSkeletonKey> GetSpawnedEntities() const { return SpawnedEntityKeys; }

	// ═══════════════════════════════════════════════════════════════
	// WEAPON VISUAL (game thread only — cosmetic representation)
	// ECS weapon logic is in FWeaponInstance/FWeaponStatic on sim thread.
	// This component is the visual counterpart, driven by game thread.
	// ═══════════════════════════════════════════════════════════════

	/** Weapon skeletal mesh, attached to camera for first person view */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Visual")
	TObjectPtr<USkeletalMeshComponent> WeaponMeshComponent;

	/** Attach weapon visual. Game thread only. */
	void AttachWeaponVisual(USkeletalMesh* InMesh, const FTransform& AttachOffset);

	/** Detach weapon visual. Game thread only. */
	void DetachWeaponVisual();

	// ═══════════════════════════════════════════════════════════════
	// WEAPON TESTING
	// ═══════════════════════════════════════════════════════════════

	/** Weapon definition for testing (must have WeaponProfile with ProjectileDefinition) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Weapon")
	TObjectPtr<UFlecsEntityDefinition> TestWeaponDefinition;

	/** Flecs entity ID of spawned test weapon (0 = not spawned) */
	UPROPERTY(BlueprintReadOnly, Category = "Flecs|Weapon")
	int64 TestWeaponEntityId = 0;

	/** Spawn test weapon and equip it to character */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon")
	void SpawnAndEquipTestWeapon();

	/** Start firing test weapon (hold) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon")
	void StartFiringWeapon();

	/** Stop firing test weapon (release) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon")
	void StopFiringWeapon();

	/** Reload test weapon */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon")
	void ReloadTestWeapon();

	// ═══════════════════════════════════════════════════════════════
	// INVENTORY
	// Pure ECS containers — no physics, no SkeletonKey.
	// Defined via Data Assets, spawned on BeginPlay.
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Inventory")
	TObjectPtr<UFlecsEntityDefinition> InventoryDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Inventory")
	TObjectPtr<UFlecsEntityDefinition> WeaponInventoryDefinition;

	UPROPERTY(BlueprintReadOnly, Category = "Flecs|Inventory")
	int64 InventoryEntityId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Flecs|Inventory")
	int64 WeaponInventoryEntityId = 0;

	UFUNCTION(BlueprintPure, Category = "Flecs|Inventory")
	int64 GetInventoryEntityId() const { return InventoryEntityId; }

	UFUNCTION(BlueprintPure, Category = "Flecs|Inventory")
	int64 GetWeaponInventoryEntityId() const { return WeaponInventoryEntityId; }

	// ═══════════════════════════════════════════════════════════════
	// CONTAINER TESTING
	// ═══════════════════════════════════════════════════════════════

	/** Container definition for testing (spawns on first E press if set) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Container")
	TObjectPtr<UFlecsEntityDefinition> TestContainerDefinition;

	/** Item definition to add to container on E press (must have ItemDefinition profile) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Container")
	TObjectPtr<UFlecsEntityDefinition> TestItemDefinition;

	/** Current test container key (spawned automatically) */
	UPROPERTY(BlueprintReadOnly, Category = "Flecs|Container")
	FSkeletonKey TestContainerKey;

	/** Spawn test container in front of character (called automatically if TestContainerDefinition set) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Container")
	FSkeletonKey SpawnTestContainer();

	/** Add test item to the container. Returns true if added. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Container")
	bool AddItemToTestContainer();

	/** Remove all items from test container. Shows count on screen. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Container")
	void RemoveAllItemsFromTestContainer();

	// ═══════════════════════════════════════════════════════════════
	// INTERACTION
	// ═══════════════════════════════════════════════════════════════

	/** Maximum distance for interaction raycast (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Interaction")
	float MaxInteractionDistance = 500.f;

	/** Use sphere trace instead of ray trace for interaction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Interaction")
	bool bUseSphereTrace = true;

	/** Sphere radius for interaction trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Interaction",
		meta = (EditCondition = "bUseSphereTrace", ClampMin = "5", ClampMax = "50"))
	float InteractionSphereRadius = 15.f;

	/** Get the BarrageKey of the current interaction target */
	UFUNCTION(BlueprintPure, Category = "Flecs|Interaction")
	FSkeletonKey GetInteractionTarget() const { return Interact.CurrentTarget; }

	/** Is there a valid interaction target? */
	UFUNCTION(BlueprintPure, Category = "Flecs|Interaction")
	bool HasInteractionTarget() const { return Interact.CurrentTarget.IsValid(); }

	/** Get prompt text for current interaction target */
	UFUNCTION(BlueprintPure, Category = "Flecs|Interaction")
	FText GetInteractionPrompt() const;

	/** Is the character currently in an interaction state (not Gameplay)? */
	UFUNCTION(BlueprintPure, Category = "Flecs|Interaction")
	bool IsInInteraction() const;

	/** Called when interaction target changes (for UI updates) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnInteractionTargetChanged(bool bHasTarget, FSkeletonKey TargetKey);

	/** Called when interaction state changes */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnInteractionStateChanged(uint8 NewState);

	/** Called when hold progress updates (0.0 to 1.0) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnHoldProgressChanged(float Progress);

	// ═══════════════════════════════════════════════════════════════
	// HUD
	// ═══════════════════════════════════════════════════════════════

	/** Widget class to create for HUD. Set to WBP_MainHUD (child of UFlecsHUDWidget). */
	UPROPERTY(EditAnywhere, Category = "HUD")
	TSubclassOf<UFlecsHUDWidget> HUDWidgetClass;

	/** Active HUD widget instance. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UFlecsHUDWidget> HUDWidget;

	// ═══════════════════════════════════════════════════════════════
	// INVENTORY UI
	// ═══════════════════════════════════════════════════════════════

	/** Widget class for inventory UI. Set to WBP_Inventory in Blueprint. */
	UPROPERTY(EditAnywhere, Category = "Inventory UI")
	TSubclassOf<UFlecsInventoryWidget> InventoryWidgetClass;

	/** Active inventory widget instance. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory UI")
	TObjectPtr<UFlecsInventoryWidget> InventoryWidget;

	/** Is inventory currently open? */
	UFUNCTION(BlueprintPure, Category = "Inventory UI")
	bool IsInventoryOpen() const;

	// ═══════════════════════════════════════════════════════════════
	// LOOT PANEL UI
	// ═══════════════════════════════════════════════════════════════

	/** Widget class for side-by-side loot panel. */
	UPROPERTY(EditAnywhere, Category = "Inventory UI")
	TSubclassOf<UFlecsLootPanel> LootPanelClass;

	/** Active loot panel instance. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory UI")
	TObjectPtr<UFlecsLootPanel> LootPanel;

	UFUNCTION(BlueprintPure, Category = "Inventory UI")
	bool IsLootOpen() const;

	void OpenLootPanel(int64 ExternalContainerEntityId, const FText& ExternalTitle);
	void CloseLootPanel();

	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** Get SkeletonKey for this character */
	UFUNCTION(BlueprintPure, Category = "Flecs")
	FSkeletonKey GetEntityKey() const;

	/** Get Flecs entity ID for this character */
	UFUNCTION(BlueprintPure, Category = "Flecs")
	int64 GetCharacterEntityId() const;

	/** Set the feet-to-actor Z offset (used by mantle to force crouch offset). */
	void SetFeetToActorOffset(float Value) { PosState.FeetToActorOffset = Value; }
	float GetFeetToActorOffset() const { return PosState.FeetToActorOffset; }

	/** Get cached Barrage body (read-only). */
	TSharedPtr<FBarragePrimitive> GetCachedBarrageBody() const { return CachedBarrageBody; }

	// ═══════════════════════════════════════════════════════════════
	// EVENTS
	// ═══════════════════════════════════════════════════════════════

	/** Called when character takes damage */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnDamageTaken(float Damage, float NewHealth);

	/** Called when character dies */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnDeath();

	/** Called when character is healed */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnHealed(float Amount, float NewHealth);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual UInputComponent* CreatePlayerInputComponent() override;

	/** Get muzzle world location */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FVector GetMuzzleLocation() const;

	/** Get firing direction (camera or forward) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FVector GetFiringDirection() const;

	/** Native death handler - calls OnDeath BP event */
	virtual void HandleDeath();

	// ═══════════════════════════════════════════════════════════════
	// ENHANCED INPUT HANDLERS
	// ═══════════════════════════════════════════════════════════════

	/** Called for movement input (WASD) */
	void Move(const FInputActionValue& Value);

	/** Called for looking input (Mouse) */
	void Look(const FInputActionValue& Value);

	/** Called when Fire is pressed */
	void StartFire(const FInputActionValue& Value);

	/** Called when Fire is released */
	void StopFire(const FInputActionValue& Value);

	/** Called when SpawnItem (E) is pressed */
	void OnSpawnItem(const FInputActionValue& Value);

	/** Called when Interact (E) is released — for Hold cancellation */
	void OnInteractReleased(const FInputActionValue& Value);

	/** Called when Cancel (Escape) is pressed — for Focus exit */
	void OnInteractCancel(const FInputActionValue& Value);

	/** Called when DestroyItem (F) is pressed */
	void OnDestroyItem(const FInputActionValue& Value);

	/** Sprint started (Shift press) */
	void OnSprintStarted(const FInputActionValue& Value);

	/** Sprint ended (Shift release) */
	void OnSprintCompleted(const FInputActionValue& Value);

	/** Jump started (Space press) — routes through FatumMovement for coyote/buffer */
	void OnJumpStarted(const FInputActionValue& Value);

	/** Jump ended (Space release) */
	void OnJumpCompleted(const FInputActionValue& Value);

	/** Toggle inventory open/close */
	void ToggleInventory(const FInputActionValue& Value);

	/** Crouch started (C press) */
	void OnCrouchStarted(const FInputActionValue& Value);

	/** Crouch ended (C release) */
	void OnCrouchCompleted(const FInputActionValue& Value);

	/** Prone toggle (Z press) */
	void OnProneStarted(const FInputActionValue& Value);

	/** Prone release (Z release, for Hold mode) */
	void OnProneCompleted(const FInputActionValue& Value);

	/** Ability 1 pressed (blink start) */
	void OnAbility1Started(const FInputActionValue& Value);

	/** Ability 1 released (blink release) */
	void OnAbility1Completed(const FInputActionValue& Value);

	/** Ability 2 pressed (kinetic blast) */
	void OnAbility2Started(const FInputActionValue& Value);

	/** Ability 3 pressed (telekinesis toggle) */
	void OnTelekinesisToggle(const FInputActionValue& Value);

	/** Telekinesis throw */
	void OnTelekinesisThrow(const FInputActionValue& Value);

	/** Telekinesis scroll (hold distance) */
	void OnTelekinesisScroll(const FInputActionValue& Value);

	/** Called by UFatumMovementComponent::OnPostureChanged delegate */
	void HandlePostureChanged(ECharacterPosture NewPosture);

private:
	/** SkeletonKey generated from actor pointer hash (replaces UPlayerKeyCarry) */
	FSkeletonKey CharacterKey;

	/** Keys of spawned test entities (for cleanup) */
	TArray<FSkeletonKey> SpawnedEntityKeys;
	/** Cached health for change detection */
	float CachedHealth = 0.f;

	/** Fire was requested before weapon finished spawning — apply after spawn completes. */
	bool bPendingFireAfterSpawn = false;

	// Movement ECS sync — only write to Flecs when state actually changes
	uint8 LastSyncedPosture = 0;
	uint8 LastSyncedMoveMode = 0;
	void SyncMovementStateToECS();

	// ─────────────────────────────────────────────────────────
	// INIT / CLEANUP (called from BeginPlay/EndPlay, implemented in respective .cpp files)
	// ─────────────────────────────────────────────────────────
	void InitCamera();
	void InitECSRegistration();
	void InitInventoryContainers();  // in FlecsCharacter_UI.cpp
	void InitInteractionTrace();     // in FlecsCharacter_Interaction.cpp
	void InitUI();                   // in FlecsCharacter_UI.cpp
	void CleanupUI();                // in FlecsCharacter_UI.cpp
	void CleanupInteraction();       // in FlecsCharacter_Interaction.cpp
	void UnregisterFromECS();

	// ─────────────────────────────────────────────────────────
	// BARRAGE CHARACTER BRIDGE
	// InputAtomics: game→sim (written by Move(), read by PrepareCharacterStep)
	// Position readback: direct Jolt read in Tick() (before CameraManager)
	// ─────────────────────────────────────────────────────────
	TSharedPtr<FCharacterInputAtomics, ESPMode::ThreadSafe> InputAtomics;  // game→sim
	TSharedPtr<FCharacterStateAtomics, ESPMode::ThreadSafe> StateAtomics; // sim→game
	bool bPrevBlinkAiming = false;

	// ── Time dilation (game thread → sim thread atomic) ──
	FTimeDilationStack DilationStack;
	double LastRealTickTime = 0.0; // wall-clock time for undilated DT in dilation stack

	/** Cached Barrage body — set once in BeginPlay EnqueueCommand (sim thread).
	 *  Read by Tick() (game thread) for direct Jolt position reads. */
	TSharedPtr<FBarragePrimitive> CachedBarrageBody;

	// ─────────────────────────────────────────────────────────
	// POSITION INTERPOLATION (game thread only, updated in Tick)
	// ─────────────────────────────────────────────────────────
	FCharacterPositionState PosState;

	/** Read Jolt position, interpolate, call ApplyBarrageSync. Called from Tick(). */
	void ReadAndApplyBarragePosition(float DeltaTime);

	/** Apply interpolated position: CMC feed + FeetToActorOffset + SetActorLocation. */
	void ApplyBarrageSync(const FVector& FeetPos, uint8 GroundState, const FVector& Velocity);

	friend class UFlecsArtillerySubsystem;

	// ─────────────────────────────────────────────────────────
	// ROPE VISUAL (sim→game via FRopeVisualAtomics, rendered in Tick)
	// ─────────────────────────────────────────────────────────
	TSharedPtr<FRopeVisualAtomics> RopeVisualAtomics;
	FRopeVisualRenderer* RopeRenderer = nullptr;

	// ─────────────────────────────────────────────────────────
	// PENDING WEAPON EQUIP (sim→game via atomics, processed in Tick)
	// ─────────────────────────────────────────────────────────
	FPendingWeaponEquip PendingWeaponEquip;

	/** Check for health changes from Flecs and fire events */
	void CheckHealthChanges();

	/** Poll SimStateCache for resource changes and fire OnResourcesUpdated on HUD */
	void UpdateResourceUI();

	/** Cached resource ratios for change detection (game thread) */
	float CachedResourceRatios[4] = {};
	uint8 CachedResourcePoolCount = 0;
	float ResourcePoolMaxValues[4] = {};  // from ResourcePoolProfile (static)
	uint8 ResourcePoolTypes[4] = {};      // EResourceType values (static)

	// ─────────────────────────────────────────────────────────
	// INTERACTION (detection + state machine)
	// Grouped into FCharacterInteractionState for header clarity.
	// Implementation in FlecsCharacter_Interaction.cpp
	// ─────────────────────────────────────────────────────────

	/** All interaction state: detection, state machine, focus camera, hold progress. */
	struct FCharacterInteractionState
	{
		// State machine
		EInteractionState State = EInteractionState::Gameplay;
		const UFlecsInteractionProfile* ActiveProfile = nullptr;
		FSkeletonKey ActiveTargetKey;

		// Detection (10Hz trace results)
		FSkeletonKey CurrentTarget;
		FText CachedPrompt;
		EInteractionType CachedType = EInteractionType::Instant;
		float CachedHoldDuration = 0.f;

		// Focus camera transition
		FTransform SavedCameraTransform = FTransform::Identity;
		float SavedCameraFOV = 90.f;
		FTransform FocusCameraTarget = FTransform::Identity;
		float FocusTargetFOV = 0.f;
		float FocusLerpAlpha = 0.f;
		float CurrentTransitionDuration = 0.4f;

		// Hold state
		float HoldAccumulator = 0.f;
		float HoldRequiredDuration = 1.f;
		float HoldTargetLostTime = 0.f;
		bool bHoldCanCancel = true;
		bool bInteractKeyHeld = false;
	};
	FCharacterInteractionState Interact;

	/** Focus panel widget instance (UPROPERTY for GC, can't live in plain struct) */
	UPROPERTY()
	TObjectPtr<UFlecsUIPanel> ActiveFocusPanel;

	/** Timer handle for 10 Hz interaction raycast */
	FTimerHandle InteractionTraceTimerHandle;

	/** Perform periodic raycast to detect interactable entities */
	void PerformInteractionTrace();

	// State machine methods (implemented in FlecsCharacter_Interaction.cpp)
	void HandleInteractionInput();
	void HandleInteractionRelease();
	void HandleInteractionCancel();
	void TickInteractionStateMachine(float DeltaTime);
	void BeginFocusTransition();
	void BeginUnfocusTransition(float OverrideDuration = 0.f);
	void RestoreCameraControl();
	FTransform ComputeFocusCameraTransform(FVector EntityPos, FQuat EntityRot, FVector LocalCameraPos, FRotator LocalCameraRot) const;
	bool GetEntityWorldTransform(FSkeletonKey EntityKey, FVector& OutPosition, FQuat& OutRotation) const;
	void ApplyFocusCameraLerp(float Alpha);
	void BeginHoldInteraction();
	void CancelHoldInteraction();
	void CompleteHoldInteraction();
	void ForceCancelInteraction();
	void SetInteractionState(EInteractionState NewState);
	const UFlecsInteractionProfile* ResolveInteractionProfile(FSkeletonKey TargetKey) const;
	bool GetEntityWorldPosition(FSkeletonKey EntityKey, FVector& OutPosition) const;
	void OpenFocusPanel();
	void CloseFocusPanel();

	// ─────────────────────────────────────────────────────────
	// WEAPON RECOIL (game thread only, FlecsCharacter_Recoil.cpp)
	// ─────────────────────────────────────────────────────────
	FWeaponRecoilState RecoilState;

	/** Base weapon transform cached from AttachWeaponVisual — reset target for inertia rotation. */
	FTransform BaseWeaponTransform = FTransform::Identity;

	/** Drain MPSC shot events from sim thread, apply pattern + kick + shake impulses. */
	void DrainShotEventsAndApplyRecoil();

	/** Spring-damper recovery for kick offset. */
	void TickKickRecovery(float DeltaTime);

	/** Compute visual shake offset (does NOT affect control rotation). */
	void TickScreenShake(float DeltaTime);

	/** Weapon inertia: spring-damper lag behind crosshair + idle sway.
	 *  @param AimDelta Mouse-only control rotation delta this frame (excludes recoil). */
	void TickWeaponInertia(float DeltaTime, const FVector2D& AimDelta);

#if !UE_BUILD_SHIPPING
	/** Debug canvas callback: draws blue dot at weapon's actual aim point (2D screen-space). */
	void DrawInertiaDebug(UCanvas* Canvas, APlayerController* PC);
	FDelegateHandle InertiaDebugDrawHandle;
#endif

	// ─────────────────────────────────────────────────────────
	// TICK HELPERS (implemented in FlecsCharacter.cpp)
	// Extracted from Tick() for readability. Execution order matters.
	// ─────────────────────────────────────────────────────────
	void WriteCameraAtomics();
	void ConsumeTeleportSnap();
	void TickTimeDilation(float DeltaTime);
	void TickPostureAndResnap(float DeltaTime);
	void UpdateCamera();
	void ProcessPendingWeaponEquip();
	void WriteAimDirection();
};
