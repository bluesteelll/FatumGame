// FlecsMultiblockRuntime — SetupStationInstance (Phase 2).
//
// Implementation moved verbatim from FlecsEntitySpawner.cpp's inline crafting
// branch (Phase 1). The only changes vs the original inline block:
//   1. Wrapped in a free function; uses Anchor.world() instead of a spawner
//      member FlecsWorld* to create slot entities.
//   2. checkf precondition: !Anchor.has<FTagCraftingStation>() (v3 PATCH 5 /
//      critique m1). Caller is expected to gate; this traps misuse.
//   3. Log prefix [SetupStationInstance] to disambiguate vs the old
//      [SpawnEntity] log line.
//
// Behaviour is otherwise identical — Phase 1 regression tests must still pass
// after the spawner callsite is updated to call this helper.

#include "Library/FlecsMultiblockRuntime.h"

#include "Async/Async.h"
#include "flecs.h"

#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "EPhysicsLayer.h"
#include "IsolatedJoltIncludes.h"  // JPH::EMotionType
#include "FlecsArtillerySubsystem.h"
#include "FlecsBarrageComponents.h"
#include "FlecsContainerProfile.h"
#include "FlecsContainerLibrary.h"
#include "FlecsCraftingLog.h"
#include "FlecsCraftingRuntime.h"
#include "FlecsCraftingStationProfile.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingUISubsystem.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsEntitySpawner.h"   // FEntitySpawnRequest, UFlecsEntityLibrary::SpawnEntity
#include "FlecsItemComponents.h"
#include "FlecsItemDefinition.h"
#include "FlecsGameTags.h"
#include "FlecsMultiblockBlueprint.h"
#include "Components/FlecsCraftingComponents.h"
#include "Components/FlecsMultiblockComponents.h"
#include "Engine/World.h"
#include "SkeletonTypes.h"

namespace FlecsMultiblockRuntime
{

flecs::entity SpawnSlotContainerFromProfile(
	flecs::entity StationE,
	const UFlecsContainerProfile* SlotProfile,
	ESlotRole Role)
{
	checkf(StationE.is_valid() && StationE.is_alive(),
		TEXT("SpawnSlotContainerFromProfile: invalid StationE"));
	checkf(SlotProfile,
		TEXT("SpawnSlotContainerFromProfile: SlotProfile is null"));

	flecs::world World = StationE.world();
	const int64 StationEntityId = static_cast<int64>(StationE.id());

	// Pure container entity — no physics, no render, no prefab.
	flecs::entity SlotEntity = World.entity();

	FContainerStatic SlotStatic = FContainerStatic::FromProfile(SlotProfile);
	FContainerInstance SlotInst;
	SlotInst.CurrentWeight = 0.f;
	SlotInst.CurrentCount = 0;
	SlotInst.OwnerEntityId = StationEntityId;

	SlotEntity.set<FContainerStatic>(SlotStatic);
	SlotEntity.set<FContainerInstance>(SlotInst);
	SlotEntity.add<FTagContainer>();

	// Type-specific instance components (mirror the main container branch).
	switch (SlotStatic.Type)
	{
	case EContainerType::Grid:
		{
			FContainerGridInstance GridInst;
			GridInst.Initialize(SlotStatic.GridWidth, SlotStatic.GridHeight);
			SlotEntity.set<FContainerGridInstance>(GridInst);
		}
		break;
	case EContainerType::Slot:
		{
			FContainerSlotsInstance SlotsInst;
			SlotEntity.set<FContainerSlotsInstance>(SlotsInst);
		}
		break;
	case EContainerType::List:
		break;
	}

	return SlotEntity;
}

} // namespace FlecsMultiblockRuntime

void FlecsMultiblockRuntime::SetupStationInstance(
	flecs::entity Anchor,
	const UFlecsCraftingStationProfile* Profile)
{
	check(Anchor.is_valid() && Anchor.is_alive());
	check(Profile);
	checkf(!Anchor.has<FTagCraftingStation>(),
		TEXT("SetupStationInstance: Anchor entity=%llu already a crafting station — caller must gate."),
		(unsigned long long)Anchor.id());

	const int64 StationEntityId = static_cast<int64>(Anchor.id());

	// Phase 2/4 — for multiblock-bonded anchors the prefab does NOT inherit
	// FCraftingStationStatic / FTagCraftingStation (anchor parts cannot have
	// CraftingStationProfile per IsDataValid). For Phase 1 direct-spawn entities
	// these are inherited via prefab is_a() — set<>() is idempotent in that case.
	if (!Anchor.has<FCraftingStationStatic>())
	{
		Anchor.set<FCraftingStationStatic>(FCraftingStationStatic::FromProfile(Profile));
	}
	if (!Anchor.has<FTagCraftingStation>())
	{
		Anchor.add<FTagCraftingStation>();
	}

	FCraftingSlots SlotsComp;
	FFuelSlot FuelSlotComp;
	int32 SlotIdx = 0;

	for (const FSlotLayoutDef& SlotDef : Profile->SlotLayout)
	{
		if (!ensureMsgf(SlotIdx < kMaxCraftingSlots,
			TEXT("SetupStationInstance: station '%s' SlotLayout exceeds kMaxCraftingSlots (%d)"),
			*Profile->GetName(), kMaxCraftingSlots))
		{
			break;
		}
		// Skip slots missing ContainerProfile (authoring-in-progress) — log + continue.
		if (!SlotDef.ContainerProfile)
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("SetupStationInstance: station '%s' slot %d (role=%u) has null ContainerProfile — skipping (SlotLayout index will still advance)"),
				*Profile->GetName(), SlotIdx, static_cast<uint32>(SlotDef.Role));
			++SlotIdx;
			continue;
		}

		// Spawn the container entity for this slot.
		flecs::entity SlotEntity = FlecsMultiblockRuntime::SpawnSlotContainerFromProfile(
			Anchor, SlotDef.ContainerProfile, SlotDef.Role);

		// Back-reference so FContainedIn observer can fast-gate to the station.
		FCraftingSlotBackRef BackRef;
		BackRef.StationEntityId = StationEntityId;
		BackRef.SlotIndex = static_cast<uint16>(SlotIdx);
		BackRef.Role = static_cast<uint8>(SlotDef.Role);
		BackRef.OwningPortIndex = 0xFF;  // baseline — not an extension port slot
		SlotEntity.set<FCraftingSlotBackRef>(BackRef);

		const int64 SlotEntityId = static_cast<int64>(SlotEntity.id());
		SlotsComp.SlotEntityIds[SlotIdx] = SlotEntityId;

		const uint8 RoleIdx = static_cast<uint8>(SlotDef.Role);
		if (RoleIdx < static_cast<uint8>(ESlotRole::MAX))
		{
			++SlotsComp.SlotRoleCounts[RoleIdx];
		}

		if (SlotDef.Role == ESlotRole::Fuel)
		{
			FuelSlotComp.FuelSlotEntityId = SlotEntityId;
		}

		++SlotIdx;
	}

	Anchor.set<FCraftingSlots>(SlotsComp);

	// Fuel slot denormalization — only set when the station actually has a Fuel slot
	// (FromProfile's checkf already enforces <= 1 Fuel slot).
	if (FuelSlotComp.FuelSlotEntityId != 0)
	{
		Anchor.set<FFuelSlot>(FuelSlotComp);
	}

	FCraftingStationInstance StationInst;
	StationInst.bSnapshotDirty = true;  // Initial snapshot published on first flush tick.
	Anchor.set<FCraftingStationInstance>(StationInst);

	// Phase 3 — Smelter-specific instance state. Only attached when the station is a Smelter.
	// (Press / Forge will branch off here in later phases.) Default-constructed = Idle, all zeros.
	if (Profile->StationType == ECraftingStationType::Smelter)
	{
		FSmelterInstance Sm;
		Anchor.set<FSmelterInstance>(Sm);

		UE_LOG(LogCrafting, Log,
			TEXT("[SetupStationInstance] Smelter instance attached to station '%s' entity=%llu"),
			*Profile->StationName.ToString(),
			(unsigned long long)Anchor.id());
	}

	// Register shared state with the UI subsystem (game thread only).
	// Key = FSkeletonKey wrapping the Flecs entity id — matches the lookup key used by
	// CraftingSnapshotFlushSystem when publishing.
	const FSkeletonKey StationKey(static_cast<uint64>(StationEntityId));
	AsyncTask(ENamedThreads::GameThread, [StationKey]()
	{
		if (UFlecsCraftingUISubsystem* UISub = UFlecsCraftingUISubsystem::SelfPtr)
		{
			UISub->CreateSharedState(StationKey);
		}
	});

	UE_LOG(LogCrafting, Log,
		TEXT("[SetupStationInstance] Crafting station set up: profile=%s entity=%llu slots=%d fuelSlot=%lld"),
		*Profile->GetName(), (unsigned long long)Anchor.id(), SlotIdx, FuelSlotComp.FuelSlotEntityId);

	// ─── Phase 4 — extensions + effective layout (multiblock stations only) ────
	if (Anchor.has<FMultiblockChildren>())
	{
		const UFlecsMultiblockBlueprint* BP = FlecsMultiblockRuntime::ResolveBlueprintForStation(Anchor);
		if (BP && BP->ExtensionPorts.Num() > 0)
		{
			FMultiblockExtensions Ext;
			Ext.PortCount = static_cast<uint8>(FMath::Min<int32>(BP->ExtensionPorts.Num(), 8));
			Anchor.set<FMultiblockExtensions>(Ext);
		}
		// Compute initial effective layout (also installs FStationEffectiveLayout).
		// MEDIUM #3 — INVARIANT: at this point the bond pipeline (BondMultiblock) has
		// fully populated FMultiblockChildren with ALL required children alive — caller
		// is responsible. If any required child died between bond and this call (e.g.
		// physics body destruct mid-step), RecomputeEffectiveLayout will GC stale ids,
		// flag MissingBitmask, and this freshly-spawned station will go straight to
		// Disabled. That's actually CORRECT — designer shouldn't see a "fully-bonded"
		// station that's missing parts. Document so future maintainers don't add a
		// "force Idle on first setup" hack here.
		FlecsMultiblockRuntime::RecomputeEffectiveLayout(Anchor);
	}
}

