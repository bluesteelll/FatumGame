// Lightweight GameplayTag-based pub/sub message system with MPSC bridge.
//
// Game thread: BroadcastMessage<T>(Tag, Msg) → listeners fire immediately.
// Any thread:  EnqueueMessage<T>(Tag, Msg) → queued, broadcast on next game thread Tick.
//
// Listeners are bound to UObject lifetime via TWeakObjectPtr — auto-skipped when dead.
// One channel = one message type. Type mismatch = fail-fast crash with diagnostic.
//
// Pattern follows CommandQueue (TQueue<TFunction, Mpsc>) from UFlecsArtillerySubsystem.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "FlecsMessageSubsystem.generated.h"

// Forward declaration
class UFlecsMessageSubsystem;

// ═══════════════════════════════════════════════════════════════
// LISTENER HANDLE
// ═══════════════════════════════════════════════════════════════

/**
 * Handle returned by RegisterListener. Call Unregister() to remove the listener.
 * Safe to call Unregister() multiple times or after subsystem destruction.
 */
struct FMessageListenerHandle
{
	FMessageListenerHandle() = default;

	/** Unregister this listener. Safe to call multiple times. */
	void Unregister();

	/** Check if handle points to a valid registration. */
	bool IsValid() const { return HandleId != 0 && Subsystem.IsValid(); }

	/** Reset without unregistering (for moves). */
	void Reset() { HandleId = 0; Subsystem.Reset(); }

private:
	friend class UFlecsMessageSubsystem;

	FMessageListenerHandle(UFlecsMessageSubsystem* InSubsystem, uint32 InHandleId)
		: Subsystem(InSubsystem), HandleId(InHandleId)
	{
	}

	TWeakObjectPtr<UFlecsMessageSubsystem> Subsystem;
	uint32 HandleId = 0;
};

// ═══════════════════════════════════════════════════════════════
// MESSAGE SUBSYSTEM
// ═══════════════════════════════════════════════════════════════

UCLASS()
class FATUMGAME_API UFlecsMessageSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Static pointer for sim thread access. Valid between OnWorldBeginPlay and Deinitialize. */
	static inline UFlecsMessageSubsystem* SelfPtr = nullptr;

	/** Get from any world context object. */
	static UFlecsMessageSubsystem* Get(const UObject* WorldContextObject);

	// ═══════════════════════════════════════════════════════════════
	// GAME THREAD API
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Broadcast a message to all listeners on the channel. Game thread only.
	 * No listeners on channel = instant return.
	 * Type mismatch (listener expects A, broadcast sends B) = fail-fast crash.
	 *
	 * @param Channel GameplayTag identifying the message channel.
	 * @param Message Typed USTRUCT payload.
	 */
	template<typename T>
	void BroadcastMessage(FGameplayTag Channel, const T& Message);

	/**
	 * Register a listener for typed messages on a channel. Game thread only.
	 * Listener is auto-skipped if BoundObject is garbage collected.
	 *
	 * @param Channel GameplayTag identifying the message channel.
	 * @param Object UObject whose lifetime controls this listener.
	 * @param Callback Invoked on broadcast with (Channel, Message).
	 * @return Handle for manual unregistration.
	 */
	template<typename T>
	FMessageListenerHandle RegisterListener(FGameplayTag Channel, UObject* Object,
		TFunction<void(FGameplayTag, const T&)> Callback);

	/**
	 * Unregister a listener by handle. Game thread only.
	 * Safe to call with invalid/expired handle.
	 */
	void UnregisterListener(FMessageListenerHandle& Handle);

	// ═══════════════════════════════════════════════════════════════
	// THREAD-SAFE API (callable from any thread)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Queue a message for broadcast on the next game thread Tick.
	 * Copies the message on the caller's thread. Lock-free MPSC enqueue.
	 *
	 * @param Channel GameplayTag identifying the message channel.
	 * @param Message Typed USTRUCT payload (copied).
	 */
	template<typename T>
	void EnqueueMessage(FGameplayTag Channel, const T& Message);

	// ═══════════════════════════════════════════════════════════════
	// LIFECYCLE
	// ═══════════════════════════════════════════════════════════════

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UFlecsMessageSubsystem, STATGROUP_Tickables);
	}

