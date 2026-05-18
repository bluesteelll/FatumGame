// FlecsSaveBarrageRestore — encode + restore for FBarrageBody during save/load (Phase 5).
//
// Encoding side (sim thread, save fence):
//   For each save-worthy entity with FBarrageBody, the walker captures pos/rot/
//   linvel/angvel/objectlayer into FBarrageBodyState. The 88-byte block is written
//   into the per-entity record between the tag block and the component block (bit 0
//   of the entity record Flags signals "block is present").
//
// Restore side (sim thread, load fence):
//   After Pass 1 (component decode) completes, the snapshot reader iterates entities
//   with bHasBarrageBody and calls RestoreBarrageBodyForEntity. The helper recreates
//   the Jolt body from the entity's FEntityDefinitionRef (resolved via the path table)
//   + the saved transform, then binds the entity via BindEntityToBarrage.
//
// Phase 5 BODY-RESTORE LIMITATIONS (documented):
//   The restore helper supports projectile spheres, box static meshes, and door bodies
//   (the most common cases). Compound bodies, character virtuals (player), and complex
//   constraint chains (multiblock anchors) are scoped for Phase 6/7. For now the helper
//   logs a Warning and skips restoration; the Flecs entity stays alive but unbound, so
//   gameplay can continue without the body (limited functionality).

#pragma once

#include "CoreMinimal.h"

namespace flecs
{
	using entity_t = uint64;
	struct entity;
}

class UBarrageDispatch;

/** 88-byte on-disk snapshot of a Barrage body's transform + velocity + layer.
 *  Captured by the walker, restored by the snapshot reader's Pass 3. */
struct FATUMGAMESAVE_API FBarrageBodyState
{
	// World-space position (UE coords, cm). 24 bytes.
	double PosX = 0.0;
	double PosY = 0.0;
	double PosZ = 0.0;

	// World-space rotation as quaternion (UE convention). 16 bytes.
	float RotX = 0.f;
	float RotY = 0.f;
	float RotZ = 0.f;
	float RotW = 1.f;

	// Linear velocity (UE coords, cm/s). 24 bytes.
	double LinVelX = 0.0;
	double LinVelY = 0.0;
	double LinVelZ = 0.0;

	// Angular velocity (UE coords, rad/s — converted from Jolt at encode time). 24 bytes.
	double AngVelX = 0.0;
	double AngVelY = 0.0;
	double AngVelZ = 0.0;

	// (Some padding implied by struct alignment; the on-disk size below is what we
	// commit to via the offset reader's 88-byte skip.)
};

// On-disk size for the FBarrageBodyState block within the per-entity record.
// 24 (pos) + 16 (rot) + 24 (linvel) + 24 (angvel) = 88 bytes.
// The snapshot reader's offset table assumes exactly this; the assert below pins it.
static_assert(sizeof(FBarrageBodyState) == 88,
	"FBarrageBodyState size drift — on-disk layout assumed exactly 88 bytes by snapshot reader");

class UFlecsArtillerySubsystem;
struct FBarrageBody;

namespace FlecsSaveBarrage
{
	/** Read pos/rot/linvel/angvel from the entity's Barrage body. Returns false if the
	 *  entity has no FBarrageBody or the body is dead/tombstoned. Sim-thread only. */
	FATUMGAMESAVE_API bool EncodeBarrageBodyState(
		const flecs::entity& E,
		UBarrageDispatch* Barrage,
		FBarrageBodyState& Out);

	/** Recreate the Jolt body for the given entity using its FEntityDefinitionRef + the
	 *  saved transform. Binds the new body to the entity via BindEntityToBarrage on
	 *  success. Returns false on any failure (logs Warning). Sim-thread only.
	 *
	 *  PHASE 5 SCOPE: projectile spheres + simple static meshes + doors. Compound bodies
	 *  and character virtuals are deferred to Phase 6/7 — the helper logs + skips them
	 *  rather than constructing incorrect bodies. */
	FATUMGAMESAVE_API bool RestoreBarrageBodyForEntity(
		const flecs::entity& E,
		const FBarrageBodyState& Body,
		UBarrageDispatch* Barrage);

	/** Unbind a Flecs entity's Barrage body (atomic clear + DEBRIS layer + tombstone).
	 *  Used by the wipe pass to dispose of bodies cleanly before destructing entities.
	 *  Safe to call when Body.BarrageKey is invalid (no-op). Sim-thread only. */
	FATUMGAMESAVE_API void UnbindAndTombstoneBarrageBody(
		UFlecsArtillerySubsystem* Artillery,
		const FBarrageBody& Body);
}