// ═══════════════════════════════════════════════════════════════
// PHASE 4 — MODULAR STATIONS IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

namespace FlecsMultiblockRuntime
{

const UFlecsMultiblockBlueprint* ResolveBlueprintForStation(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return nullptr;
	// Anchor entity itself carries FMultiblockPartStatic via prefab inheritance
	// (set on the prefab in GetOrCreateEntityPrefab when MultiblockBlueprint != null).
	const FMultiblockPartStatic* PS = StationE.try_get<FMultiblockPartStatic>();
	if (!PS) return nullptr;
	return PS->Blueprint;
}

bool ReadStationWorldTransform(flecs::entity StationE, FVector& OutPos, FQuat& OutQuat)
{
	OutPos = FVector::ZeroVector;
	OutQuat = FQuat::Identity;

	if (!StationE.is_valid() || !StationE.is_alive()) return false;
	const FBarrageBody* Body = StationE.try_get<FBarrageBody>();
	if (!Body || !Body->IsValid()) return false;

	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
	if (!Sub) return false;
	UBarrageDispatch* Barrage = Sub->GetBarrageDispatch();
	if (!Barrage) return false;

	FBLet Prim = Barrage->GetShapeRef(Body->BarrageKey);
	if (!FBarragePrimitive::IsNotNull(Prim)) return false;

	const FVector3f PosF = FBarragePrimitive::GetPosition(Prim);
	OutPos = FVector(PosF.X, PosF.Y, PosF.Z);
	OutQuat = FQuat(FBarragePrimitive::OptimisticGetAbsoluteRotation(Prim));
	return true;
}

void RestorePartToDynamic(flecs::entity ChildE)
{
	if (!ChildE.is_valid() || !ChildE.is_alive()) return;

	const FBarrageBody* Body = ChildE.try_get<FBarrageBody>();
	if (!Body || !Body->IsValid())
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] RestorePartToDynamic: entity=%llu has no Barrage body"),
			(unsigned long long)ChildE.id());
		return;
	}

	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
	if (!Sub) return;
	UBarrageDispatch* Barrage = Sub->GetBarrageDispatch();
	if (!Barrage) return;

	FBLet Prim = Barrage->GetShapeRef(Body->BarrageKey);
	if (!FBarragePrimitive::IsNotNull(Prim)) return;

	// Inverse of BondMultiblock Step 2 freeze. Activate so the body wakes up.
	Barrage->SetBodyMotionType(Prim->KeyIntoBarrage, JPH::EMotionType::Dynamic, /*bActivate=*/true);
	// Idempotent layer restore — bond didn't touch layer, but be defensive.
	Barrage->SetBodyObjectLayer(Prim->KeyIntoBarrage, Layers::MOVING);
}

void ApplyDetachImpulse(flecs::entity ChildE, float ImpulseCmS)
{
	if (!ChildE.is_valid() || !ChildE.is_alive()) return;
	if (ImpulseCmS <= 0.f) return;

	const FBarrageBody* Body = ChildE.try_get<FBarrageBody>();
	if (!Body || !Body->IsValid()) return;

	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
	if (!Sub) return;
	UBarrageDispatch* Barrage = Sub->GetBarrageDispatch();
	if (!Barrage) return;

	FBLet Prim = Barrage->GetShapeRef(Body->BarrageKey);
	if (!FBarragePrimitive::IsNotNull(Prim)) return;

	const float MassKg = Barrage->GetBodyMass(Prim->KeyIntoBarrage);
	// Z+ pop (kg·cm/s). Mass=0 (kinematic-just-converted) → fall back to magnitude only.
	const float EffectiveMass = (MassKg > 0.f) ? MassKg : 1.f;
	const FVector ImpulseUE(0.f, 0.f, EffectiveMass * ImpulseCmS);
	Barrage->AddBodyImpulse(Prim->KeyIntoBarrage, ImpulseUE);
}

void DrainAndDestructSlot(flecs::entity StationE, FCraftingSlots* Slots, int32 SlotIdx, uint8 ExpectedPortIndex)
{
	check(Slots);
	if (SlotIdx < 0 || SlotIdx >= kMaxCraftingSlots) return;

	const int64 SlotId = Slots->SlotEntityIds[SlotIdx];
	if (SlotId == 0) return;

	flecs::world W = StationE.world();
	flecs::entity SlotE = W.entity(static_cast<flecs::entity_t>(SlotId));
	if (!SlotE.is_alive())
	{
		Slots->SlotEntityIds[SlotIdx] = 0;
		return;
	}

	if (const FCraftingSlotBackRef* Back = SlotE.try_get<FCraftingSlotBackRef>())
	{
		// V2 PATCH 1 — defense-in-depth identity check.
		checkf(Back->OwningPortIndex == ExpectedPortIndex,
			TEXT("DrainAndDestructSlot: SlotIdx=%d expected port %u but BackRef has %u — would destroy wrong slot's items"),
			SlotIdx, (uint32)ExpectedPortIndex, (uint32)Back->OwningPortIndex);
		if (Back->Role < static_cast<uint8>(ESlotRole::MAX))
		{
			--Slots->SlotRoleCounts[Back->Role];
		}
	}

	// Walk items in slot, collect-then-destruct (don't mutate iterator).
	TArray<flecs::entity, TInlineAllocator<8>> ItemsToDestroy;
	W.each([SlotId, &ItemsToDestroy](flecs::entity ItemE, const FContainedIn& CI)
	{
		if (CI.ContainerEntityId == SlotId)
		{
			ItemsToDestroy.Add(ItemE);
		}
	});
	const int32 ItemCount = ItemsToDestroy.Num();
	for (flecs::entity ItemE : ItemsToDestroy)
	{
		if (ItemE.is_alive())
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("[Modular] DrainAndDestructSlot losing item entity=%llu (slot=%lld port=%u)"),
				(unsigned long long)ItemE.id(), SlotId, (uint32)ExpectedPortIndex);
			ItemE.destruct();
		}
	}

	SlotE.destruct();
	Slots->SlotEntityIds[SlotIdx] = 0;

	if (ItemCount > 0)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] DrainAndDestructSlot: %d item(s) lost in slot %d (port %u)"),
			ItemCount, SlotIdx, (uint32)ExpectedPortIndex);
	}
}

