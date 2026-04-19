// Multiblock assembly — Phase 2 of the Crafting System.
//
// See CRAFTING_PHASE2_BLUEPRINT_V2.md + V3.md for design.
// Two-pass detection (collect candidates, bond after iterator closes) prevents
// archetype migration mid-query. Scan runs at 2 Hz (every 30 sim ticks).

#include "FlecsArtillerySubsystem.h"

#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "EPhysicsLayer.h"
#include "IsolatedJoltIncludes.h"  // JPH::EMotionType (SetBodyMotionType argument)
#include "PhysicsFilters/FastObjectLayerFilters.h"

#include "FlecsBarrageComponents.h"
#include "FlecsGameTags.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemComponents.h"
#include "FlecsCraftingLog.h"
#include "FlecsMultiblockBlueprint.h"
#include "Components/FlecsMultiblockComponents.h"
#include "Components/FlecsCraftingComponents.h"
#include "Library/FlecsMultiblockRuntime.h"

#include "HAL/IConsoleManager.h"

namespace
{
	/** Detection runs every N sim ticks (60 Hz sim → 30 ticks ≈ 2 Hz). */
	constexpr int32 kDetectionTicksInterval = 30;

	/** CVar-gated debug logging for near-miss child matches (v3 PATCH 6). */
	static TAutoConsoleVariable<int32> CVarDebugMultiblock(
		TEXT("fatum.Crafting.DebugMultiblock"),
		0,
		TEXT("1 = log near-miss multiblock matches for authoring diagnosis"),
		ECVF_Default);

	/** One anchor ↔ {blueprint, matched children} record collected during Pass 1. */
	struct FBondRequest
	{
		flecs::entity                               Anchor;
		const UFlecsMultiblockBlueprint*            Blueprint = nullptr;
		TArray<int64, TInlineAllocator<15>>         ChildEntityIds;
	};

	/** Pass-1 helper: try to match every child spec in Blueprint against the
	 *  candidate set; write matched ids into OutMatchedChildIds (1-to-1 with
	 *  Blueprint->Children). Returns false (and leaves output indeterminate)
	 *  if any spec has no acceptable candidate. */
	static bool TryMatchAllChildren(
		flecs::world& World,
		flecs::entity Anchor,
		const FVector& AnchorPos,
		const UFlecsMultiblockBlueprint* Blueprint,
		const TArray<uint64, TInlineAllocator<64>>& CandidateEntIds,
		const TArray<FVector, TInlineAllocator<64>>& CandidatePositions,
		TArray<int64, TInlineAllocator<15>>& OutMatchedChildIds)
	{
		OutMatchedChildIds.Reset();
		OutMatchedChildIds.SetNumZeroed(Blueprint->Children.Num());

		TArray<bool, TInlineAllocator<64>> Used;
		Used.SetNumZeroed(CandidateEntIds.Num());

		for (int32 i = 0; i < Blueprint->Children.Num(); ++i)
		{
			const FMultiblockChildPartSpec& Spec = Blueprint->Children[i];
			if (!Spec.PartDefinition) return false;

			const FVector ExpectedPos = AnchorPos + Spec.RelativeOffset;
			const float TolSq = Spec.PositionTolerance * Spec.PositionTolerance;
			const float NearMissSq = 4.f * TolSq;

			int32 BestIdx = INDEX_NONE;
			float BestDistSq = TolSq;
			int32 NearMissCt = 0;

			for (int32 c = 0; c < CandidateEntIds.Num(); ++c)
			{
				if (Used[c]) continue;

				flecs::entity CandE = World.entity(static_cast<flecs::entity_t>(CandidateEntIds[c]));
				if (!CandE.is_alive() || CandE == Anchor) continue;
				if (CandE.has<FTagDead>() || CandE.has<FTagMultiblockBonded>()) continue;

				const FMultiblockPartStatic* PS = CandE.try_get<FMultiblockPartStatic>();
				if (!PS || PS->Blueprint != Blueprint) continue;

				// v3 PATCH 3: anchor/child distinction via tag only.
				if (CandE.has<FTagMultiblockAnchor>()) continue;

				if (PS->PartRole != Spec.PartRole) continue;

				const FEntityDefinitionRef* DefRef = CandE.try_get<FEntityDefinitionRef>();
				if (DefRef && DefRef->Definition != Spec.PartDefinition) continue;

				const float DistSq = FVector::DistSquared(CandidatePositions[c], ExpectedPos);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestIdx = c;
				}
				else if (DistSq < NearMissSq)
				{
					++NearMissCt;
				}
			}

			if (BestIdx == INDEX_NONE)
			{
				// v3 PATCH 6: only emit the near-miss warning when the designer
				// explicitly enabled diagnostic logging AND there was at least
				// one candidate within 2× tolerance (otherwise the message is
				// noise — nothing close).
				if (CVarDebugMultiblock.GetValueOnAnyThread() != 0 && NearMissCt > 0)
				{
					UE_LOG(LogCrafting, Warning,
						TEXT("[Multiblock] near-miss: blueprint=%s role=%s nearMiss=%d (need 1 within %.1fcm)"),
						*Blueprint->BlueprintId.ToString(),
						*Spec.PartRole.ToString(),
						NearMissCt,
						Spec.PositionTolerance);
				}
				return false;
			}

			Used[BestIdx] = true;
			OutMatchedChildIds[i] = static_cast<int64>(CandidateEntIds[BestIdx]);
		}

