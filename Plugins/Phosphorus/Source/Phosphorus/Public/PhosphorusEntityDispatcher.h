// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Phosphorus Entity Dispatcher - Unified Type + Tag dispatch with entity cache

#pragma once

#include "CoreMinimal.h"
#include "PhosphorusDispatcher.h"

/**
 * TPhosphorusEntityDispatcher - Unified collision dispatch system
 *
 * Combines three dispatch mechanisms in priority order:
 * 1. TYPE DISPATCH (O(1)) - Fast matrix lookup based on type enum
 * 2. TAG DISPATCH (O(log N)) - Flexible Phosphorus tag-based dispatch
 *
 * Features:
 * - Type dispatch uses symmetric matrix for O(1) lookup
 * - Tag dispatch with hierarchy support (child types fallback to parent handlers)
 * - Entity tag cache for external tag lookups
 * - Separate Blueprint and Native handler chains
 *
 * Template Parameters:
 * - TPayload: The payload struct passed to handlers
 * - NumTypes: Number of distinct types in the type enum
 *
 * Usage:
 *   // For an enum with 14 types (including NUM_TYPES sentinel)
 *   TPhosphorusEntityDispatcher<FMyPayload, 14> Dispatcher(TEXT("MySystem"));
 *
 *   // Fast type-based handler (types are uint8)
 *   Dispatcher.RegisterTypeHandler(4, 6, MyHandler); // Projectile + Actor
 *
 *   // Flexible tag-based handler
 *   Dispatcher.RegisterTagHandler(TAG_Boss, TAG_Weapon, BossWeaponHandler);
 *
 *   // Dispatch - tries Type first, then Tags
 *   Dispatcher.Dispatch(TypeA, TypeB, TagA, TagB, Payload);
 *
 * @tparam TPayload - The payload struct passed to handlers
 * @tparam NumTypes - Number of types in the type enum (typically your_enum::NUM_TYPES)
 */
template<typename TPayload, int32 NumTypes>
class TPhosphorusEntityDispatcher
{
public:
	// Handler delegate type - returns true to stop processing chain
	using FHandler = TDelegate<bool(const TPayload&)>;

	// Type index type (uint8 is sufficient for most enums)
	using TypeIndex = uint8;

	// Size of symmetric matrix
	static constexpr int32 TypeMatrixSize = (NumTypes * (NumTypes + 1)) / 2;

	// ═══════════════════════════════════════════════════════════════════════════
	// CONSTRUCTION
	// ═══════════════════════════════════════════════════════════════════════════

	TPhosphorusEntityDispatcher()
		: Name(TEXT("EntityDispatcher"))
		, NativeTagDispatcher(TEXT("Tags.Native"))
		, BlueprintTagDispatcher(TEXT("Tags.Blueprint"))
	{
		InitializeTypeMatrix();
		UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Created (default, %d types)"), *Name, NumTypes);
	}

	explicit TPhosphorusEntityDispatcher(const FString& InName)
		: Name(InName)
		, NativeTagDispatcher(InName + TEXT(".Tags.Native"))
		, BlueprintTagDispatcher(InName + TEXT(".Tags.Blueprint"))
	{
		InitializeTypeMatrix();
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Created (%d types)"), *Name, NumTypes);
	}