void EnsurePortSlot(
	flecs::entity StationE,
	FCraftingSlots* Slots,
	int32 SlotIdx,
	ESlotRole Role,
	const UFlecsContainerProfile* SlotProfile,
	uint8 PortIndex)
{
	check(Slots);
	checkf(SlotProfile,
		TEXT("EnsurePortSlot: SlotProfile null for port %u role %u — IsDataValid should have caught this"),
		(uint32)PortIndex, (uint32)Role);

	const int64 ExistingId = Slots->SlotEntityIds[SlotIdx];
	if (ExistingId != 0)
	{
		// Validate identity — preserve if alive + same port + same role.
		flecs::world W = StationE.world();
		flecs::entity SlotE = W.entity(static_cast<flecs::entity_t>(ExistingId));
		const FCraftingSlotBackRef* Back = SlotE.try_get<FCraftingSlotBackRef>();
		const bool bIdentityOk = SlotE.is_alive()
			&& Back
			&& Back->Role == static_cast<uint8>(Role)
			&& Back->OwningPortIndex == PortIndex;
		if (bIdentityOk)
		{
			return;  // preserve — same port, same role, alive
		}

		// Identity broken — V2 PATCH 1: this should be extremely rare with
		// port-indexed layout, log loudly + drain.
		UE_LOG(LogCrafting, Error,
			TEXT("[Modular] EnsurePortSlot identity mismatch: SlotIdx=%d ExpectedPort=%u Role=%u — draining"),
			SlotIdx, (uint32)PortIndex, (uint32)Role);
		DrainAndDestructSlot(StationE, Slots, SlotIdx, /*ExpectedPortIndex=*/PortIndex);
	}

	// Spawn new container entity from SlotProfile.
	flecs::entity NewSlot = SpawnSlotContainerFromProfile(StationE, SlotProfile, Role);
	FCraftingSlotBackRef Back;
	Back.StationEntityId = static_cast<int64>(StationE.id());
	Back.SlotIndex       = static_cast<uint16>(SlotIdx);
	Back.Role            = static_cast<uint8>(Role);
	Back.OwningPortIndex = PortIndex;
	NewSlot.set<FCraftingSlotBackRef>(Back);

	Slots->SlotEntityIds[SlotIdx] = static_cast<int64>(NewSlot.id());
	if (static_cast<uint8>(Role) < static_cast<uint8>(ESlotRole::MAX))
	{
		++Slots->SlotRoleCounts[static_cast<uint8>(Role)];
	}
}

namespace
{
	/** File-local — map a PartRole FName to a bit position 0..15. The 4-bit hash
	 *  (0xF mask) GUARANTEES collisions when distinct roles hash to the same nibble:
	 *  with N=16 buckets, two random roles collide with probability 1/16 (~6%). The
	 *  resulting bitmask is therefore NOT a precise "this exact role is missing"
	 *  signal — it's a fuzzy "some part hashing to bit K is missing" hint.
	 *
	 *  CALLER CONTRACT: this function is for the per-station MissingRequiredRolesBitmask
	 *  field, consumed ONLY by the UI hint-colour pipeline ("show red when bitmask
	 *  non-zero"). Gameplay correctness — gating recipes, locking slots, transitioning
	 *  Disabled — uses RoleBit-AGNOSTIC `MissingBitmask != 0` checks elsewhere. The
	 *  individual bit positions are an indicator, not an identifier.
	 *
	 *  MEDIUM #2: if Phase 5+ wants per-role missing-part labels, replace this with a
	 *  TMap<FName, uint8> assigning unique bit indices per blueprint, or store the
	 *  full FName list. Either change requires snapshot-format coordination with UI.
	 */
	uint16 HashRoleToBitIndex(FName Role)
	{
		return static_cast<uint16>(GetTypeHash(Role) & 0xF);
	}
}