private:
	// ═══════════════════════════════════════════════════════════════
	// INTERNAL TYPES
	// ═══════════════════════════════════════════════════════════════

	struct FListenerEntry
	{
		uint32 HandleId = 0;
		UScriptStruct* StructType = nullptr;
		TWeakObjectPtr<UObject> BoundObject;
		TFunction<void(FGameplayTag, UScriptStruct*, const void*)> Callback;
	};

	struct FChannelData
	{
		TArray<FListenerEntry> Listeners;
	};

	// ═══════════════════════════════════════════════════════════════
	// INTERNAL METHODS
	// ═══════════════════════════════════════════════════════════════

	/** Remove all listeners whose BoundObject is no longer valid. */
	void PurgeDeadListeners();

	/** Internal unregister by handle ID. Returns true if found and removed. */
	bool UnregisterById(uint32 InHandleId);

	// ═══════════════════════════════════════════════════════════════
	// DATA
	// ═══════════════════════════════════════════════════════════════

	/** Channel → listener list. Only accessed from game thread. */
	TMap<FGameplayTag, FChannelData> Channels;

	/** Monotonically increasing handle ID. */
	uint32 NextHandleId = 1;

	/** MPSC queue for cross-thread message delivery. */
	TQueue<TFunction<void()>, EQueueMode::Mpsc> PendingBroadcasts;

	/** Broadcast counter for lazy dead-listener cleanup. */
	int32 BroadcastCounter = 0;

	/** Purge dead listeners every N broadcasts. */
	static constexpr int32 CleanupInterval = 60;
};

// ═══════════════════════════════════════════════════════════════
// TEMPLATE IMPLEMENTATIONS (must be in header)
// ═══════════════════════════════════════════════════════════════

template<typename T>
void UFlecsMessageSubsystem::BroadcastMessage(FGameplayTag Channel, const T& Message)
{
	check(IsInGameThread());
	checkf(Channel.IsValid(), TEXT("BroadcastMessage: invalid channel tag"));

	auto* ChannelData = Channels.Find(Channel);
	if (!ChannelData) { return; }

	UScriptStruct* BroadcastType = T::StaticStruct();

	for (auto& Listener : ChannelData->Listeners)
	{
		if (!Listener.BoundObject.IsValid()) { continue; }

		checkf(Listener.StructType == BroadcastType,
			TEXT("Type mismatch on channel [%s]: listener expects [%s], broadcast sends [%s]"),
			*Channel.ToString(),
			*Listener.StructType->GetName(),
			*BroadcastType->GetName());

		Listener.Callback(Channel, BroadcastType, &Message);
	}

	// Lazy cleanup of dead listeners
	if (++BroadcastCounter >= CleanupInterval)
	{
		BroadcastCounter = 0;
		PurgeDeadListeners();
	}
}

template<typename T>
FMessageListenerHandle UFlecsMessageSubsystem::RegisterListener(FGameplayTag Channel, UObject* Object,
	TFunction<void(FGameplayTag, const T&)> Callback)
{
	check(IsInGameThread());
	checkf(Channel.IsValid(), TEXT("RegisterListener: invalid channel tag"));
	checkf(Object != nullptr, TEXT("RegisterListener: null UObject"));
	checkf(static_cast<bool>(Callback), TEXT("RegisterListener: null callback"));

	const uint32 Id = NextHandleId++;

	FListenerEntry Entry;
	Entry.HandleId = Id;
	Entry.StructType = T::StaticStruct();
	Entry.BoundObject = Object;
	Entry.Callback = [Cb = MoveTemp(Callback)](FGameplayTag Tag, UScriptStruct*, const void* Data)
	{
		Cb(Tag, *static_cast<const T*>(Data));
	};

	Channels.FindOrAdd(Channel).Listeners.Add(MoveTemp(Entry));
	return FMessageListenerHandle(this, Id);
}

template<typename T>
void UFlecsMessageSubsystem::EnqueueMessage(FGameplayTag Channel, const T& Message)
{
	checkf(Channel.IsValid(), TEXT("EnqueueMessage: invalid channel tag"));

	T MessageCopy = Message;
	PendingBroadcasts.Enqueue([this, Channel, Msg = MoveTemp(MessageCopy)]()
	{
		BroadcastMessage<T>(Channel, Msg);
	});
}
