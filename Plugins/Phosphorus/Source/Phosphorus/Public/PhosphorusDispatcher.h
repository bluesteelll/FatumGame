// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Phosphorus Event Dispatch Framework - Core Dispatcher Template

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PhosphorusTypes.h"

/**
 * TPhosphorusDispatcher - Generic event dispatch system with matrix-based handler lookup
 *
 * Features:
 * - O(1) handler lookup via type index matrix
 * - Tag hierarchy support (child types fallback to parent handlers)
 * - Symmetric dispatch (A+B == B+A unless explicitly registered both ways)
 * - Per-dispatcher type registry (fully independent systems)
 *
 * Usage:
 * 1. Create dispatcher: TPhosphorusDispatcher<FMyPayload> Dispatcher(TEXT("MySystem"));
 * 2. Register types: Dispatcher.RegisterType(TAG_Player);
 * 3. Register handlers: Dispatcher.RegisterHandler(TAG_Player, TAG_Monster, MyHandler);
 * 4. Dispatch events: Dispatcher.Dispatch(TypeA, TypeB, PayloadData);
 *
 * @tparam TPayload - The payload struct passed to handlers
 */
template<typename TPayload>
class TPhosphorusDispatcher
{
public:
	// Handler delegate type - returns true to stop processing chain
	using FHandler = TDelegate<bool(const TPayload&)>;

	// ═══════════════════════════════════════════════════════════════════════════
	// CONSTRUCTION
	// ═══════════════════════════════════════════════════════════════════════════

	TPhosphorusDispatcher()
		: Name(Phosphorus::DefaultDispatcherName)
	{
		UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Dispatcher created (default)"), *Name);
	}

	explicit TPhosphorusDispatcher(const FString& InName)
		: Name(InName)
	{
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Dispatcher created"), *Name);
	}

