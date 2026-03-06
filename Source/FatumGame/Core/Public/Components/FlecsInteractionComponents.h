// Interaction components for Flecs entities.

#pragma once

#include "CoreMinimal.h"

class UFlecsInteractionProfile;

// ═══════════════════════════════════════════════════════════════
// INTERACTION STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static interaction data - lives in PREFAB, shared by all entities of this type.
 * Contains immutable interaction rules.
 */
struct FInteractionStatic
{
	/** Maximum interaction range (cm) */
	float MaxRange = 300.f;

	/** If true, entity becomes non-interactable after first use */
	bool bSingleUse = false;

	/** Interaction type (EInteractionType cast to uint8) */
	uint8 InteractionType = 0;

	/** Instant action type (EInstantAction cast to uint8, for Instant/Hold completion) */
	uint8 InstantAction = 0;

	/** If true, interaction is restricted to a cone defined by AngleCosine/AngleDirection */
	bool bRestrictAngle = false;

	/** Cosine of half-angle for the interaction cone (pre-computed for fast dot-product check).
	 *  Default -1 (cos(180°) = full sphere = no restriction) as defense-in-depth. */
	float AngleCosine = -1.f;

	/** Direction the cone faces in entity local space (normalized) */
	FVector AngleDirection = FVector::ForwardVector;

	static FInteractionStatic FromProfile(const UFlecsInteractionProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// INTERACTION INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Mutable per-entity interaction state.
 * Used for toggleable entities (switches, levers) and use counting.
 */
struct FInteractionInstance
{
	/** Current toggle state (for Toggle interactions) */
	bool bToggleState = false;

	/** How many times this entity has been interacted with */
	int32 UseCount = 0;
};

// ═══════════════════════════════════════════════════════════════
// INTERACTION ANGLE OVERRIDE (per-instance)
// ═══════════════════════════════════════════════════════════════

/**
 * Per-instance override for interaction angle restriction.
 * If present on entity, overrides InteractionProfile's angle settings.
 */
struct FInteractionAngleOverride
{
	/** Cosine of half-angle of the interaction cone (pre-computed) */
	float AngleCosine = -1.f;

	/** Direction the cone faces in entity local space (normalized) */
	FVector Direction = FVector::ForwardVector;
};