void RecomputeEffectiveLayout(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return;
	if (StationE.has<FTagCraftingStationDestroying>()) return;

	const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
	if (!Static || !Static->Profile) return;

	FMultiblockChildren* Roster = StationE.try_get_mut<FMultiblockChildren>();
	if (!Roster) return;  // not a multiblock; trivial single-prefab station — no extensions possible.

	const UFlecsMultiblockBlueprint* BP = ResolveBlueprintForStation(StationE);
	if (!BP) return;

	flecs::world World = StationE.world();

	// ─── Walk required children: GC stale ids + compute MissingBitmask. ───
	uint16 MissingBitmask = 0;
	for (int32 i = 0; i < BP->Children.Num(); ++i)
	{
		FMultiblockChildSlot& Slot = Roster->ChildSlots[i];
		if (Slot.bIsExtension) continue;  // extension ports don't count for "missing required"

		bool bAlive = false;
		if (Slot.ChildEntityId != 0)
		{
			flecs::entity Ch = World.entity(static_cast<flecs::entity_t>(Slot.ChildEntityId));
			bAlive = Ch.is_alive() && !Ch.has<FTagDead>();
			if (!bAlive)
			{
				Slot.ChildEntityId = 0;  // GC stale id
				Slot.bSwappable = 0;
			}
		}
		if (!bAlive)
		{
			const uint16 RoleBit = static_cast<uint16>(1u << HashRoleToBitIndex(Slot.PartRole));
			MissingBitmask |= RoleBit;
		}
	}

	// ─── Walk extension ports: GC stale occupants, count by type. ───
	FMultiblockExtensions* Ext = StationE.try_get_mut<FMultiblockExtensions>();
	uint8 OccupiedCount = 0;
	float FuelCeilingDeltaSec = 0.f;
	if (Ext && BP->ExtensionPorts.Num() > 0)
	{
		const int32 PortN = FMath::Min<int32>(BP->ExtensionPorts.Num(), 8);
		for (int32 p = 0; p < PortN; ++p)
		{
			const int64 Id = Ext->PortOccupants[p];
			if (Id == 0) continue;
			flecs::entity ChE = World.entity(static_cast<flecs::entity_t>(Id));
			if (!ChE.is_alive() || ChE.has<FTagDead>())
			{
				Ext->PortOccupants[p] = 0;
				continue;
			}
			++OccupiedCount;
			if (BP->ExtensionPorts[p].PortType == EExtensionPortType::FuelTank)
			{
				FuelCeilingDeltaSec += 60.f;
			}
		}
	}

	// ─── V2 PATCH 1 — Effective slot rebuild: port-indexed, NOT contiguous-packed. ───
	// Layout:
	//   [0, BaselineCount)                       baseline slots (never resized by extensions)
	//   [BaselineCount, BaselineCount + PortN)   one slot per extension port, vacant if port empty
	FCraftingSlots* Slots = StationE.try_get_mut<FCraftingSlots>();
	if (!Slots) return;

	const int32 BaselineCount = Static->Profile->SlotLayout.Num();
	const int32 PortN = (Ext && BP) ? FMath::Min<int32>(BP->ExtensionPorts.Num(), 8) : 0;
	const int32 EffectiveCount = BaselineCount + PortN;
	checkf(EffectiveCount <= kMaxCraftingSlots,
		TEXT("RecomputeEffectiveLayout: '%s' EffectiveCount=%d > kMaxCraftingSlots=%d (Profile=%d Ports=%d)"),
		*Static->StationName.ToString(), EffectiveCount, kMaxCraftingSlots, BaselineCount, PortN);

	// Walk ports — for each, ensure its dedicated slot exists IFF port is occupied,
	// else destruct the slot if it exists.
	for (int32 p = 0; p < PortN; ++p)
	{
		const int32 SlotIdx = BaselineCount + p;
		const FMultiblockExtensionPort& Port = BP->ExtensionPorts[p];
		const bool bPortOccupied = (Ext && Ext->PortOccupants[p] != 0);

		ESlotRole DesiredRole = ESlotRole::MaterialInput;  // dummy default
		const UFlecsContainerProfile* SlotProfile = nullptr;
		bool bSlotContributing = true;
		switch (Port.PortType)
		{
		case EExtensionPortType::DieSlot:
			DesiredRole = ESlotRole::Die;
			SlotProfile = Static->Profile->ExtensionDieSlotProfile;
			break;
		case EExtensionPortType::OutputTray:
			DesiredRole = ESlotRole::Output;
			SlotProfile = Static->Profile->ExtensionOutputSlotProfile;
			break;
		case EExtensionPortType::ToolRack:
			DesiredRole = ESlotRole::Tool;
			SlotProfile = Static->Profile->ExtensionToolSlotProfile;
			break;
		case EExtensionPortType::FuelTank:
		case EExtensionPortType::Generic:
		default:
			bSlotContributing = false;
			if (Slots->SlotEntityIds[SlotIdx] != 0)
			{
				UE_LOG(LogCrafting, Error,
					TEXT("[Modular] '%s' port %d type=%u has non-zero slot id %lld — corrupt"),
					*Static->StationName.ToString(), p, (uint32)Port.PortType,
					Slots->SlotEntityIds[SlotIdx]);
			}
			break;
		}

		if (!bSlotContributing) continue;

		if (bPortOccupied)
		{
			EnsurePortSlot(StationE, Slots, SlotIdx, DesiredRole, SlotProfile, /*PortIndex=*/static_cast<uint8>(p));
		}
		else if (Slots->SlotEntityIds[SlotIdx] != 0)
		{
			DrainAndDestructSlot(StationE, Slots, SlotIdx, /*ExpectedPortIndex=*/static_cast<uint8>(p));
		}
	}

	// Update derived state.
	FStationEffectiveLayout Layout;
	Layout.EffectiveSlotCount    = static_cast<uint8>(EffectiveCount);
	Layout.EffectiveFuelCeiling  = 90.f + FuelCeilingDeltaSec;  // baseline 90s — Phase 1 default
	Layout.ExtensionPortsOccupiedCount = OccupiedCount;
	Layout.MissingRequiredRolesBitmask = MissingBitmask;
	StationE.set<FStationEffectiveLayout>(Layout);

	// ─── V2 PATCH 2 — Phase transition: any phase → Disabled (when missing) ↔ Idle (when restored). ───
	FSmelterInstance* Sm = StationE.try_get_mut<FSmelterInstance>();
	if (Sm)
	{
		const bool bShouldBeDisabled = (MissingBitmask != 0);

		if (bShouldBeDisabled && Sm->Phase != EProcessPhase::Disabled)
		{
			// Active processing → must refund ledger before going Disabled.
			const bool bWasActive = (Sm->Phase == EProcessPhase::Processing
								  || Sm->Phase == EProcessPhase::Stalled
								  || Sm->Phase == EProcessPhase::Completing);
			if (bWasActive)
			{
				UE_LOG(LogCrafting, Warning,
					TEXT("[Modular] '%s' force-cancel due to missing required functional (Phase=%u, MissingMask=0x%04x)"),
					*Static->StationName.ToString(), (uint32)Sm->Phase, MissingBitmask);

				FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>();
				checkf(Inst, TEXT("RecomputeEffectiveLayout: station has FSmelterInstance but no FCraftingStationInstance"));

				if (Sm->Phase == EProcessPhase::Completing)
				{
					// V2 PATCH 2b — Completing-phase guard. Output already produced; refunding
					// just-consumed ingredients would double-credit. Discard ledger silently.
					UE_LOG(LogCrafting, Error,
						TEXT("[Modular] '%s' required functional died during Completing phase — output already produced, ledger discarded WITHOUT refund (would double-credit)"),
						*Static->StationName.ToString());
					Sm->ConsumedLedger.Reset();
					Sm->Phase = EProcessPhase::Cancelled;

					// HIGH #3 — INVARIANT: lock release is a pre-condition for any Phase != Idle
					// exit. SmelterCancel handles this for the Processing/Stalled branch above
					// (releases MaterialInputAndFuel). For the Completing branch we must explicitly
					// release ALL locks before TransitionStationToDisabled. We don't rely on
					// TransitionStationToDisabled's defensive UnlockStationSlots(All) call —
					// future refactor may remove it, leaving orphaned locks here.
					FlecsCraftingRuntime::UnlockStationSlots(StationE, *Slots, FlecsCraftingRuntime::ESlotRoleLockMask::All);
				}
				else
				{
					FlecsCraftingRuntime::SmelterCancel(StationE, *Static, *Inst, *Sm, *Slots);
				}
			}
			FlecsCraftingRuntime::TransitionStationToDisabled(StationE);
		}
		else if (!bShouldBeDisabled && Sm->Phase == EProcessPhase::Disabled)
		{
			FlecsCraftingRuntime::TransitionStationToIdle(StationE);
		}
	}
	else if (MissingBitmask != 0
		 && Static->StationType != ECraftingStationType::Generic
		 && Static->StationType != ECraftingStationType::None)
	{
		// HIGH #5 — Forward-compat hot-fix path. Phase 4 only wires Disabled-transition for
		// FSmelterInstance. Future Press / Forge / Alchemy types must wire equivalent
		// `*Instance::Phase` storage and a transition branch above. This ensureMsgf forces
		// any future maintainer to notice when a non-Generic station is left in a "missing
		// required functional" state without proper Disabled handling.
		//
		// TODO(crafting/phase5+) — generalize Disabled transition: introduce
		// `EProcessPhase Phase` on FCraftingStationInstance itself (shared by all station
		// types) so this branch handles all stations uniformly. Until then the per-type
		// cast above is the source of truth, and this guard keeps us honest.
		ensureMsgf(false,
			TEXT("RecomputeEffectiveLayout: station '%s' StationType=%u has MissingMask=0x%04x but no FSmelterInstance — Disabled transition NOT wired for this type. Future refactor required."),
			*Static->StationName.ToString(),
			static_cast<uint32>(Static->StationType),
			MissingBitmask);
	}

	// Dirty snapshot so widgets see the new MissingPartsBitmask + ExtensionPortsOccupied.
	if (FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>())
	{
		Inst->bSnapshotDirty = true;
	}

	UE_LOG(LogCrafting, Log,
		TEXT("[Modular] '%s' RecomputeEffectiveLayout: SlotCount=%d FuelCeilingDelta=+%.1fs OccupiedPorts=%u MissingMask=0x%04x"),
		*Static->StationName.ToString(), EffectiveCount, FuelCeilingDeltaSec, (uint32)OccupiedCount, MissingBitmask);
}

// ═══════════════════════════════════════════════════════════════
// PHASE 4 — ATTACH / DETACH / DECONSTRUCT
// ═══════════════════════════════════════════════════════════════

