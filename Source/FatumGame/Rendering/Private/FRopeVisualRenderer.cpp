// Rope visual rendering: Verlet chain + Niagara Ribbon.
// Reads FRopeVisualAtomics (sim→game), computes visual segments, pushes to Niagara.
// Game thread only.

#include "FRopeVisualRenderer.h"
#include "FlecsSwingableComponents.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

static const FName RopePositionsName("RopePositions");

// ═══════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════

FRopeVisualRenderer::~FRopeVisualRenderer()
{
	Deactivate();
}

void FRopeVisualRenderer::Activate(USceneComponent* AttachTo, FRopeVisualAtomics* Atomics)
{
	check(AttachTo);
	check(Atomics);

	AttachRoot = AttachTo;
	RopeVisualAtomics = Atomics;
}

void FRopeVisualRenderer::Deactivate()
{
	bRopeVisualActive = false;
	RopeSegmentCount = 0;
	VerletPositions.Empty();
	VerletOldPositions.Empty();
	RopeVisualAtomics = nullptr;
	AttachRoot.Reset();

	DestroyNiagaraComponent();
}

// ═══════════════════════════════════════════════════════════════════════════
// UPDATE
// ═══════════════════════════════════════════════════════════════════════════

void FRopeVisualRenderer::Update(float DeltaTime, UWorld* World, FVector CharPos)
{
	if (!RopeVisualAtomics) return;

	const bool bActive = RopeVisualAtomics->bActive.Read();

	// ── Deactivation: was active, now inactive ──
	if (bRopeVisualActive && !bActive)
	{
		bRopeVisualActive = false;
		RopeSegmentCount = 0;
		VerletPositions.Empty();
		VerletOldPositions.Empty();
		DestroyNiagaraComponent();
		return;
	}

	if (!bActive) return;

	// ── Read atomics ──
	const FVector Anchor(
		RopeVisualAtomics->AnchorX.Read(),
		RopeVisualAtomics->AnchorY.Read(),
		RopeVisualAtomics->AnchorZ.Read());
	const float RopeLength = RopeVisualAtomics->RopeLength.Read();
	const int32 SegCount = RopeVisualAtomics->SegmentCount.Read();
	if (SegCount < 1) return;
	const int32 NodeCount = SegCount + 1;

	// Swing velocity (UE coords, cm/s) — used to bias Verlet for inertial trailing
	const FVector SwingVel(
		RopeVisualAtomics->VelX.Read(),
		RopeVisualAtomics->VelY.Read(),
		RopeVisualAtomics->VelZ.Read());

	// ── Activation: first frame ──
	if (!bRopeVisualActive)
	{
		bRopeVisualActive = true;
		RopeSegmentCount = SegCount;

		InitChain(NodeCount, SegCount, Anchor, CharPos);
		SpawnNiagaraComponent();

		// Push initial positions to Niagara
		if (RopeNiagaraComp)
		{
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(
				RopeNiagaraComp, RopePositionsName, VerletPositions);
		}
		return; // first frame: just initialize, Verlet starts next frame
	}

	// ── Segment count changed (shouldn't happen mid-swing, but guard) ──
	if (SegCount != RopeSegmentCount)
	{
		RopeSegmentCount = SegCount;
		InitChain(NodeCount, SegCount, Anchor, CharPos);
	}

	// ── Verlet integration ──
	check(World);
	const float VisualDT = FMath::Min(DeltaTime, 1.f / 30.f); // clamp to prevent explosion on hitches
	const FVector Gravity(0.f, 0.f, World->GetGravityZ()); // UE world gravity (cm/s^2)
	const float Damping = RopeVisualAtomics->VisualDamping.Read();
	const int32 Iterations = RopeVisualAtomics->ConstraintIterations.Read();

	// Inertial bias: mid-chain nodes get a fraction of swing velocity to produce trailing effect.
	// Without this, the visual chain freefalls with gravity only and doesn't match swing momentum.
	const float InertialStrength = 0.3f; // fraction of swing velocity applied to free nodes
	const FVector InertialForce = SwingVel * InertialStrength;

	// Pin endpoints
	VerletPositions[0] = Anchor;
	VerletPositions[NodeCount - 1] = CharPos;

	// Integrate free nodes (1 to NodeCount-2)
	for (int32 i = 1; i < NodeCount - 1; ++i)
	{
		FVector Vel = VerletPositions[i] - VerletOldPositions[i];
		VerletOldPositions[i] = VerletPositions[i];
		// Weight inertial influence by distance from midpoint (strongest at center)
		float MidWeight = 1.f - FMath::Abs(2.f * static_cast<float>(i) / static_cast<float>(SegCount) - 1.f);
		VerletPositions[i] += Vel * Damping
			+ Gravity * (VisualDT * VisualDT)
			+ InertialForce * (MidWeight * VisualDT * VisualDT);
	}

	// ── Constraint satisfaction ──
	const float TargetSegLen = RopeLength / static_cast<float>(SegCount);

	// Short rope guard: skip constraints when segments are too short (solver oscillates)
	if (TargetSegLen < 5.f)
	{
		for (int32 i = 0; i < NodeCount; ++i)
		{
			float Alpha = static_cast<float>(i) / static_cast<float>(SegCount);
			VerletPositions[i] = FMath::Lerp(Anchor, CharPos, Alpha);
			VerletOldPositions[i] = VerletPositions[i];
		}
	}
	else
	{
		for (int32 iter = 0; iter < Iterations; ++iter)
		{
			for (int32 i = 0; i < SegCount; ++i)
			{
				FVector Delta = VerletPositions[i + 1] - VerletPositions[i];
				float Dist = Delta.Size();
				if (Dist < 0.001f) continue;

				float Correction = 1.f - TargetSegLen / Dist;
				FVector CorrVec = Delta * (Correction * 0.5f);

				if (i != 0)
					VerletPositions[i] += CorrVec;
				if (i + 1 != NodeCount - 1)
					VerletPositions[i + 1] -= CorrVec;
			}

			// Re-pin endpoints after each iteration
			VerletPositions[0] = Anchor;
			VerletPositions[NodeCount - 1] = CharPos;
		}
	}

	// ── Push to Niagara ──
	if (RopeNiagaraComp)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(
			RopeNiagaraComp, RopePositionsName, VerletPositions);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// INTERNAL HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void FRopeVisualRenderer::InitChain(int32 NodeCount, int32 SegCount, const FVector& Anchor, const FVector& CharPos)
{
	VerletPositions.SetNumUninitialized(NodeCount);
	VerletOldPositions.SetNumUninitialized(NodeCount);
	for (int32 i = 0; i < NodeCount; ++i)
	{
		float Alpha = static_cast<float>(i) / static_cast<float>(SegCount);
		FVector Pos = FMath::Lerp(Anchor, CharPos, Alpha);
		VerletPositions[i] = Pos;
		VerletOldPositions[i] = Pos; // zero initial velocity
	}
}

void FRopeVisualRenderer::SpawnNiagaraComponent()
{
	if (RopeNiagaraComp) return;
	if (!RopeVisualAtomics) return;
	if (!AttachRoot.IsValid()) return;

	UNiagaraSystem* NiagaraSys = static_cast<UNiagaraSystem*>(
		RopeVisualAtomics->NiagaraSystemPtr.load(std::memory_order_relaxed));
	ensureMsgf(NiagaraSys, TEXT("FRopeVisualRenderer: RopeNiagaraSystem is null — set it in DA_Rope's RopeSwingProfile."));
	if (NiagaraSys)
	{
		RopeNiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			NiagaraSys, AttachRoot.Get(), NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepWorldPosition, false);
		if (RopeNiagaraComp)
		{
			RopeNiagaraComp->SetAutoDestroy(false);
		}
	}
}

void FRopeVisualRenderer::DestroyNiagaraComponent()
{
	if (RopeNiagaraComp)
	{
		RopeNiagaraComp->Deactivate();
		RopeNiagaraComp->DestroyComponent();
		RopeNiagaraComp = nullptr;
	}
}
