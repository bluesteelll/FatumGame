// FRopeVisualRenderer: Verlet chain + Niagara Ribbon renderer for rope swing visual.
//
// Plain C++ class (not UCLASS). Reads FRopeVisualAtomics from sim thread,
// computes Verlet positions, pushes to Niagara via Array Data Interface.
// Game thread only — called from AFlecsCharacter::Tick().

#pragma once

#include "CoreMinimal.h"

class UNiagaraComponent;
class USceneComponent;
class UWorld;
struct FRopeVisualAtomics;

class FRopeVisualRenderer
{
public:
	FRopeVisualRenderer() = default;
	~FRopeVisualRenderer();

	// Non-copyable, non-movable (owns UNiagaraComponent)
	FRopeVisualRenderer(const FRopeVisualRenderer&) = delete;
	FRopeVisualRenderer& operator=(const FRopeVisualRenderer&) = delete;

	/** Begin rendering rope visual. Stores atomics pointer and attachment root. */
	void Activate(USceneComponent* AttachTo, FRopeVisualAtomics* Atomics);

	/** Stop rendering and destroy Niagara component. */
	void Deactivate();

	/** Tick: read atomics, compute Verlet chain, push to Niagara. */
	void Update(float DeltaTime, UWorld* World, FVector CharPos);

	/** Is the renderer currently active (has atomics and was activated)? */
	bool IsActive() const { return bRopeVisualActive; }

private:
	// ─────────────────────────────────────────────────────────
	// STATE
	// ─────────────────────────────────────────────────────────
	FRopeVisualAtomics* RopeVisualAtomics = nullptr;
	TWeakObjectPtr<USceneComponent> AttachRoot;

	TArray<FVector> VerletPositions;
	TArray<FVector> VerletOldPositions;
	int32 RopeSegmentCount = 0;
	bool bRopeVisualActive = false;

	/** Active Niagara component (spawned/destroyed with rope swing). */
	TObjectPtr<UNiagaraComponent> RopeNiagaraComp;

	// ─────────────────────────────────────────────────────────
	// INTERNAL
	// ─────────────────────────────────────────────────────────

	/** Initialize Verlet chain as straight line from Anchor to CharPos. */
	void InitChain(int32 NodeCount, int32 SegCount, const FVector& Anchor, const FVector& CharPos);

	/** Spawn Niagara component from atomics' NiagaraSystemPtr. */
	void SpawnNiagaraComponent();

	/** Destroy and null the Niagara component. */
	void DestroyNiagaraComponent();
};