namespace
{
	/** HIGH #2 — Port-type vs part-component validation. Returns false (and logs) when
	 *  the spawned-via-EntityDefinition part will not satisfy the port's runtime contract.
	 *
	 *  This guard is INTENTIONALLY pre-spawn. We inspect the candidate part item entity
	 *  (the inventory copy) — its prefab inheritance carries the same components the
	 *  to-be-spawned attached child will inherit. PartRole role-list filtering already
	 *  happens upstream; this guard catches type-vs-port mismatches the role list cannot
	 *  express (e.g. a part with the right PartRole but missing FCraftingFuelItemData).
	 */
	bool ValidatePartComponentsForPort(
		flecs::entity PartItem,
		EExtensionPortType PortType,
		const FName& PartRole)
	{
		switch (PortType)
		{
		case EExtensionPortType::FuelTank:
			if (!PartItem.has<FCraftingFuelItemData>())
			{
				UE_LOG(LogCrafting, Warning,
					TEXT("[Modular] AttachPart rejected — FuelTank port requires part with FCraftingFuelItemData (role='%s' has none)"),
					*PartRole.ToString());
				return false;
			}
			return true;

		case EExtensionPortType::DieSlot:
		case EExtensionPortType::OutputTray:
		case EExtensionPortType::ToolRack:
			// These ports rely on FMultiblockPartStatic for slot identity. Any multiblock
			// part has it via prefab inheritance — but we re-check defensively so that
			// orphan (non-multiblock) inventory items can't pass.
			if (!PartItem.has<FMultiblockPartStatic>())
			{
				UE_LOG(LogCrafting, Warning,
					TEXT("[Modular] AttachPart rejected — port type %u requires FMultiblockPartStatic (role='%s' has none)"),
					static_cast<uint32>(PortType), *PartRole.ToString());
				return false;
			}
			return true;

		case EExtensionPortType::Generic:
			// Designer-defined effect — no required components. AcceptedPartRoles list
			// is the only filter.
			return true;

		case EExtensionPortType::None:
			// Sentinel — never used in production. Reject defensively.
			UE_LOG(LogCrafting, Error,
				TEXT("[Modular] AttachPart rejected — port type None is sentinel-only"));
			return false;

		default:
			UE_LOG(LogCrafting, Warning,
				TEXT("[Modular] AttachPart rejected — unknown port type %u"),
				static_cast<uint32>(PortType));
			return false;
		}
	}
}

bool AttachPartToStation(flecs::entity PartItem, flecs::entity StationE, int32 PortIndex,
	TWeakObjectPtr<UWorld> WeakWorld)
{
	check(PartItem.is_valid() && PartItem.is_alive());
	check(StationE.is_valid() && StationE.is_alive());

	if (StationE.has<FTagCraftingStationDestroying>())
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — station entity=%llu mid-teardown"),
			(unsigned long long)StationE.id());
		return false;
	}

	// V2 PATCH 3 — idempotency guard. Reject if either the part item OR the station
	// has an in-flight attach reservation. The HIGH #1 timeout (60 sim ticks ≈ 1s) is
	// the backstop; FinalizePartAttach re-checks PortOccupants for the contract.
	if (PartItem.has<FPendingPartAttach>())
	{
		UE_LOG(LogCrafting, Verbose,
			TEXT("[Modular] AttachPart rejected — part item entity=%llu already has FPendingPartAttach (in-flight)"),
			(unsigned long long)PartItem.id());
		return false;
	}
	if (StationE.has<FPendingStationAttach>())
	{
		const FPendingStationAttach* P = StationE.try_get<FPendingStationAttach>();
		UE_LOG(LogCrafting, Verbose,
			TEXT("[Modular] AttachPart rejected — station entity=%llu already has in-flight attach for port %d"),
			(unsigned long long)StationE.id(), P ? P->ReservedPortIndex : -1);
		return false;
	}

	const FSmelterInstance* Sm = StationE.try_get<FSmelterInstance>();
	const bool bStationIdleOrDisabled = !Sm
		|| Sm->Phase == EProcessPhase::Idle
		|| Sm->Phase == EProcessPhase::Disabled;
	if (!bStationIdleOrDisabled)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — station entity=%llu phase=%u not Idle/Disabled"),
			(unsigned long long)StationE.id(),
			Sm ? static_cast<uint32>(Sm->Phase) : 0u);
		return false;
	}

	const UFlecsMultiblockBlueprint* BP = ResolveBlueprintForStation(StationE);
	if (!BP || !BP->ExtensionPorts.IsValidIndex(PortIndex))
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — invalid port index %d (BP ports=%d)"),
			PortIndex, BP ? BP->ExtensionPorts.Num() : 0);
		return false;
	}

	const FMultiblockExtensionPort& Port = BP->ExtensionPorts[PortIndex];

	// Validate the part's PartRole matches port acceptance.
	const FEntityDefinitionRef* DefRef = PartItem.try_get<FEntityDefinitionRef>();
	if (!DefRef || !DefRef->Definition)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — part entity=%llu has no EntityDefinition"),
			(unsigned long long)PartItem.id());
		return false;
	}
	const FName PartRole = DefRef->Definition->MultiblockPartRole;
	if (Port.AcceptedPartRoles.Num() > 0 && !Port.AcceptedPartRoles.Contains(PartRole))
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — port '%s' does not accept role '%s'"),
			*Port.PortId.ToString(), *PartRole.ToString());
		return false;
	}

	// HIGH #2 — port-type vs part-component check. Catches mismatches the role list
	// cannot express (e.g. FuelTank with non-fuel part of the right role).
	if (!ValidatePartComponentsForPort(PartItem, Port.PortType, PartRole))
	{
		return false;
	}

	// Stack cap check — count occupants of same PortType.
	FMultiblockExtensions* Ext = StationE.try_get_mut<FMultiblockExtensions>();
	if (!Ext)
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Modular] AttachPart rejected — station has no FMultiblockExtensions component"));
		return false;
	}
	int32 SamePortTypeOccupied = 0;
	const int32 PortN = FMath::Min<int32>(BP->ExtensionPorts.Num(), 8);
	for (int32 p = 0; p < PortN; ++p)
	{
		if (BP->ExtensionPorts[p].PortType == Port.PortType && Ext->PortOccupants[p] != 0)
			++SamePortTypeOccupied;
	}
	if (SamePortTypeOccupied >= Port.MaxStackedAtThisPort)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — port type %u at MaxStacked %u"),
			static_cast<uint32>(Port.PortType), Port.MaxStackedAtThisPort);
		return false;
	}
	if (Ext->PortOccupants[PortIndex] != 0)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — port %d already occupied by %lld"),
			PortIndex, Ext->PortOccupants[PortIndex]);
		return false;
	}

	// Compute world pose using snapped yaw from roster.
	FMultiblockChildren* Roster = StationE.try_get_mut<FMultiblockChildren>();
	check(Roster);
	FVector AnchorPos;
	FQuat AnchorQuat;
	if (!ReadStationWorldTransform(StationE, AnchorPos, AnchorQuat))
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] AttachPart rejected — anchor entity=%llu has no Barrage body"),
			(unsigned long long)StationE.id());
		return false;
	}
	const FQuat SnappedQuat(FRotator(0.f, static_cast<float>(Roster->AnchorYawSnappedDeg), 0.f).Quaternion());
	const FVector PortWorldPos = AnchorPos + SnappedQuat.RotateVector(Port.RelativeOffset);

	// Reserve part item + station against player-mutation race (V2 PATCH 3).
	// HIGH #4 — `flecs::world W = ...` (by-value copy, lightweight handle) for consistency.
	flecs::world W = StationE.world();
	const uint64 NowTick = W.get_info()->frame_count_total;

	FPendingPartAttach Pending;
	Pending.TargetStationEntityId = static_cast<int64>(StationE.id());
	Pending.EnqueuedTickStamp     = NowTick;
	PartItem.set<FPendingPartAttach>(Pending);

	FPendingStationAttach StationPending;
	StationPending.ReservedPortIndex = PortIndex;
	StationPending.EnqueuedTickStamp = NowTick;
	StationE.set<FPendingStationAttach>(StationPending);

	// Spawn a NEW child entity at the port pose using the part's EntityDefinition.
	// Mirrors the bond branch — but skipping the candidate scan.
	UFlecsEntityDefinition* PartDef = DefRef->Definition;
	FEntitySpawnRequest Req = FEntitySpawnRequest::FromDefinition(PartDef, PortWorldPos);
	Req.bPickupable = false;  // attached part is bonded, not pickupable

	// CRITICAL #1 — WeakWorld is captured by the GAME-THREAD BP entry, NOT taken via
	// UFlecsArtillerySubsystem::GetWorld() here on sim thread (UObjectArray race). If
	// the caller failed to capture (shouldn't happen — checkf upstream), bail.
	if (!WeakWorld.IsValid())
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Modular] AttachPart: WeakWorld stale at sim entry — clearing reservations and aborting"));
		PartItem.remove<FPendingPartAttach>();
		StationE.remove<FPendingStationAttach>();
		return false;
	}

	const FSkeletonKey StationKey(static_cast<uint64>(StationE.id()));
	const int32 PortIdxCopy = PortIndex;
	const int64 OldItemId = static_cast<int64>(PartItem.id());

	AsyncTask(ENamedThreads::GameThread, [WeakWorld, Req, StationKey, PortIdxCopy, OldItemId]()
	{
		UWorld* W2 = WeakWorld.Get();
		if (!W2)
		{
			// World destroyed mid-spawn — let stale-reservation cleanup handle the rollback.
			return;
		}
		FSkeletonKey NewChildKey = UFlecsEntityLibrary::SpawnEntity(W2, Req);
		// Now back to sim — bond the spawned child.
		if (UFlecsArtillerySubsystem* InnerSub = UFlecsArtillerySubsystem::SelfPtr)
		{
			InnerSub->EnqueueCommand([StationKey, PortIdxCopy, NewChildKey, OldItemId]()
			{
				FlecsMultiblockRuntime::FinalizePartAttach(StationKey, PortIdxCopy, NewChildKey, OldItemId);
			});
		}
	});

	UE_LOG(LogCrafting, Log,
		TEXT("[Modular] AttachPart RESERVED: station=%llu port=%d part-item=%lld"),
		(unsigned long long)StationE.id(), PortIndex, OldItemId);

	return true;
}