	~TPhosphorusEntityDispatcher()
	{
		UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Destroyed (types: %d, tags: %d)"),
			*Name, TypeHandlerCount, GetTagHandlerCount());
	}

	// Non-copyable
	TPhosphorusEntityDispatcher(const TPhosphorusEntityDispatcher&) = delete;
	TPhosphorusEntityDispatcher& operator=(const TPhosphorusEntityDispatcher&) = delete;

	// Movable
	TPhosphorusEntityDispatcher(TPhosphorusEntityDispatcher&&) = default;
	TPhosphorusEntityDispatcher& operator=(TPhosphorusEntityDispatcher&&) = default;

	// ═══════════════════════════════════════════════════════════════════════════
	// CONFIGURATION
	// ═══════════════════════════════════════════════════════════════════════════

	void SetName(const FString& InName)
	{
		Name = InName;
		NativeTagDispatcher.SetName(InName + TEXT(".Tags.Native"));
		BlueprintTagDispatcher.SetName(InName + TEXT(".Tags.Blueprint"));
	}

	const FString& GetName() const { return Name; }

	// ═══════════════════════════════════════════════════════════════════════════
	// TYPE DISPATCH (O(1) matrix lookup)
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a native handler for a type pair
	 * Handler returns true to consume event, false to continue to next handler
	 *
	 * @param TypeA First type index (0 to NumTypes-1)
	 * @param TypeB Second type index (0 to NumTypes-1)
	 * @param Handler The handler delegate
	 */
	Phosphorus::ERegistrationResult RegisterTypeHandler(TypeIndex TypeA, TypeIndex TypeB, FHandler Handler)
	{
		if (!Handler.IsBound())
		{
			UE_LOG(LogPhosphorus, Warning, TEXT("[%s] RegisterTypeHandler: Unbound handler"), *Name);
			return Phosphorus::ERegistrationResult::InvalidHandler;
		}

		if (TypeA >= NumTypes || TypeB >= NumTypes)
		{
			UE_LOG(LogPhosphorus, Error, TEXT("[%s] RegisterTypeHandler: Type out of range [%d, %d] (max %d)"),
				*Name, TypeA, TypeB, NumTypes - 1);
			return Phosphorus::ERegistrationResult::InvalidTag;
		}

		int32 Index = GetTypeIndex(TypeA, TypeB);
		checkf(Index >= 0 && Index < TypeMatrixSize,
			TEXT("[%s] Type index out of bounds: %d (max %d)"), *Name, Index, TypeMatrixSize - 1);

		bool bWasEmpty = !TypeHandlers[Index].IsBound();
		TypeHandlers[Index] = MoveTemp(Handler);

		if (bWasEmpty)
		{
			TypeHandlerCount++;
		}

		UE_LOG(LogPhosphorus, Log, TEXT("[%s] RegisterTypeHandler: [%d + %d] at index %d %s"),
			*Name, TypeA, TypeB, Index,
			bWasEmpty ? TEXT("(new)") : TEXT("(replaced)"));

		return bWasEmpty ? Phosphorus::ERegistrationResult::Success : Phosphorus::ERegistrationResult::Replaced;
	}

	/**
	 * Register a handler using enum values directly
	 * Convenience overload that static_casts enum to uint8
	 */
	template<typename TEnum>
	Phosphorus::ERegistrationResult RegisterTypeHandler(TEnum TypeA, TEnum TypeB, FHandler Handler)
	{
		return RegisterTypeHandler(
			static_cast<TypeIndex>(TypeA),
			static_cast<TypeIndex>(TypeB),
			MoveTemp(Handler));
	}

	/**
	 * Unregister handler for a type pair
	 */
	bool UnregisterTypeHandler(TypeIndex TypeA, TypeIndex TypeB)
	{
		if (TypeA >= NumTypes || TypeB >= NumTypes)
		{
			return false;
		}

		int32 Index = GetTypeIndex(TypeA, TypeB);
		if (Index >= 0 && Index < TypeMatrixSize && TypeHandlers[Index].IsBound())
		{
			TypeHandlers[Index].Unbind();
			TypeHandlerCount--;
			UE_LOG(LogPhosphorus, Log, TEXT("[%s] UnregisterTypeHandler: [%d + %d]"), *Name, TypeA, TypeB);
			return true;
		}
		return false;
	}

	template<typename TEnum>
	bool UnregisterTypeHandler(TEnum TypeA, TEnum TypeB)
	{
		return UnregisterTypeHandler(static_cast<TypeIndex>(TypeA), static_cast<TypeIndex>(TypeB));
	}

	/**
	 * Check if a handler exists for type pair
	 */
	bool HasTypeHandler(TypeIndex TypeA, TypeIndex TypeB) const
	{
		if (TypeA >= NumTypes || TypeB >= NumTypes)
		{
			return false;
		}
		int32 Index = GetTypeIndex(TypeA, TypeB);
		return Index >= 0 && Index < TypeMatrixSize && TypeHandlers[Index].IsBound();
	}

	template<typename TEnum>
	bool HasTypeHandler(TEnum TypeA, TEnum TypeB) const
	{
		return HasTypeHandler(static_cast<TypeIndex>(TypeA), static_cast<TypeIndex>(TypeB));
	}

	/** Get number of registered type handlers */
	int32 GetTypeHandlerCount() const { return TypeHandlerCount; }

	// ═══════════════════════════════════════════════════════════════════════════
	// TAG DISPATCH (Phosphorus, O(log N))
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a gameplay tag with optional parent for hierarchy fallback
	 */
	Phosphorus::ERegistrationResult RegisterTag(FGameplayTag Tag, FGameplayTag Parent = FGameplayTag())
	{
		if (!Tag.IsValid())
		{
			return Phosphorus::ERegistrationResult::InvalidTag;
		}

		NativeTagDispatcher.RegisterType(Tag, Parent);
		BlueprintTagDispatcher.RegisterType(Tag, Parent);

		if (!RegisteredTags.Contains(Tag))
		{
			RegisteredTags.Add(Tag);
		}

		UE_LOG(LogPhosphorus, Log, TEXT("[%s] RegisterTag: '%s'%s"),
			*Name, *Tag.ToString(),
			Parent.IsValid() ? *FString::Printf(TEXT(" -> parent '%s'"), *Parent.ToString()) : TEXT(""));

		return Phosphorus::ERegistrationResult::Success;
	}

	/** Check if a tag is registered */
	bool IsTagRegistered(FGameplayTag Tag) const
	{
		return NativeTagDispatcher.IsTypeRegistered(Tag);
	}

	/** Get all registered tags */
	const TArray<FGameplayTag>& GetRegisteredTags() const { return RegisteredTags; }

	/**
	 * Register a native tag handler for a tag pair (higher priority)
	 */
	Phosphorus::ERegistrationResult RegisterNativeTagHandler(FGameplayTag TagA, FGameplayTag TagB, FHandler Handler)
	{
		if (!Handler.IsBound())
		{
			return Phosphorus::ERegistrationResult::InvalidHandler;
		}

		auto Result = NativeTagDispatcher.RegisterHandler(TagA, TagB, MoveTemp(Handler));
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] RegisterNativeTagHandler: [%s + %s] -> %s"),
			*Name, *TagA.ToString(), *TagB.ToString(), Phosphorus::ToString(Result));
		return Result;
	}

	/**
	 * Register a Blueprint tag handler for a tag pair (lower priority)
	 */
	Phosphorus::ERegistrationResult RegisterBlueprintTagHandler(FGameplayTag TagA, FGameplayTag TagB, FHandler Handler)
	{
		if (!Handler.IsBound())
		{
			return Phosphorus::ERegistrationResult::InvalidHandler;
		}

		auto Result = BlueprintTagDispatcher.RegisterHandler(TagA, TagB, MoveTemp(Handler));
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] RegisterBlueprintTagHandler: [%s + %s] -> %s"),
			*Name, *TagA.ToString(), *TagB.ToString(), Phosphorus::ToString(Result));
		return Result;
	}

	/**
	 * Unregister tag handler for a tag pair (both native and blueprint)
	 */
	bool UnregisterTagHandler(FGameplayTag TagA, FGameplayTag TagB)
	{
		bool bRemovedNative = NativeTagDispatcher.UnregisterHandler(TagA, TagB);
		bool bRemovedBP = BlueprintTagDispatcher.UnregisterHandler(TagA, TagB);
		return bRemovedNative || bRemovedBP;
	}

	/**
	 * Check if a tag handler exists for tag pair
	 */
	bool HasTagHandler(FGameplayTag TagA, FGameplayTag TagB) const
	{
		return NativeTagDispatcher.HasHandler(TagA, TagB) ||
		       BlueprintTagDispatcher.HasHandler(TagA, TagB);
	}

	/** Get number of registered tag handlers */
	int32 GetTagHandlerCount() const
	{
		return NativeTagDispatcher.GetRegisteredHandlerCount() +
		       BlueprintTagDispatcher.GetRegisteredHandlerCount();
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// ENTITY TAG CACHE
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Set the tag lookup function for entity → tag resolution
	 *
	 * @param LookupFunc Function that takes (EntityKey, Tag) and returns true if entity has tag
	 */
	void SetTagLookupFunction(TFunction<bool(uint64 Key, FGameplayTag Tag)> LookupFunc)
	{
		TagLookupFunction = MoveTemp(LookupFunc);
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Tag lookup function set"), *Name);
	}

	/**
	 * Get cached gameplay tag for entity (or lookup and cache)
	 */
	FGameplayTag GetEntityTag(uint64 Key)
	{
		if (Key == 0)
		{
			return FGameplayTag();
		}

		// Check cache first
		if (FGameplayTag* Cached = EntityTagCache.Find(Key))
		{
			return *Cached;
		}

		// Lookup using provided function
		FGameplayTag Result;
		if (TagLookupFunction)
		{
			for (const FGameplayTag& Tag : RegisteredTags)
			{
				if (TagLookupFunction(Key, Tag))
				{
					Result = Tag;
					break;
				}
			}
		}

		// Cache result (even if invalid - prevents repeated lookups)
		EntityTagCache.Add(Key, Result);
		return Result;
	}

	/** Clear entity tag cache */
	void ClearEntityCache()
	{
		int32 Size = EntityTagCache.Num();
		EntityTagCache.Reset();
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Entity cache cleared (%d entries)"), *Name, Size);
	}

	/** Invalidate cache entry for specific entity */
	void InvalidateCacheEntry(uint64 Key)
	{
		EntityTagCache.Remove(Key);
	}

	/** Get number of cached entity tags */
	int32 GetCacheSize() const
	{
		return EntityTagCache.Num();
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// UNIFIED DISPATCH
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Dispatch event with full context (types + tags)
	 *
	 * Priority order:
	 * 1. Type handlers (O(1) matrix lookup)
	 * 2. Native tag handlers (higher priority)
	 * 3. Blueprint tag handlers (lower priority)
	 *
	 * @param TypeA Type index of first entity
	 * @param TypeB Type index of second entity
	 * @param TagA Gameplay tag of first entity (can be invalid)
	 * @param TagB Gameplay tag of second entity (can be invalid)
	 * @param Payload Event data
	 * @return Dispatch result
	 */
	Phosphorus::EDispatchResult Dispatch(
		TypeIndex TypeA, TypeIndex TypeB,
		FGameplayTag TagA, FGameplayTag TagB,
		const TPayload& Payload)
	{
		// 1. Try type dispatch first (fastest)
		if (TypeA < NumTypes && TypeB < NumTypes)
		{
			int32 Index = GetTypeIndex(TypeA, TypeB);
			if (Index >= 0 && Index < TypeMatrixSize && TypeHandlers[Index].IsBound())
			{
				bool bConsumed = TypeHandlers[Index].Execute(Payload);
				if (bConsumed)
				{
					UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Type handler consumed [%d + %d]"),
						*Name, TypeA, TypeB);
					return Phosphorus::EDispatchResult::Consumed;
				}
			}
		}

		// 2. Try tag dispatch (if we have tags)
		if (TagA.IsValid() || TagB.IsValid())
		{
			// Native handlers first
			Phosphorus::EDispatchResult NativeResult = NativeTagDispatcher.Dispatch(TagA, TagB, Payload);
			if (NativeResult == Phosphorus::EDispatchResult::Consumed)
			{
				UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Native tag handler consumed [%s + %s]"),
					*Name, *TagA.ToString(), *TagB.ToString());
				return Phosphorus::EDispatchResult::Consumed;
			}

			// Blueprint handlers
			Phosphorus::EDispatchResult BPResult = BlueprintTagDispatcher.Dispatch(TagA, TagB, Payload);
			if (BPResult == Phosphorus::EDispatchResult::Consumed)
			{
				UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Blueprint tag handler consumed [%s + %s]"),
					*Name, *TagA.ToString(), *TagB.ToString());
				return Phosphorus::EDispatchResult::Consumed;
			}

			// Handler found but didn't consume
			if (NativeResult == Phosphorus::EDispatchResult::Handled ||
			    BPResult == Phosphorus::EDispatchResult::Handled)
			{
				return Phosphorus::EDispatchResult::Handled;
			}
		}

		return Phosphorus::EDispatchResult::NoHandler;
	}

	/**
	 * Dispatch using enum types directly
	 */
	template<typename TEnum>
	Phosphorus::EDispatchResult Dispatch(
		TEnum TypeA, TEnum TypeB,
		FGameplayTag TagA, FGameplayTag TagB,
		const TPayload& Payload)
	{
		return Dispatch(
			static_cast<TypeIndex>(TypeA),
			static_cast<TypeIndex>(TypeB),
			TagA, TagB, Payload);
	}

	/**
	 * Dispatch with automatic tag lookup from entity keys
	 *
	 * @param TypeA Type index of first entity
	 * @param TypeB Type index of second entity
	 * @param KeyA Entity key of first entity (for tag lookup)
	 * @param KeyB Entity key of second entity (for tag lookup)
	 * @param Payload Event data
	 * @return Dispatch result
	 */
	Phosphorus::EDispatchResult DispatchWithKeyLookup(
		TypeIndex TypeA, TypeIndex TypeB,
		uint64 KeyA, uint64 KeyB,
		const TPayload& Payload)
	{
		// Try type dispatch first (doesn't need tags)
		if (TypeA < NumTypes && TypeB < NumTypes)
		{
			int32 Index = GetTypeIndex(TypeA, TypeB);
			if (Index >= 0 && Index < TypeMatrixSize && TypeHandlers[Index].IsBound())
			{
				bool bConsumed = TypeHandlers[Index].Execute(Payload);
				if (bConsumed)
				{
					return Phosphorus::EDispatchResult::Consumed;
				}
			}
		}

		// Only lookup tags if we have registered tags
		if (RegisteredTags.Num() > 0)
		{
			FGameplayTag TagA = GetEntityTag(KeyA);
			FGameplayTag TagB = GetEntityTag(KeyB);

			if (TagA.IsValid() || TagB.IsValid())
			{
				// Native handlers
				Phosphorus::EDispatchResult NativeResult = NativeTagDispatcher.Dispatch(TagA, TagB, Payload);
				if (NativeResult == Phosphorus::EDispatchResult::Consumed)
				{
					return Phosphorus::EDispatchResult::Consumed;
				}

				// Blueprint handlers
				Phosphorus::EDispatchResult BPResult = BlueprintTagDispatcher.Dispatch(TagA, TagB, Payload);
				if (BPResult == Phosphorus::EDispatchResult::Consumed)
				{
					return Phosphorus::EDispatchResult::Consumed;
				}

				if (NativeResult == Phosphorus::EDispatchResult::Handled ||
				    BPResult == Phosphorus::EDispatchResult::Handled)
				{
					return Phosphorus::EDispatchResult::Handled;
				}
			}
		}

		return Phosphorus::EDispatchResult::NoHandler;
	}

	/**
	 * Dispatch with automatic tag lookup using enum types
	 */
	template<typename TEnum>
	Phosphorus::EDispatchResult DispatchWithKeyLookup(
		TEnum TypeA, TEnum TypeB,
		uint64 KeyA, uint64 KeyB,
		const TPayload& Payload)
	{
		return DispatchWithKeyLookup(
			static_cast<TypeIndex>(TypeA),
			static_cast<TypeIndex>(TypeB),
			KeyA, KeyB, Payload);
	}

	/**
	 * Simple dispatch that returns bool
	 */
	bool DispatchSimple(
		TypeIndex TypeA, TypeIndex TypeB,
		FGameplayTag TagA, FGameplayTag TagB,
		const TPayload& Payload)
	{
		auto Result = Dispatch(TypeA, TypeB, TagA, TagB, Payload);
		return Result == Phosphorus::EDispatchResult::Handled ||
		       Result == Phosphorus::EDispatchResult::Consumed;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// UTILITIES
	// ═══════════════════════════════════════════════════════════════════════════

	/** Clear all state */
	void Reset()
	{
		int32 TypeCount = TypeHandlerCount;
		int32 TagCount = GetTagHandlerCount();
		int32 CacheSize = EntityTagCache.Num();

		// Clear type handlers
		for (auto& Handler : TypeHandlers)
		{
			Handler.Unbind();
		}
		TypeHandlerCount = 0;

		// Clear tag dispatchers
		NativeTagDispatcher.Reset();
		BlueprintTagDispatcher.Reset();

		// Clear cache and tags
		EntityTagCache.Empty();
		RegisteredTags.Empty();

		UE_LOG(LogPhosphorus, Log,
			TEXT("[%s] Reset: cleared %d type handlers, %d tag handlers, %d cache entries"),
			*Name, TypeCount, TagCount, CacheSize);
	}

	/** Dump state to log */
	void DumpState() const
	{
		UE_LOG(LogPhosphorus, Log, TEXT("════════════════════════════════════════════════════"));
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] STATE DUMP"), *Name);
		UE_LOG(LogPhosphorus, Log, TEXT("════════════════════════════════════════════════════"));
		UE_LOG(LogPhosphorus, Log, TEXT("  Num Types: %d"), NumTypes);
		UE_LOG(LogPhosphorus, Log, TEXT("  Matrix Size: %d"), TypeMatrixSize);
		UE_LOG(LogPhosphorus, Log, TEXT("  Type Handlers: %d"), TypeHandlerCount);
		UE_LOG(LogPhosphorus, Log, TEXT("  Tag Handlers (Native): %d"), NativeTagDispatcher.GetRegisteredHandlerCount());
		UE_LOG(LogPhosphorus, Log, TEXT("  Tag Handlers (Blueprint): %d"), BlueprintTagDispatcher.GetRegisteredHandlerCount());
		UE_LOG(LogPhosphorus, Log, TEXT("  Registered Tags: %d"), RegisteredTags.Num());
		UE_LOG(LogPhosphorus, Log, TEXT("  Entity Cache: %d entries"), EntityTagCache.Num());
		UE_LOG(LogPhosphorus, Log, TEXT("════════════════════════════════════════════════════"));

		// Dump registered type handlers
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Type Handlers:"), *Name);
		for (int32 i = 0; i < NumTypes; ++i)
		{
			for (int32 j = i; j < NumTypes; ++j)
			{
				int32 Index = GetTypeIndex(static_cast<TypeIndex>(i), static_cast<TypeIndex>(j));
				if (Index >= 0 && Index < TypeMatrixSize && TypeHandlers[Index].IsBound())
				{
					UE_LOG(LogPhosphorus, Log, TEXT("  [%d + %d] -> handler bound"), i, j);
				}
			}
		}

		// Dump tag dispatchers
		NativeTagDispatcher.DumpState();
		BlueprintTagDispatcher.DumpState();
	}

