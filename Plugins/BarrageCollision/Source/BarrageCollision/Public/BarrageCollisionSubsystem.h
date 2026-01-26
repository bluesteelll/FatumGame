// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Barrage Collision Subsystem - Event listener and Blueprint API

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BarrageCollisionTypes.h"
#include "BarrageCollisionSubsystem.generated.h"

// ═══════════════════════════════════════════════════════════════════════════════
// COLLISION SUBSYSTEM
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * UBarrageCollisionSubsystem - World subsystem for Barrage collision processing
 *
 * Responsibilities:
 * - Listens to Barrage contact events
 * - Builds collision payloads with entity data
 * - Dispatches events through Phosphorus framework
 * - Provides Blueprint API for handler registration
 *
 * Architecture:
 * - All dispatch logic is in TPhosphorusEntityDispatcher
 * - This class is a thin integration layer
 * - Native code can access dispatcher directly via GetDispatcher()
 *
 * Usage (Blueprint):
 *   auto* Collision = UBarrageCollisionSubsystem::Get(GetWorld());
 *   Collision->RegisterTypeHandler(EEntityType::Projectile, EEntityType::Actor, MyHandler);
 *
 * Usage (C++):
 *   auto* Collision = UBarrageCollisionSubsystem::Get(GetWorld());
 *   Collision->GetDispatcher().RegisterTypeHandler(EEntityType::Projectile, EEntityType::Actor,
 *       FNativeCollisionHandler::CreateLambda([](const FBarrageCollisionPayload& P) {
 *           // Handle collision
 *           return true; // consumed
 *       }));
 */
UCLASS()
class BARRAGECOLLISION_API UBarrageCollisionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════════════
	// STATIC ACCESS
	// ═══════════════════════════════════════════════════════════════════════════

	/** Get subsystem instance for world (returns nullptr if world is null) */
	UFUNCTION(BlueprintPure, Category = "Barrage|Collision", meta = (WorldContext = "WorldContextObject"))
	static UBarrageCollisionSubsystem* Get(const UObject* WorldContextObject);

	// ═══════════════════════════════════════════════════════════════════════════
	// DISPATCHER ACCESS (Native)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Get the underlying dispatcher for advanced native usage
	 * Prefer this for C++ code - no delegate wrapping overhead
	 */
	FORCEINLINE FBarrageCollisionDispatcher& GetDispatcher() { return Dispatcher; }
	FORCEINLINE const FBarrageCollisionDispatcher& GetDispatcher() const { return Dispatcher; }

	// ═══════════════════════════════════════════════════════════════════════════
	// TYPE HANDLERS (Blueprint API)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a Blueprint handler for an entity type pair
	 *
	 * @param TypeA First entity type
	 * @param TypeB Second entity type (order doesn't matter - symmetric)
	 * @param Handler Delegate to call when collision occurs
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void RegisterTypeHandler(EEntityType TypeA, EEntityType TypeB, FBPCollisionHandler Handler);

	/**
	 * Unregister handler for an entity type pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void UnregisterTypeHandler(EEntityType TypeA, EEntityType TypeB);

	/**
	 * Check if a handler exists for entity type pair
	 */
	UFUNCTION(BlueprintPure, Category = "Barrage|Collision")
	bool HasTypeHandler(EEntityType TypeA, EEntityType TypeB) const;

	// ═══════════════════════════════════════════════════════════════════════════
	// TAG REGISTRATION (Blueprint API)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a gameplay tag for collision dispatch
	 *
	 * Tags must be registered before they can be used in handlers.
	 * Optionally specify a parent tag for hierarchy fallback.
	 *
	 * @param Tag The tag to register
	 * @param Parent Optional parent tag (handlers for parent match children)
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void RegisterTag(FGameplayTag Tag, FGameplayTag Parent = FGameplayTag());

	/**
	 * Check if a tag is registered for collision dispatch
	 */
	UFUNCTION(BlueprintPure, Category = "Barrage|Collision")
	bool IsTagRegistered(FGameplayTag Tag) const;

	// ═══════════════════════════════════════════════════════════════════════════
	// TAG HANDLERS (Blueprint API)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a Blueprint handler for a tag pair
	 *
	 * Tag handlers have lower priority than type handlers.
	 * Tags are looked up lazily from Artillery when needed.
	 *
	 * @param TagA First entity tag
	 * @param TagB Second entity tag
	 * @param Handler Delegate to call when collision occurs
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void RegisterTagHandler(FGameplayTag TagA, FGameplayTag TagB, FBPCollisionHandler Handler);

	/**
	 * Unregister tag handler for a tag pair
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void UnregisterTagHandler(FGameplayTag TagA, FGameplayTag TagB);

	/**
	 * Check if a tag handler exists for tag pair
	 */
	UFUNCTION(BlueprintPure, Category = "Barrage|Collision")
	bool HasTagHandler(FGameplayTag TagA, FGameplayTag TagB) const;

	// ═══════════════════════════════════════════════════════════════════════════
	// CACHE MANAGEMENT (Blueprint API)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Clear the entity tag cache
	 *
	 * Call this if entity tags change at runtime.
	 * Cache is automatically cleared on world teardown.
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void ClearCache();

	/**
	 * Invalidate cache entry for a specific entity
	 *
	 * Call this when a single entity's tags change.
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision")
	void InvalidateCacheEntry(int64 EntityKey);

	// ═══════════════════════════════════════════════════════════════════════════
	// DEBUG (Blueprint API)
	// ═══════════════════════════════════════════════════════════════════════════

	/** Dump current state to log for debugging */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Collision|Debug")
	void DumpState() const;

	/** Get number of registered type handlers */
	UFUNCTION(BlueprintPure, Category = "Barrage|Collision|Debug")
	int32 GetTypeHandlerCount() const;

	/** Get number of registered tag handlers */
	UFUNCTION(BlueprintPure, Category = "Barrage|Collision|Debug")
	int32 GetTagHandlerCount() const;

	/** Get number of cached entity tags */
	UFUNCTION(BlueprintPure, Category = "Barrage|Collision|Debug")
	int32 GetCacheSize() const;

protected:
	// ═══════════════════════════════════════════════════════════════════════════
	// LIFECYCLE
	// ═══════════════════════════════════════════════════════════════════════════

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	// ═══════════════════════════════════════════════════════════════════════════
	// STATE
	// ═══════════════════════════════════════════════════════════════════════════

	/** Phosphorus dispatcher - handles all dispatch logic */
	FBarrageCollisionDispatcher Dispatcher;

	/** Delegate handle for Barrage contact events */
	FDelegateHandle ContactEventHandle;

	// ═══════════════════════════════════════════════════════════════════════════
	// INTERNAL
	// ═══════════════════════════════════════════════════════════════════════════

	/** Called when Barrage detects a collision */
	void OnBarrageContact(const BarrageContactEvent& Event);

	/** Register default collision handlers */
	void RegisterDefaultHandlers();

	/** Tag lookup function for Artillery integration */
	static bool LookupEntityTag(uint64 Key, FGameplayTag Tag);
};

// ═══════════════════════════════════════════════════════════════════════════════
// LEGACY ALIAS
// ═══════════════════════════════════════════════════════════════════════════════

// Backwards compatibility alias - prefer UBarrageCollisionSubsystem
using UBarrageCollisionProcessors = UBarrageCollisionSubsystem;
