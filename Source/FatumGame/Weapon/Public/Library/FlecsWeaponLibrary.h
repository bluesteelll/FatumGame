// Blueprint function library for Flecs ECS weapon control operations.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FlecsWeaponLibrary.generated.h"

UCLASS()
class UFlecsWeaponLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// WEAPON CONTROL (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void StartFiring(UObject* WorldContextObject, int64 WeaponEntityId);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void StopFiring(UObject* WorldContextObject, int64 WeaponEntityId);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void ReloadWeapon(UObject* WorldContextObject, int64 WeaponEntityId);

	/** Toggle reload: if idle, start reload. If reloading, cancel reload. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void ToggleReload(UObject* WorldContextObject, int64 WeaponEntityId);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void SetAimDirection(UObject* WorldContextObject, int64 CharacterEntityId, FVector Direction, FVector CharacterPosition = FVector::ZeroVector);

	/**
	 * Get current ammo in weapon magazine.
	 * WARNING: Cross-thread read - value may be stale by 1-2 frames.
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static int32 GetWeaponAmmo(UObject* WorldContextObject, int64 WeaponEntityId);

	/**
	 * Get weapon ammo info (current, magazine size, reserve).
	 * WARNING: Cross-thread read - values may be stale by 1-2 frames.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static bool GetWeaponAmmoInfo(UObject* WorldContextObject, int64 WeaponEntityId,
		int32& OutCurrentAmmo, int32& OutMagazineSize, int32& OutReserveAmmo);

	UFUNCTION(BlueprintPure, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static bool IsWeaponReloading(UObject* WorldContextObject, int64 WeaponEntityId);
};