private:
	// ═══════════════════════════════════════════════════════════════════════════
	// TYPE MATRIX HELPERS
	// ═══════════════════════════════════════════════════════════════════════════

	void InitializeTypeMatrix()
	{
		TypeHandlers.SetNum(TypeMatrixSize);
		TypeHandlerCount = 0;
		UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Type matrix: %d slots for %d types"),
			*Name, TypeMatrixSize, NumTypes);
	}

	/**
	 * Get matrix index for type pair (symmetric)
	 * Uses upper triangle storage: index = i * N - i*(i+1)/2 + j where i <= j
	 */
	static int32 GetTypeIndex(TypeIndex A, TypeIndex B)
	{
		TypeIndex i = A;
		TypeIndex j = B;

		// Ensure i <= j for symmetry
		if (i > j)
		{
			Swap(i, j);
		}

		return i * NumTypes - (i * (i + 1)) / 2 + j;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// STATE
	// ═══════════════════════════════════════════════════════════════════════════

	FString Name;

	// Type dispatch (O(1) matrix)
	TArray<FHandler> TypeHandlers;
	int32 TypeHandlerCount = 0;

	// Tag dispatch (Phosphorus)
	TPhosphorusDispatcher<TPayload> NativeTagDispatcher;
	TPhosphorusDispatcher<TPayload> BlueprintTagDispatcher;
	TArray<FGameplayTag> RegisteredTags;

	// Entity tag cache
	TMap<uint64, FGameplayTag> EntityTagCache;
	TFunction<bool(uint64, FGameplayTag)> TagLookupFunction;
};