void FinalizePartAttach(FSkeletonKey StationKey, int32 PortIndex, FSkeletonKey NewChildKey, int64 OldItemId)
{
	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
	if (!Sub) return;

	flecs::world* WorldPtr = Sub->GetFlecsWorld();
	if (!WorldPtr) return;
	// HIGH #4 — `flecs::world W = ...` (by-value copy, lightweight handle) for consistency.
	flecs::world W = *WorldPtr;

	flecs::entity StationE = Sub->GetEntityForBarrageKey(StationKey);
	flecs::entity NewChild = Sub->GetEntityForBarrageKey(NewChildKey);
	if (!StationE.is_alive())
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] FinalizePartAttach: station no longer alive (port=%d) — orphaning new child"),
			PortIndex);
		// Best-effort cleanup of the player-side item; the spawned child is left as a normal pickup.
		flecs::entity OldItem = W.entity(static_cast<flecs::entity_t>(OldItemId));
		if (OldItem.is_alive() && OldItem.has<FPendingPartAttach>())
		{
			OldItem.remove<FPendingPartAttach>();
		}
		return;
	}
	if (!NewChild.is_alive())
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] FinalizePartAttach: new child no longer alive (station=%llu port=%d) — clearing reservations"),
			(unsigned long long)StationE.id(), PortIndex);
		StationE.remove<FPendingStationAttach>();
		flecs::entity OldItem = W.entity(static_cast<flecs::entity_t>(OldItemId));
		if (OldItem.is_alive() && OldItem.has<FPendingPartAttach>())
		{
			OldItem.remove<FPendingPartAttach>();
		}
		return;
	}

	// CRITICAL #3 — invariants on the spawned child + port-occupancy guard.
	// AttachPartToStation gated the EntityDefinition pre-spawn, but the prefab inheritance
	// must actually have produced FMultiblockPartStatic and a role accepted by this port.
	// If any invariant fails, rollback (destruct NewChild) — never blind-bond unknown data.
	const UFlecsMultiblockBlueprint* BP = ResolveBlueprintForStation(StationE);
	if (!BP || !BP->ExtensionPorts.IsValidIndex(PortIndex))
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Modular] FinalizePartAttach: invalid port %d on station=%llu — rolling back"),
			PortIndex, (unsigned long long)StationE.id());
		NewChild.destruct();
		StationE.remove<FPendingStationAttach>();
		flecs::entity OldItem = W.entity(static_cast<flecs::entity_t>(OldItemId));
		if (OldItem.is_alive() && OldItem.has<FPendingPartAttach>())
		{
			OldItem.remove<FPendingPartAttach>();
		}
		return;
	}

	// Invariant 1: spawned entity carries FMultiblockPartStatic.
	checkf(NewChild.has<FMultiblockPartStatic>(),
		TEXT("FinalizePartAttach: spawned NewChild=%llu has no FMultiblockPartStatic — prefab not authored as a multiblock part"),
		(unsigned long long)NewChild.id());

	// Invariant 2: spawned PartRole is in port's AcceptedPartRoles (if list non-empty).
	const FMultiblockPartStatic* PS = NewChild.try_get<FMultiblockPartStatic>();
	checkf(PS, TEXT("FinalizePartAttach: try_get<FMultiblockPartStatic> failed despite has<>"));
	const TArray<FName>& Accepted = BP->ExtensionPorts[PortIndex].AcceptedPartRoles;
	checkf(Accepted.IsEmpty() || Accepted.Contains(PS->PartRole),
		TEXT("FinalizePartAttach: spawned PartRole='%s' not accepted by port %d (port='%s')"),
		*PS->PartRole.ToString(), PortIndex, *BP->ExtensionPorts[PortIndex].PortId.ToString());

	// Invariant 3 (HIGH #1 timeout-race protection): port must still be vacant at finalize.
	// If the 60-tick reservation timeout fired during AsyncTask flight and another attach
	// passed AttachPartToStation's `PortOccupants[PortIndex] != 0` gate, this guard catches
	// the corruption. Rollback: destruct NewChild (unbonded), keep existing occupant.
	FMultiblockExtensions* Ext = StationE.try_get_mut<FMultiblockExtensions>();
	if (!Ext)
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Modular] FinalizePartAttach: station=%llu lost FMultiblockExtensions mid-flight — rolling back"),
			(unsigned long long)StationE.id());
		NewChild.destruct();
		StationE.remove<FPendingStationAttach>();
		flecs::entity OldItem = W.entity(static_cast<flecs::entity_t>(OldItemId));
		if (OldItem.is_alive() && OldItem.has<FPendingPartAttach>())
		{
			OldItem.remove<FPendingPartAttach>();
		}
		return;
	}
	if (Ext->PortOccupants[PortIndex] != 0)
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Modular] FinalizePartAttach: TIMEOUT-RACE — port %d on station=%llu occupied by %lld during AsyncTask flight (reservation timeout fired). Rolling back NewChild=%llu."),
			PortIndex, (unsigned long long)StationE.id(),
			Ext->PortOccupants[PortIndex], (unsigned long long)NewChild.id());
		NewChild.destruct();
		StationE.remove<FPendingStationAttach>();
		flecs::entity OldItem = W.entity(static_cast<flecs::entity_t>(OldItemId));
		if (OldItem.is_alive() && OldItem.has<FPendingPartAttach>())
		{
			OldItem.remove<FPendingPartAttach>();
		}
		return;
	}

	// Bond the new child: freeze body + tags + back-ref.
	UBarrageDispatch* Barrage = Sub->GetBarrageDispatch();
	if (Barrage)
	{
		const FBarrageBody* B = NewChild.try_get<FBarrageBody>();
		if (B && B->IsValid())
		{
			FBLet Prim = Barrage->GetShapeRef(B->BarrageKey);
			if (FBarragePrimitive::IsNotNull(Prim))
			{
				Barrage->SetBodyMotionType(Prim->KeyIntoBarrage, JPH::EMotionType::Static, /*bActivate=*/false);
			}
		}
	}
	NewChild.add<FTagMultiblockBonded>();
	NewChild.remove<FTagPickupable>();
	NewChild.remove<FTagItem>();
	NewChild.remove<FTagInteractable>();

	FMultiblockChildOf BackRef;
	BackRef.AnchorEntityId = static_cast<int64>(StationE.id());
	NewChild.set<FMultiblockChildOf>(BackRef);

	// Update roster — append a row for this extension.
	FMultiblockChildren* Roster = StationE.try_get_mut<FMultiblockChildren>();
	check(Roster);
	int32 InsertIdx = INDEX_NONE;
	for (int32 i = 0; i < 15; ++i)
	{
		if (Roster->ChildSlots[i].ChildEntityId == 0)
		{
			InsertIdx = i;
			break;
		}
	}
	checkf(InsertIdx != INDEX_NONE,
		TEXT("FinalizePartAttach: roster full — should be impossible (15-cap, 8 ports)"));

	FMultiblockChildSlot& Row = Roster->ChildSlots[InsertIdx];
	Row.ChildEntityId = static_cast<int64>(NewChild.id());
	Row.bIsExtension  = 1;
	Row.PortIndex     = static_cast<uint8>(PortIndex);
	Row.bSwappable    = 1;  // extensions are always swappable
	// Use the spawned part's actual PartRole (validated above) — more accurate than
	// AcceptedPartRoles[0] which was a heuristic.
	Row.PartRole = PS->PartRole;
	if (InsertIdx >= Roster->ChildCount) Roster->ChildCount = static_cast<uint8>(InsertIdx + 1);

	// Update extension occupancy.
	Ext->PortOccupants[PortIndex] = static_cast<int64>(NewChild.id());

	// Destruct the original part item (it lived in player inventory or world floor).
	flecs::entity OldItem = W.entity(static_cast<flecs::entity_t>(OldItemId));
	if (OldItem.is_alive())
	{
		const FContainedIn* CI = OldItem.try_get<FContainedIn>();
		if (CI && CI->ContainerEntityId != 0)
		{
			UFlecsContainerLibrary::RemoveItemFromContainerFromStation(
				Sub, CI->ContainerEntityId, OldItemId);
		}
		else
		{
			OldItem.destruct();
		}
	}

	// Recompute layout — may transition Disabled → Idle if all required functionals present.
	RecomputeEffectiveLayout(StationE);

	// V2 PATCH 3 — clear reservations (success path).
	StationE.remove<FPendingStationAttach>();
	// PartItem reservation was implicitly cleared by destructing the old item above.

	UE_LOG(LogCrafting, Log,
		TEXT("[Modular] AttachPart SUCCESS: station=%llu port=%d new-child=%llu (consumed item=%lld)"),
		(unsigned long long)StationE.id(), PortIndex,
		(unsigned long long)NewChild.id(), OldItemId);
}