		return true;
	}
}

// ═══════════════════════════════════════════════════════════════
// DETECTION SYSTEM
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::SetupMultiblockSystems()
{
	check(FlecsWorld);
	flecs::world& World = *FlecsWorld;

	// Anchor query — every live, non-dead, non-bonded entity carrying the
	// part-static + physics body + anchor tag.
	auto AnchorQuery = World.query_builder<const FMultiblockPartStatic, const FBarrageBody>()
		.with<FTagMultiblockPart>()
		.with<FTagMultiblockAnchor>()
		.without<FTagMultiblockBonded>()
		.without<FTagDead>()
		.build();

	// MultiblockDetectionSystem runs on OnUpdate with the same scheduling as
	// the rest of the crafting pipeline, BEFORE CraftingSnapshotFlushSystem —
	// so the first-tick snapshot publish on bond happens the same sim tick.
	World.system<>("MultiblockDetectionSystem")
		.kind(flecs::OnUpdate)
		.run([this, AnchorQuery](flecs::iter&)
		{
			// 2 Hz throttle — SphereSearch + nested lookups aren't cheap at 60 Hz.
			if (CraftingMultiblockTickCountdown > 0)
			{
				--CraftingMultiblockTickCountdown;
				return;
			}
			CraftingMultiblockTickCountdown = kDetectionTicksInterval - 1;

			EnsureBarrageAccess();
			if (!CachedBarrageDispatch) return;

			flecs::world& W = *FlecsWorld;

			// Collect bond requests during iteration — no mutation yet.
			TArray<FBondRequest, TInlineAllocator<8>> BondRequests;

			// ─── PASS 1 — iterate, collect. NO mutation. ────────────────
			AnchorQuery.each([&](flecs::entity Anchor,
			                     const FMultiblockPartStatic& PS,
			                     const FBarrageBody& Body)
			{
				const UFlecsMultiblockBlueprint* BP = PS.Blueprint;
				if (!BP || !BP->StationProfile || BP->Children.Num() == 0) return;
				if (!Body.IsValid()) return;

				FBLet Prim = CachedBarrageDispatch->GetShapeRef(Body.BarrageKey);
				if (!FBarragePrimitive::IsNotNull(Prim)) return;

				const FVector3f AnchorPosF = FBarragePrimitive::GetPosition(Prim);
				const FVector AnchorPos(AnchorPosF.X, AnchorPosF.Y, AnchorPosF.Z);

				// SphereSearch expects Jolt coordinates (meters). Convert.
				const double RadiusJolt = BP->DetectionScanRadius / 100.0;

				// MOVING-only filter (R4): editor-placed static parts unsupported.
				FastIncludeObjectLayerFilter ObjFilter({EPhysicsLayer::MOVING});
				auto BPFilter = CachedBarrageDispatch->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
				JPH::BodyFilter NoBodyFilter;

				uint32 FoundCount = 0;
				TArray<uint32> FoundBodies;
				FoundBodies.Reserve(64);

				CachedBarrageDispatch->SphereSearch(
					FBarrageKey(),       // no source body exclusion at broadphase level
					AnchorPos,
					RadiusJolt,
					BPFilter,
					ObjFilter,
					NoBodyFilter,
					&FoundCount,
					FoundBodies);

				if (FoundCount == 0) return;

				// Body-id → Flecs-entity resolution for every candidate, with position cache.
				TArray<uint64, TInlineAllocator<64>> CandidateEntIds;
				TArray<FVector, TInlineAllocator<64>> CandidatePositions;
				CandidateEntIds.Reserve(FoundCount);
				CandidatePositions.Reserve(FoundCount);

				for (uint32 k = 0; k < FoundCount; ++k)
				{
					const FBarrageKey BK = CachedBarrageDispatch->GenerateBarrageKeyFromBodyId(FoundBodies[k]);
					FBLet CandPrim = CachedBarrageDispatch->GetShapeRef(BK);
					if (!FBarragePrimitive::IsNotNull(CandPrim)) continue;

					const uint64 FlecsId = CandPrim->GetFlecsEntity();
					if (FlecsId == 0) continue;

					flecs::entity CE = W.entity(static_cast<flecs::entity_t>(FlecsId));
					if (!CE.is_alive()) continue;

					const FVector3f CandPosF = FBarragePrimitive::GetPosition(CandPrim);
					CandidateEntIds.Add(FlecsId);
					CandidatePositions.Add(FVector(CandPosF.X, CandPosF.Y, CandPosF.Z));
				}

				if (CandidateEntIds.Num() < BP->Children.Num()) return;

				TArray<int64, TInlineAllocator<15>> MatchedIds;
				if (!TryMatchAllChildren(W, Anchor, AnchorPos, BP,
				                          CandidateEntIds, CandidatePositions, MatchedIds))
				{
					return;
				}

				FBondRequest Req;
				Req.Anchor = Anchor;
				Req.Blueprint = BP;
				Req.ChildEntityIds = MoveTemp(MatchedIds);
				BondRequests.Add(MoveTemp(Req));
			});

			// ─── PASS 2 — execute bonds. Iterator closed. ────────────────
			// Re-gate each request: multiple anchors may have proposed the
			// same child, and the first successful bond will have tagged it
			// FTagMultiblockBonded. Second anchor sees staleness and skips.
			for (FBondRequest& Req : BondRequests)
			{
				if (!Req.Anchor.is_alive() || Req.Anchor.has<FTagMultiblockBonded>())
				{
					continue;
				}

				UE_LOG(LogCrafting, Log,
					TEXT("[MultiblockDetect] MATCH anchor=%llu blueprint=%s children=%d — bonding"),
					(unsigned long long)Req.Anchor.id(),
					*Req.Blueprint->BlueprintId.ToString(),
					Req.ChildEntityIds.Num());

				BondMultiblock(Req.Anchor, Req.Blueprint, Req.ChildEntityIds);
			}
		});
}

