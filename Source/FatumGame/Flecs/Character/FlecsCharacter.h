// Flecs-integrated Character - full ECS integration for health, damage, collision.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SkeletonTypes.h"
#include "FlecsCharacter.generated.h"

class UFlecsEntityDefinition;
class UFlecsHUDWidget;
class UFlecsInventoryWidget;
class UFlecsLootPanel;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UCameraComponent;
struct FInputActionValue;

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

	// ═══════════════════════════════════════════════════════════════
	// ENHANCED INPUT
	// ═══════════════════════════════════════════════════════════════

	/** Gameplay mapping context — active during normal gameplay */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> GameplayMappingContext;

	/** UI mapping context — active when inventory/menu is open (only toggle key) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> InventoryMappingContext;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	/** Fire Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	/** Spawn Item Input Action (E) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SpawnItemAction;

	/** Destroy Item Input Action (F) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> DestroyItemAction;

	/** Toggle Inventory Input Action (I) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InventoryAction;

	// ═══════════════════════════════════════════════════════════════
	// HEALTH
	// ═══════════════════════════════════════════════════════════════

	/** Maximum health */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Health")
	float MaxHealth = 100.f;

	/** Starting health (0 = use MaxHealth) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Health")
	float StartingHealth = 0.f;

	/** Armor reduces incoming damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Health")
	float Armor = 0.f;

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
	FSkeletonKey GetInteractionTarget() const { return CurrentInteractionTarget; }

	/** Is there a valid interaction target? */
	UFUNCTION(BlueprintPure, Category = "Flecs|Interaction")
	bool HasInteractionTarget() const { return CurrentInteractionTarget.IsValid(); }

	/** Get prompt text for current interaction target */
	UFUNCTION(BlueprintPure, Category = "Flecs|Interaction")
	FText GetInteractionPrompt() const;

	/** Called when interaction target changes (for UI updates) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnInteractionTargetChanged(bool bHasTarget, FSkeletonKey TargetKey);

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

	/** Called when DestroyItem (F) is pressed */
	void OnDestroyItem(const FInputActionValue& Value);

	/** Toggle inventory open/close */
	void ToggleInventory(const FInputActionValue& Value);

private:
	/** SkeletonKey generated from actor pointer hash (replaces UPlayerKeyCarry) */
	FSkeletonKey CharacterKey;

	/** Keys of spawned test entities (for cleanup) */
	TArray<FSkeletonKey> SpawnedEntityKeys;
	/** Cached health for change detection */
	float CachedHealth = 0.f;

	/** Fire was requested before weapon finished spawning — apply after spawn completes. */
	bool bPendingFireAfterSpawn = false;

	/** Check for health changes from Flecs and fire events */
	void CheckHealthChanges();

	// ─────────────────────────────────────────────────────────
	// INTERACTION (private)
	// ─────────────────────────────────────────────────────────

	/** Current interaction target (SkeletonKey of a Barrage body) */
	FSkeletonKey CurrentInteractionTarget;

	/** Cached prompt text for current target (avoids cross-thread reads) */
	FText CachedInteractionPrompt;

	/** Timer handle for 10 Hz interaction raycast */
	FTimerHandle InteractionTraceTimerHandle;

	/** Perform periodic raycast to detect interactable entities */
	void PerformInteractionTrace();

	/** Execute interaction with CurrentInteractionTarget */
	void TryInteract();
};