namespace
{
	float ResolveDeconstructImpulse(flecs::entity StationE)
	{
		const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
		if (Static && Static->Profile)
		{
			return Static->Profile->DeconstructImpulseCmS;
		}
		return 50.f;  // sensible default
	}

	float ResolveDetachPickupGrace(flecs::entity StationE)
	{
		const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
		if (Static && Static->Profile && Static->Profile->DetachPickupGraceSeconds > 0.f)
		{
			return Static->Profile->DetachPickupGraceSeconds;
		}
		return 0.5f;
	}

	/** V2 PATCH 4 — install pickup grace on a freshly detached body so the
	 *  same-tick PickupCollisionSystem doesn't suck it into the player. */
	void InstallPickupGrace(flecs::entity ChildE, int64 OwnerStationId, float GraceSeconds)
	{
		if (!ChildE.is_valid() || !ChildE.is_alive()) return;

		FWorldItemInstance WorldInst;
		WorldInst.PickupGraceTimer  = GraceSeconds;
		WorldInst.DespawnTimer      = -1.f;
		WorldInst.DroppedByEntityId = OwnerStationId;
		ChildE.set<FWorldItemInstance>(WorldInst);
		ChildE.add<FTagDroppedItem>();
	}
}

bool DetachPartFromStation(flecs::entity ChildE)
{
	check(ChildE.is_valid() && ChildE.is_alive());

	const FMultiblockChildOf* Back = ChildE.try_get<FMultiblockChildOf>();
	if (!Back) return false;

	flecs::world World = ChildE.world();
	flecs::entity StationE = World.entity(static_cast<flecs::entity_t>(Back->AnchorEntityId));
	if (!StationE.is_alive() || StationE.has<FTagCraftingStationDestroying>())
		return false;

	// MEDIUM #4 — gate on FPendingStationAttach. If the station has an in-flight attach
	// reservation, detaching mid-flight could race against FinalizePartAttach (which
	// expects the roster + extensions in a known state). Defer; the player will re-fire
	// the input next tick. Verbose log because this is normal user behavior under load.
	if (StationE.has<FPendingStationAttach>())
	{
		UE_LOG(LogCrafting, Verbose,
			TEXT("[Modular] DetachPart deferred — station=%llu has in-flight attach (re-try next tick)"),
			(unsigned long long)StationE.id());
		return false;
	}

	const FSmelterInstance* Sm = StationE.try_get<FSmelterInstance>();
	const bool bStationIdleOrDisabled = !Sm
		|| Sm->Phase == EProcessPhase::Idle
		|| Sm->Phase == EProcessPhase::Disabled;
	if (!bStationIdleOrDisabled)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] DetachPart rejected — parent station phase=%u (need Idle/Disabled)"),
			Sm ? static_cast<uint32>(Sm->Phase) : 0u);
		return false;
	}

	// Find the roster slot for this child.
	FMultiblockChildren* Roster = StationE.try_get_mut<FMultiblockChildren>();
	check(Roster);
	int32 RosterIdx = INDEX_NONE;
	for (int32 i = 0; i < Roster->ChildCount; ++i)
	{
		if (Roster->ChildSlots[i].ChildEntityId == static_cast<int64>(ChildE.id()))
		{
			RosterIdx = i;
			break;
		}
	}
	if (RosterIdx == INDEX_NONE)
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Modular] DetachPart child=%llu not in roster — corrupt state"),
			(unsigned long long)ChildE.id());
		return false;
	}

	const FMultiblockChildSlot Slot = Roster->ChildSlots[RosterIdx];  // copy for log access after clear
	if (!Slot.bSwappable)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] DetachPart child=%llu role='%s' rigid (bSwappable=false)"),
			(unsigned long long)ChildE.id(), *Slot.PartRole.ToString());
		return false;
	}

	// MEDIUM #1 — order matters: tags + grace BEFORE RestorePartToDynamic + Impulse.
	// RestorePartToDynamic activates the Jolt body; once active, the same-tick
	// PickupCollisionSystem could pick this body up via collision before grace timer
	// is installed. Order:
	//   1. Strip bonded tags + add pickup tags (tag-set is sim-thread-coherent — no race)
	//   2. InstallPickupGrace BEFORE body activation (FWorldItemInstance + FTagDroppedItem)
	//   3. RestorePartToDynamic (body wakes up — grace already in place)
	//   4. ApplyDetachImpulse (kicks the now-active body upward)
	ChildE.remove<FTagMultiblockBonded>();
	ChildE.remove<FMultiblockChildOf>();
	ChildE.add<FTagPickupable>();
	ChildE.add<FTagItem>();
	ChildE.add<FTagInteractable>();

	const float Grace = ResolveDetachPickupGrace(StationE);
	InstallPickupGrace(ChildE, static_cast<int64>(StationE.id()), Grace);

	RestorePartToDynamic(ChildE);

	const float Impulse = ResolveDeconstructImpulse(StationE);
	ApplyDetachImpulse(ChildE, Impulse);

	// Update roster + extensions.
	Roster->ChildSlots[RosterIdx].ChildEntityId = 0;
	Roster->ChildSlots[RosterIdx].bSwappable = 0;
	if (Slot.bIsExtension)
	{
		FMultiblockExtensions* Ext = StationE.try_get_mut<FMultiblockExtensions>();
		if (Ext) Ext->PortOccupants[Slot.PortIndex] = 0;
	}

	// Recompute → may transition Idle → Disabled if a required functional was detached.
	RecomputeEffectiveLayout(StationE);

	UE_LOG(LogCrafting, Log,
		TEXT("[Modular] DetachPart SUCCESS: child=%llu role='%s' from station=%llu (extension=%u port=%u)"),
		(unsigned long long)ChildE.id(), *Slot.PartRole.ToString(),
		(unsigned long long)StationE.id(),
		(uint32)Slot.bIsExtension, (uint32)Slot.PortIndex);

	return true;
}