// ═══════════════════════════════════════════════════════════════
// BOND TRANSACTION
// ═══════════════════════════════════════════════════════════════
//
// Steps:
//   1. Strip FTagPickupable + FTagItem from every participant. Only the
//      ANCHOR keeps FTagInteractable so the player can open the station UI;
//      children are sealed (R2: damaging a bonded child will orphan the
//      station until Phase 3 deconstruct is implemented).
//   2. Freeze every participant's Barrage body to JPH::EMotionType::Static.
//      v3 PATCH 1 drops PreBondMotionType storage — Phase 3 restore will
//      unconditionally return to Dynamic (requires anchor to have been
//      Dynamic originally — R6).
//   3. Write roster back-refs: anchor gets FMultiblockChildren, each child
//      gets FMultiblockChildOf pointing at the anchor.
//   4. Delegate station setup (slot containers, UI shared state, etc.) to
//      the shared SetupStationInstance helper — identical code path to
//      Phase 1 direct-spawn.

void UFlecsArtillerySubsystem::BondMultiblock(flecs::entity Anchor,
	const UFlecsMultiblockBlueprint* Blueprint,
	const TArray<int64, TInlineAllocator<15>>& ChildEntityIds)
{
	checkf(Anchor.is_valid() && Anchor.is_alive(),
		TEXT("BondMultiblock: invalid anchor"));
	checkf(Blueprint && Blueprint->StationProfile,
		TEXT("BondMultiblock: invalid blueprint"));
	checkf(ChildEntityIds.Num() <= 15,
		TEXT("BondMultiblock: too many children (%d > 15)"), ChildEntityIds.Num());
	check(CachedBarrageDispatch);

	flecs::world World = Anchor.world();

	// ── Step 1: strip tags ───────────────────────────────────────
	auto StripTags = [](flecs::entity E, bool bKeepInteractable)
	{
		E.add<FTagMultiblockBonded>();
		E.remove<FTagPickupable>();
		E.remove<FTagItem>();
		if (!bKeepInteractable)
		{
			E.remove<FTagInteractable>();
		}
	};

	StripTags(Anchor, /*KeepInteractable=*/true);

	for (int64 ChildId : ChildEntityIds)
	{
		flecs::entity C = World.entity(static_cast<flecs::entity_t>(ChildId));
		if (!C.is_alive()) continue;
		StripTags(C, /*KeepInteractable=*/false);
	}

	// ── Step 2: freeze bodies (v3 PATCH 2 key-type fix) ──────────
	// FBarrageBody::BarrageKey is FSkeletonKey; SetBodyMotionType takes
	// FBarrageKey. Canonical bridge: GetShapeRef(SkelKey) → Prim->KeyIntoBarrage.
	auto Freeze = [this](flecs::entity E)
	{
		const FBarrageBody* B = E.try_get<FBarrageBody>();
		if (!B || !B->IsValid()) return;

		FBLet Prim = CachedBarrageDispatch->GetShapeRef(B->BarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim)) return;

		CachedBarrageDispatch->SetBodyMotionType(
			Prim->KeyIntoBarrage,
			JPH::EMotionType::Static,
			/*bActivate=*/false);
	};

	Freeze(Anchor);

	// ── Step 3: write roster + back-refs ─────────────────────────
	const int64 AnchorId = static_cast<int64>(Anchor.id());
	FMultiblockChildren Roster;
	uint8 WrittenCount = 0;

	for (int64 ChildId : ChildEntityIds)
	{
		flecs::entity C = World.entity(static_cast<flecs::entity_t>(ChildId));
		if (!C.is_alive()) continue;

		Freeze(C);

		FMultiblockChildOf BackRef;
		BackRef.AnchorEntityId = AnchorId;
		C.set<FMultiblockChildOf>(BackRef);

		Roster.ChildEntityIds[WrittenCount++] = ChildId;
	}
	Roster.ChildCount = WrittenCount;
	Anchor.set<FMultiblockChildren>(Roster);

	// ── Step 4: upgrade anchor into a crafting station ───────────
	FlecsMultiblockRuntime::SetupStationInstance(Anchor, Blueprint->StationProfile);

	UE_LOG(LogCrafting, Log,
		TEXT("[Multiblock] Bonded %s at anchor=%llu (children=%u)"),
		*Blueprint->BlueprintId.ToString(),
		(unsigned long long)Anchor.id(),
		static_cast<uint32>(Roster.ChildCount));
}
