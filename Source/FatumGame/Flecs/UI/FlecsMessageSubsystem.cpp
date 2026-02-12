// UFlecsMessageSubsystem: Lightweight pub/sub + MPSC bridge.

#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "Engine/World.h"

// ═══════════════════════════════════════════════════════════════
// GAMEPLAY TAG DEFINITIONS
// ═══════════════════════════════════════════════════════════════

UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Health,      "UI.Health");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Death,       "UI.Death");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Ammo,        "UI.Weapon.Ammo");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Reload,      "UI.Weapon.Reload");
UE_DEFINE_GAMEPLAY_TAG(TAG_UI_Interaction,        "UI.Interaction");

// ═══════════════════════════════════════════════════════════════
// STATIC ACCESS
// ═══════════════════════════════════════════════════════════════

UFlecsMessageSubsystem* UFlecsMessageSubsystem::Get(const UObject* WorldContextObject)
{
	check(WorldContextObject);
	const UWorld* World = WorldContextObject->GetWorld();
	check(World);
	return World->GetSubsystem<UFlecsMessageSubsystem>();
}

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsMessageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SelfPtr = this;
}

void UFlecsMessageSubsystem::Deinitialize()
{
	SelfPtr = nullptr;
	Channels.Empty();

	// Drain remaining messages (discard — world is shutting down)
	TFunction<void()> Discard;
	while (PendingBroadcasts.Dequeue(Discard)) {}

	Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════
// TICK — DRAIN MPSC QUEUE
// ═══════════════════════════════════════════════════════════════

void UFlecsMessageSubsystem::Tick(float DeltaTime)
{
	TFunction<void()> Pending;
	while (PendingBroadcasts.Dequeue(Pending))
	{
		Pending();
	}
}

// ═══════════════════════════════════════════════════════════════
// UNREGISTER
// ═══════════════════════════════════════════════════════════════

void UFlecsMessageSubsystem::UnregisterListener(FMessageListenerHandle& Handle)
{
	check(IsInGameThread());

	if (Handle.HandleId == 0) { return; }

	UnregisterById(Handle.HandleId);
	Handle.Reset();
}

bool UFlecsMessageSubsystem::UnregisterById(uint32 InHandleId)
{
	for (auto& [Tag, ChannelData] : Channels)
	{
		const int32 Idx = ChannelData.Listeners.IndexOfByPredicate(
			[InHandleId](const FListenerEntry& Entry) { return Entry.HandleId == InHandleId; });

		if (Idx != INDEX_NONE)
		{
			ChannelData.Listeners.RemoveAtSwap(Idx);
			return true;
		}
	}
	return false;
}

// ═══════════════════════════════════════════════════════════════
// DEAD LISTENER CLEANUP
// ═══════════════════════════════════════════════════════════════

void UFlecsMessageSubsystem::PurgeDeadListeners()
{
	for (auto& [Tag, ChannelData] : Channels)
	{
		ChannelData.Listeners.RemoveAllSwap(
			[](const FListenerEntry& Entry) { return !Entry.BoundObject.IsValid(); });
	}
}

// ═══════════════════════════════════════════════════════════════
// LISTENER HANDLE
// ═══════════════════════════════════════════════════════════════

void FMessageListenerHandle::Unregister()
{
	if (HandleId == 0) { return; }

	if (UFlecsMessageSubsystem* Sub = Subsystem.Get())
	{
		Sub->UnregisterListener(*this);
	}
	else
	{
		HandleId = 0;
		Subsystem.Reset();
	}
}