	~TPhosphorusDispatcher()
	{
		UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] Dispatcher destroyed (types: %d, handlers: %d)"),
			*Name, TypeToIndex.Num(), CountHandlers());
	}

	// Non-copyable (handlers contain bound delegates)
	TPhosphorusDispatcher(const TPhosphorusDispatcher&) = delete;
	TPhosphorusDispatcher& operator=(const TPhosphorusDispatcher&) = delete;

	// Movable
	TPhosphorusDispatcher(TPhosphorusDispatcher&&) = default;
	TPhosphorusDispatcher& operator=(TPhosphorusDispatcher&&) = default;

	// ═══════════════════════════════════════════════════════════════════════════
	// CONFIGURATION
	// ═══════════════════════════════════════════════════════════════════════════

	/** Set dispatcher name (used in logging) */
	void SetName(const FString& InName)
	{
		FString OldName = Name;
		Name = InName;
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Renamed from '%s'"), *Name, *OldName);
	}

	/** Get dispatcher name */
	const FString& GetName() const { return Name; }

	// ═══════════════════════════════════════════════════════════════════════════
	// TYPE REGISTRATION
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a type tag with optional parent for hierarchy fallback
	 *
	 * @param Type - The gameplay tag identifying this type
	 * @param Parent - Optional parent tag (handlers registered for parent will match children)
	 * @return Registration result
	 *
	 * Example:
	 *   RegisterType(TAG_Monster_Zombie, TAG_Monster);  // Zombie inherits Monster handlers
	 */
	Phosphorus::ERegistrationResult RegisterType(FGameplayTag Type, FGameplayTag Parent = FGameplayTag())
	{
		// Validate input
		if (!Type.IsValid())
		{
			UE_LOG(LogPhosphorus, Error, TEXT("[%s] RegisterType: Invalid tag provided"), *Name);
			return Phosphorus::ERegistrationResult::InvalidTag;
		}

		// Check if already registered
		bool bIsNew = !TypeToIndex.Contains(Type);

		// Get or create index
		uint8 Index = GetOrCreateIndex(Type);
		if (Index == Phosphorus::InvalidTypeIndex)
		{
			UE_LOG(LogPhosphorus, Error, TEXT("[%s] RegisterType: Max types (%d) reached for '%s'"),
				*Name, Phosphorus::MaxTypeIndex, *Type.ToString());
			return Phosphorus::ERegistrationResult::MaxTypesReached;
		}

		// Handle parent registration
		if (Parent.IsValid())
		{
			// Ensure parent is registered
			uint8 ParentIndex = GetOrCreateIndex(Parent);
			if (ParentIndex == Phosphorus::InvalidTypeIndex)
			{
				UE_LOG(LogPhosphorus, Error, TEXT("[%s] RegisterType: Max types reached when registering parent '%s'"),
					*Name, *Parent.ToString());
				return Phosphorus::ERegistrationResult::MaxTypesReached;
			}

			TypeToParent.Add(Type, Parent);

			UE_LOG(LogPhosphorus, Log, TEXT("[%s] RegisterType: '%s' (index %d) -> parent '%s' (index %d)"),
				*Name, *Type.ToString(), Index, *Parent.ToString(), ParentIndex);
		}
		else if (bIsNew)
		{
			UE_LOG(LogPhosphorus, Log, TEXT("[%s] RegisterType: '%s' (index %d)"),
				*Name, *Type.ToString(), Index);
		}
		else
		{
			UE_LOG(LogPhosphorus, Verbose, TEXT("[%s] RegisterType: '%s' already registered (index %d)"),
				*Name, *Type.ToString(), Index);
		}

		return bIsNew ? Phosphorus::ERegistrationResult::Success : Phosphorus::ERegistrationResult::Replaced;
	}

	/** Check if a type is registered */
	bool IsTypeRegistered(FGameplayTag Type) const
	{
		return TypeToIndex.Contains(Type);
	}

	/** Get the parent of a registered type (returns invalid tag if no parent) */
	FGameplayTag GetTypeParent(FGameplayTag Type) const
	{
		const FGameplayTag* Parent = TypeToParent.Find(Type);
		return Parent ? *Parent : FGameplayTag();
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// HANDLER REGISTRATION
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Register a handler for a type pair
	 *
	 * @param TypeA - First type tag
	 * @param TypeB - Second type tag
	 * @param Handler - Delegate to call when this type pair collides
	 * @return Registration result
	 *
	 * Note: Registration is directional. RegisterHandler(A, B, H) stores at Matrix[A][B].
	 * Dispatch checks both [A][B] and [B][A] for symmetry.
	 */
	Phosphorus::ERegistrationResult RegisterHandler(FGameplayTag TypeA, FGameplayTag TypeB, FHandler Handler)
	{
		// Validate tags
		if (!TypeA.IsValid() || !TypeB.IsValid())
		{
			UE_LOG(LogPhosphorus, Error,
				TEXT("[%s] RegisterHandler: Invalid tag(s) - A:'%s' B:'%s'"),
				*Name, *TypeA.ToString(), *TypeB.ToString());
			return Phosphorus::ERegistrationResult::InvalidTag;
		}

		// Validate handler
		if (!Handler.IsBound())
		{
			UE_LOG(LogPhosphorus, Error,
				TEXT("[%s] RegisterHandler: Unbound handler for [%s + %s]"),
				*Name, *TypeA.ToString(), *TypeB.ToString());
			return Phosphorus::ERegistrationResult::InvalidHandler;
		}

		// Get or create indices
		uint8 IdxA = GetOrCreateIndex(TypeA);
		uint8 IdxB = GetOrCreateIndex(TypeB);

		if (IdxA == Phosphorus::InvalidTypeIndex || IdxB == Phosphorus::InvalidTypeIndex)
		{
			UE_LOG(LogPhosphorus, Error,
				TEXT("[%s] RegisterHandler: Max types reached for [%s + %s]"),
				*Name, *TypeA.ToString(), *TypeB.ToString());
			return Phosphorus::ERegistrationResult::MaxTypesReached;
		}

		// Store handler
		bool bExisted = Matrix[IdxA][IdxB].IsValid();
		Matrix[IdxA][IdxB].Handler = MoveTemp(Handler);

		UE_LOG(LogPhosphorus, Log,
			TEXT("[%s] RegisterHandler: [%s + %s] at [%d][%d] %s"),
			*Name,
			*TypeA.ToString(), *TypeB.ToString(),
			IdxA, IdxB,
			bExisted ? TEXT("(replaced)") : TEXT("(new)"));

		return bExisted ? Phosphorus::ERegistrationResult::Replaced : Phosphorus::ERegistrationResult::Success;
	}

	/**
	 * Unregister a handler for a type pair
	 *
	 * @param TypeA - First type tag
	 * @param TypeB - Second type tag
	 * @return true if handler was removed, false if not found
	 */
	bool UnregisterHandler(FGameplayTag TypeA, FGameplayTag TypeB)
	{
		uint8 IdxA = GetIndex(TypeA);
		uint8 IdxB = GetIndex(TypeB);

		if (IdxA == Phosphorus::InvalidTypeIndex || IdxB == Phosphorus::InvalidTypeIndex)
		{
			UE_LOG(LogPhosphorus, Warning,
				TEXT("[%s] UnregisterHandler: Type not registered - A:'%s' B:'%s'"),
				*Name, *TypeA.ToString(), *TypeB.ToString());
			return false;
		}

		if (IdxA < Matrix.Num() && IdxB < Matrix[IdxA].Num() && Matrix[IdxA][IdxB].IsValid())
		{
			Matrix[IdxA][IdxB] = FHandlerEntry();
			UE_LOG(LogPhosphorus, Log,
				TEXT("[%s] UnregisterHandler: [%s + %s] removed"),
				*Name, *TypeA.ToString(), *TypeB.ToString());
			return true;
		}

		UE_LOG(LogPhosphorus, Warning,
			TEXT("[%s] UnregisterHandler: No handler at [%s + %s]"),
			*Name, *TypeA.ToString(), *TypeB.ToString());
		return false;
	}

	/** Check if a handler exists for a type pair (includes fallback check) */
	bool HasHandler(FGameplayTag TypeA, FGameplayTag TypeB) const
	{
		return FindHandlerWithFallback(TypeA, TypeB) != nullptr;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// DISPATCH
	// ═══════════════════════════════════════════════════════════════════════════

	/**
	 * Dispatch an event to the appropriate handler
	 *
	 * @param TypeA - First entity type
	 * @param TypeB - Second entity type
	 * @param Payload - Event data to pass to handler
	 * @return Dispatch result
	 *
	 * Lookup order:
	 * 1. Exact match [A][B] or [B][A]
	 * 2. Parent(A) + B or B + Parent(A)
	 * 3. A + Parent(B) or Parent(B) + A
	 * 4. Parent(A) + Parent(B) or Parent(B) + Parent(A)
	 */
	Phosphorus::EDispatchResult Dispatch(FGameplayTag TypeA, FGameplayTag TypeB, const TPayload& Payload)
	{
		// Validate input
		if (!TypeA.IsValid() && !TypeB.IsValid())
		{
			UE_LOG(LogPhosphorus, Verbose,
				TEXT("[%s] Dispatch: Both types invalid, skipping"),
				*Name);
			return Phosphorus::EDispatchResult::InvalidInput;
		}

		// Find handler
		const FHandlerEntry* Entry = FindHandlerWithFallback(TypeA, TypeB);
		if (!Entry)
		{
			UE_LOG(LogPhosphorus, Verbose,
				TEXT("[%s] Dispatch: No handler for [%s + %s]"),
				*Name, *TypeA.ToString(), *TypeB.ToString());
			return Phosphorus::EDispatchResult::NoHandler;
		}

		// Execute handler
		UE_LOG(LogPhosphorus, Verbose,
			TEXT("[%s] Dispatch: Executing handler for [%s + %s]"),
			*Name, *TypeA.ToString(), *TypeB.ToString());

		bool bConsumed = Entry->Handler.Execute(Payload);

		UE_LOG(LogPhosphorus, Verbose,
			TEXT("[%s] Dispatch: Handler returned %s"),
			*Name, bConsumed ? TEXT("Consumed") : TEXT("Handled"));

		return bConsumed ? Phosphorus::EDispatchResult::Consumed : Phosphorus::EDispatchResult::Handled;
	}

	/**
	 * Simplified dispatch that returns bool (true if any handler was called)
	 */
	bool DispatchSimple(FGameplayTag TypeA, FGameplayTag TypeB, const TPayload& Payload)
	{
		Phosphorus::EDispatchResult Result = Dispatch(TypeA, TypeB, Payload);
		return Result == Phosphorus::EDispatchResult::Handled ||
		       Result == Phosphorus::EDispatchResult::Consumed;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// UTILITIES
	// ═══════════════════════════════════════════════════════════════════════════

	/** Clear all types and handlers */
	void Reset()
	{
		int32 TypeCount = TypeToIndex.Num();
		int32 HandlerCount = CountHandlers();

		TypeToIndex.Empty();
		IndexToType.Empty();
		TypeToParent.Empty();
		Matrix.Empty();

		UE_LOG(LogPhosphorus, Log,
			TEXT("[%s] Reset: Cleared %d types, %d handlers"),
			*Name, TypeCount, HandlerCount);
	}

	/** Get number of registered types */
	int32 GetRegisteredTypeCount() const { return TypeToIndex.Num(); }

	/** Get number of registered handlers */
	int32 GetRegisteredHandlerCount() const { return CountHandlers(); }

	/** Dump current state to log for debugging */
	void DumpState() const
	{
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] ════════════════════════════════════════"), *Name);
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] STATE DUMP"), *Name);
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] ════════════════════════════════════════"), *Name);

		// Types
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Registered Types: %d"), *Name, TypeToIndex.Num());
		for (const auto& Pair : TypeToIndex)
		{
			const FGameplayTag* Parent = TypeToParent.Find(Pair.Key);
			if (Parent && Parent->IsValid())
			{
				UE_LOG(LogPhosphorus, Log, TEXT("[%s]   [%3d] %s -> parent: %s"),
					*Name, Pair.Value, *Pair.Key.ToString(), *Parent->ToString());
			}
			else
			{
				UE_LOG(LogPhosphorus, Log, TEXT("[%s]   [%3d] %s"),
					*Name, Pair.Value, *Pair.Key.ToString());
			}
		}

		// Handlers
		int32 HandlerCount = CountHandlers();
		UE_LOG(LogPhosphorus, Log, TEXT("[%s] Registered Handlers: %d"), *Name, HandlerCount);
		for (int32 A = 0; A < Matrix.Num(); ++A)
		{
			for (int32 B = 0; B < Matrix[A].Num(); ++B)
			{
				if (Matrix[A][B].IsValid())
				{
					FGameplayTag TagA = (A < IndexToType.Num()) ? IndexToType[A] : FGameplayTag();
					FGameplayTag TagB = (B < IndexToType.Num()) ? IndexToType[B] : FGameplayTag();
					UE_LOG(LogPhosphorus, Log, TEXT("[%s]   [%d][%d] %s + %s"),
						*Name, A, B, *TagA.ToString(), *TagB.ToString());
				}
			}
		}

		UE_LOG(LogPhosphorus, Log, TEXT("[%s] ════════════════════════════════════════"), *Name);
	}

