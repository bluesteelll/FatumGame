// Base model for FlecsUI.
// Manages view binding, pending operations, and bridge to sim thread.
// Uses flecs::entity for entity identity.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUITypes.h"
#include "flecs.h"
#include "FlecsUIModel.generated.h"

UCLASS(Abstract)
class FLECSUI_API UFlecsUIModel : public UObject
{
	GENERATED_BODY()

public:
	// ═══ View Management ═══

	/** Bind a view (widget) to receive updates. View must implement IFlecsUIView (or derived). */
	void BindView(UObject* View);

	/** Unbind a view. Safe to call if not bound. */
	void UnbindView(UObject* View);

	/** Get all currently bound views (with stale weak pointers cleaned). */
	const TArray<TWeakObjectPtr<UObject>>& GetViews() const { return Views; }

	// ═══ Lifecycle ═══

	/** Activate this model for a specific Flecs entity. */
	virtual void Activate(flecs::entity InEntity);

	/** Deactivate — clears entity, pending ops, notifies views. */
	virtual void Deactivate();

	bool IsActive() const { return Entity.is_valid(); }
	flecs::entity GetEntity() const { return Entity; }

	// ═══ Op Confirmation (called by subsystem from MPSC drain) ═══

	void ReceiveOpResult(uint32 OperationId, EUIOpResult Result);

	// ═══ Bridge to Sim Thread ═══
	// Set by subsystem. Captures EnqueueCommand via closure.
	// Signature: (OpId, SimThreadLambda) → enqueues to sim thread.
	TFunction<void(uint32 OpId, TFunction<void()>&&)> ExecuteOnSim;

protected:
	/** Allocate a unique operation ID (game thread only). */
	uint32 AllocateOpId();

	/** Find and complete a pending op. Returns true if found. */
	bool CompletePendingOp(uint32 OpId, EUIOpResult Result);

	/** Active pending operations awaiting sim confirmation. */
	TArray<FPendingOp> PendingOps;

	/** Bound views (weak pointers, auto-cleaned on access). */
	TArray<TWeakObjectPtr<UObject>> Views;

	/** The Flecs entity this model represents. */
	flecs::entity Entity;

	/** Monotonically increasing op counter. */
	uint32 NextOpId = 1;
};