void DeconstructStation(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return;
	if (StationE.has<FTagCraftingStationDestroying>()) return;

	const FSmelterInstance* Sm = StationE.try_get<FSmelterInstance>();
	if (Sm && Sm->Phase != EProcessPhase::Idle && Sm->Phase != EProcessPhase::Disabled)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Modular] Deconstruct rejected — station phase=%u (need Idle/Disabled)"),
			static_cast<uint32>(Sm->Phase));
		return;
	}

	// ALL slots empty check (player must clear before deconstruct).
	// MEDIUM #5 — single World.each() pass tallying per-slot occupancy via TMap.
	// Was O(slots × world-items); now O(world-items + slots). For typical
	// kMaxCraftingSlots=24 with thousands of world items this is a meaningful win
	// when players spam wrench-deconstruct in a populated scene.
	const FCraftingSlots* Slots = StationE.try_get<FCraftingSlots>();
	if (Slots)
	{
		// Build set of slot ids we care about (≤ kMaxCraftingSlots non-zero entries).
		TSet<int64> SlotIdSet;
		SlotIdSet.Reserve(kMaxCraftingSlots);
		for (int32 i = 0; i < kMaxCraftingSlots; ++i)
		{
			const int64 SlotId = Slots->SlotEntityIds[i];
			if (SlotId != 0) SlotIdSet.Add(SlotId);
		}

		if (SlotIdSet.Num() > 0)
		{
			flecs::world World = StationE.world();
			TMap<int64, int32> CountsBySlot;
			CountsBySlot.Reserve(SlotIdSet.Num());
			World.each([&SlotIdSet, &CountsBySlot](flecs::entity, const FContainedIn& CI)
			{
				if (SlotIdSet.Contains(CI.ContainerEntityId))
				{
					CountsBySlot.FindOrAdd(CI.ContainerEntityId)++;
				}
			});

			// Find any non-empty slot — log + reject.
			for (int32 i = 0; i < kMaxCraftingSlots; ++i)
			{
				const int64 SlotId = Slots->SlotEntityIds[i];
				if (SlotId == 0) continue;
				const int32* CountPtr = CountsBySlot.Find(SlotId);
				if (CountPtr && *CountPtr > 0)
				{
					UE_LOG(LogCrafting, Warning,
						TEXT("[Modular] Deconstruct rejected — slot %d still has %d items"), i, *CountPtr);
					return;
				}
			}
		}
	}

	// Mark station mid-teardown to suppress observers + library hooks.
	StationE.add<FTagCraftingStationDestroying>();

	const float Impulse = ResolveDeconstructImpulse(StationE);
	const float Grace = ResolveDetachPickupGrace(StationE);
	const int64 StationId = static_cast<int64>(StationE.id());

	flecs::world World = StationE.world();

	// Walk roster: restore each child to Dynamic + pickup, impulse pop, install grace.
	// MEDIUM #1 — order: tags + grace BEFORE RestorePartToDynamic + Impulse (see DetachPart).
	FMultiblockChildren* Roster = StationE.try_get_mut<FMultiblockChildren>();
	int32 ChildrenPopped = 0;
	if (Roster)
	{
		for (int32 i = 0; i < Roster->ChildCount; ++i)
		{
			const int64 ChildId = Roster->ChildSlots[i].ChildEntityId;
			if (ChildId == 0) continue;
			flecs::entity Ch = World.entity(static_cast<flecs::entity_t>(ChildId));
			if (!Ch.is_alive()) continue;

			Ch.remove<FTagMultiblockBonded>();
			Ch.remove<FMultiblockChildOf>();
			Ch.add<FTagPickupable>();
			Ch.add<FTagItem>();
			Ch.add<FTagInteractable>();
			InstallPickupGrace(Ch, StationId, Grace);
			RestorePartToDynamic(Ch);
			ApplyDetachImpulse(Ch, Impulse);
			++ChildrenPopped;
		}
	}

	// Restore the anchor itself to Dynamic + pickup. Same order: tags + grace, then activate.
	StationE.remove<FTagMultiblockBonded>();
	StationE.add<FTagPickupable>();
	StationE.add<FTagItem>();
	StationE.add<FTagInteractable>();
	InstallPickupGrace(StationE, StationId, Grace);
	RestorePartToDynamic(StationE);
	ApplyDetachImpulse(StationE, Impulse);

	// Drain + destruct slot containers (slots already verified empty above).
	if (Slots)
	{
		for (int32 i = 0; i < kMaxCraftingSlots; ++i)
		{
			const int64 SlotId = Slots->SlotEntityIds[i];
			if (SlotId == 0) continue;
			flecs::entity SlotE = World.entity(static_cast<flecs::entity_t>(SlotId));
			if (!SlotE.is_alive()) continue;
			SlotE.destruct();
		}
	}

	// Strip station components (anchor REMAINS as a regular pickupable item).
	StationE.remove<FTagCraftingStation>();
	StationE.remove<FCraftingStationStatic>();
	StationE.remove<FCraftingStationInstance>();
	StationE.remove<FCraftingSlots>();
	StationE.remove<FFuelSlot>();
	StationE.remove<FSmelterInstance>();
	StationE.remove<FStationEffectiveLayout>();
	StationE.remove<FMultiblockExtensions>();
	StationE.remove<FMultiblockChildren>();
	StationE.remove<FTagStationDisabled>();
	StationE.remove<FPendingStationAttach>();  // clear any racing reservation
	StationE.remove<FTagCraftingStationDestroying>();

	// Release UI shared state (game thread).
	const FSkeletonKey StationKey(static_cast<uint64>(StationId));
	AsyncTask(ENamedThreads::GameThread, [StationKey]()
	{
		if (UFlecsCraftingUISubsystem* UISub = UFlecsCraftingUISubsystem::SelfPtr)
		{
			UISub->DestroySharedState(StationKey);
		}
	});

	UE_LOG(LogCrafting, Log,
		TEXT("[Modular] Deconstruct SUCCESS: anchor=%llu restored to Dynamic+Pickup, %d children popped"),
		(unsigned long long)StationE.id(), ChildrenPopped);
}

} // namespace FlecsMultiblockRuntime