private:
	// ═══════════════════════════════════════════════════════════════════════════
	// INTERNAL TYPES
	// ═══════════════════════════════════════════════════════════════════════════

	struct FHandlerEntry
	{
		FHandler Handler;

		bool IsValid() const { return Handler.IsBound(); }
	};

	// ═══════════════════════════════════════════════════════════════════════════
	// INTERNAL STATE
	// ═══════════════════════════════════════════════════════════════════════════

	FString Name;

	// Type registry
	TMap<FGameplayTag, uint8> TypeToIndex;
	TArray<FGameplayTag> IndexToType;
	TMap<FGameplayTag, FGameplayTag> TypeToParent;

	// Handler matrix [TypeA][TypeB] -> Handler
	TArray<TArray<FHandlerEntry>> Matrix;

	// ═══════════════════════════════════════════════════════════════════════════
	// INTERNAL HELPERS
	// ═══════════════════════════════════════════════════════════════════════════

	/** Get existing index or create new one */
	uint8 GetOrCreateIndex(FGameplayTag Type)
	{
		// Return existing
		if (uint8* Existing = TypeToIndex.Find(Type))
		{
			return *Existing;
		}

		// Check capacity
		if (IndexToType.Num() >= Phosphorus::MaxTypeIndex)
		{
			UE_LOG(LogPhosphorus, Error,
				TEXT("[%s] GetOrCreateIndex: Maximum types (%d) reached!"),
				*Name, Phosphorus::MaxTypeIndex);
			return Phosphorus::InvalidTypeIndex;
		}

		// Create new
		uint8 NewIndex = static_cast<uint8>(IndexToType.Num());
		TypeToIndex.Add(Type, NewIndex);
		IndexToType.Add(Type);
		EnsureMatrixSize(NewIndex);

		UE_LOG(LogPhosphorus, Verbose,
			TEXT("[%s] GetOrCreateIndex: Created index %d for '%s'"),
			*Name, NewIndex, *Type.ToString());

		return NewIndex;
	}

	/** Get existing index (returns InvalidTypeIndex if not found) */
	uint8 GetIndex(FGameplayTag Type) const
	{
		const uint8* Idx = TypeToIndex.Find(Type);
		return Idx ? *Idx : Phosphorus::InvalidTypeIndex;
	}

	/** Ensure matrix has room for index */
	void EnsureMatrixSize(uint8 Index)
	{
		int32 Required = Index + 1;

		// Expand rows
		while (Matrix.Num() < Required)
		{
			Matrix.AddDefaulted();
		}

		// Expand columns in all rows
		for (auto& Row : Matrix)
		{
			while (Row.Num() < Required)
			{
				Row.AddDefaulted();
			}
		}
	}

	/** Direct matrix lookup (no fallback) */
	const FHandlerEntry* FindHandler(uint8 A, uint8 B) const
	{
		if (A >= Matrix.Num() || B >= Matrix[A].Num())
		{
			return nullptr;
		}

		const FHandlerEntry* Entry = &Matrix[A][B];
		return Entry->IsValid() ? Entry : nullptr;
	}

	/**
	 * Find handler with parent fallback
	 *
	 * Search order:
	 * 1. [A][B], [B][A] - exact match
	 * 2. [Parent(A)][B], [B][Parent(A)] - A's parent
	 * 3. [A][Parent(B)], [Parent(B)][A] - B's parent
	 * 4. [Parent(A)][Parent(B)], [Parent(B)][Parent(A)] - both parents
	 */
	const FHandlerEntry* FindHandlerWithFallback(FGameplayTag TypeA, FGameplayTag TypeB) const
	{
		uint8 IdxA = GetIndex(TypeA);
		uint8 IdxB = GetIndex(TypeB);

		// Both invalid - no possible handler
		if (IdxA == Phosphorus::InvalidTypeIndex && IdxB == Phosphorus::InvalidTypeIndex)
		{
			return nullptr;
		}

		// 1. Exact match [A][B] or [B][A]
		if (IdxA != Phosphorus::InvalidTypeIndex && IdxB != Phosphorus::InvalidTypeIndex)
		{
			if (const FHandlerEntry* H = FindHandler(IdxA, IdxB))
			{
				UE_LOG(LogPhosphorus, Verbose,
					TEXT("[%s] FindHandler: Exact match [%s][%s]"),
					*Name, *TypeA.ToString(), *TypeB.ToString());
				return H;
			}
			if (const FHandlerEntry* H = FindHandler(IdxB, IdxA))
			{
				UE_LOG(LogPhosphorus, Verbose,
					TEXT("[%s] FindHandler: Symmetric match [%s][%s]"),
					*Name, *TypeB.ToString(), *TypeA.ToString());
				return H;
			}
		}

		// Get parents
		const FGameplayTag* ParentA = TypeToParent.Find(TypeA);
		const FGameplayTag* ParentB = TypeToParent.Find(TypeB);

		// 2. Parent of A + B
		if (ParentA && ParentA->IsValid())
		{
			uint8 ParentIdxA = GetIndex(*ParentA);
			if (ParentIdxA != Phosphorus::InvalidTypeIndex && IdxB != Phosphorus::InvalidTypeIndex)
			{
				if (const FHandlerEntry* H = FindHandler(ParentIdxA, IdxB))
				{
					UE_LOG(LogPhosphorus, Verbose,
						TEXT("[%s] FindHandler: Fallback [%s->%s][%s]"),
						*Name, *TypeA.ToString(), *ParentA->ToString(), *TypeB.ToString());
					return H;
				}
				if (const FHandlerEntry* H = FindHandler(IdxB, ParentIdxA))
				{
					UE_LOG(LogPhosphorus, Verbose,
						TEXT("[%s] FindHandler: Fallback symmetric [%s][%s->%s]"),
						*Name, *TypeB.ToString(), *TypeA.ToString(), *ParentA->ToString());
					return H;
				}
			}
		}

		// 3. A + Parent of B
		if (ParentB && ParentB->IsValid())
		{
			uint8 ParentIdxB = GetIndex(*ParentB);
			if (IdxA != Phosphorus::InvalidTypeIndex && ParentIdxB != Phosphorus::InvalidTypeIndex)
			{
				if (const FHandlerEntry* H = FindHandler(IdxA, ParentIdxB))
				{
					UE_LOG(LogPhosphorus, Verbose,
						TEXT("[%s] FindHandler: Fallback [%s][%s->%s]"),
						*Name, *TypeA.ToString(), *TypeB.ToString(), *ParentB->ToString());
					return H;
				}
				if (const FHandlerEntry* H = FindHandler(ParentIdxB, IdxA))
				{
					UE_LOG(LogPhosphorus, Verbose,
						TEXT("[%s] FindHandler: Fallback symmetric [%s->%s][%s]"),
						*Name, *TypeB.ToString(), *ParentB->ToString(), *TypeA.ToString());
					return H;
				}
			}
		}

		// 4. Both parents
		if (ParentA && ParentB && ParentA->IsValid() && ParentB->IsValid())
		{
			uint8 PA = GetIndex(*ParentA);
			uint8 PB = GetIndex(*ParentB);
			if (PA != Phosphorus::InvalidTypeIndex && PB != Phosphorus::InvalidTypeIndex)
			{
				if (const FHandlerEntry* H = FindHandler(PA, PB))
				{
					UE_LOG(LogPhosphorus, Verbose,
						TEXT("[%s] FindHandler: Fallback both parents [%s->%s][%s->%s]"),
						*Name, *TypeA.ToString(), *ParentA->ToString(),
						*TypeB.ToString(), *ParentB->ToString());
					return H;
				}
				if (const FHandlerEntry* H = FindHandler(PB, PA))
				{
					UE_LOG(LogPhosphorus, Verbose,
						TEXT("[%s] FindHandler: Fallback both parents symmetric [%s->%s][%s->%s]"),
						*Name, *TypeB.ToString(), *ParentB->ToString(),
						*TypeA.ToString(), *ParentA->ToString());
					return H;
				}
			}
		}

		return nullptr;
	}

	/** Count total valid handlers in matrix */
	int32 CountHandlers() const
	{
		int32 Count = 0;
		for (const auto& Row : Matrix)
		{
			for (const auto& Entry : Row)
			{
				if (Entry.IsValid())
				{
					++Count;
				}
			}
		}
		return Count;
	}
};
