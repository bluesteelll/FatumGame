// FlecsCraftingRuntime — sim-thread crafting helpers.

#include "FlecsCraftingRuntime.h"
#include "FlecsCraftingComponents.h"
#include "FlecsCraftingRecipeRegistry.h"
#include "FlecsCraftingRecipeDef.h"
#include "FlecsCraftingStationProfile.h"
#include "FlecsCraftingLog.h"
#include "FlecsCraftingUISubsystem.h"
#include "FlecsItemComponents.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsContainerLibrary.h"
#include "FlecsEntitySpawner.h"   // FEntitySpawnRequest, UFlecsEntityLibrary::SpawnEntity
#include "FlecsBarrageComponents.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "flecs.h"

// Default Z lift (cm) for Smelter overflow / output drops above the station body.
static constexpr float kSmelterOutputOverflowLiftCm = 50.f;

namespace FlecsCraftingRuntime
{

// ═══════════════════════════════════════════════════════════════
// DIRTY FLAG
// ═══════════════════════════════════════════════════════════════

void InvalidateMatchCache(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return;
	if (StationE.has<FTagCraftingStationDestroying>()) return;

	FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>();
	if (!Inst) return;
	Inst->bSnapshotDirty = true;
}

void MarkStationDirtyByContainer(flecs::world& World, int64 OldContainerId, int64 NewContainerId)
{
	auto ResolveAndDirty = [&World](int64 ContainerId)
	{
		if (ContainerId == 0) return;
		flecs::entity ContainerE = World.entity(static_cast<flecs::entity_t>(ContainerId));
		if (!ContainerE.is_valid() || !ContainerE.is_alive()) return;

		const FCraftingSlotBackRef* Back = ContainerE.try_get<FCraftingSlotBackRef>();
		if (!Back || Back->StationEntityId == 0) return;  // Container is not a crafting slot.

		flecs::entity StationE = World.entity(static_cast<flecs::entity_t>(Back->StationEntityId));
		InvalidateMatchCache(StationE);
	};

	ResolveAndDirty(OldContainerId);
	if (NewContainerId != OldContainerId)
	{
		ResolveAndDirty(NewContainerId);
	}
}

// ═══════════════════════════════════════════════════════════════
// DIGEST + MATCH
// ═══════════════════════════════════════════════════════════════

namespace
{
	/**
	 * Collect (ItemTypeId, Count) pairs across all slots whose role matches the filter mask.
	 * RoleMask bits are (1u << ESlotRole value). Sorted ascending by TypeId to stabilise hash.
	 */
	void CollectSlotContents(
		flecs::entity StationE,
		uint32 RoleMask,
		TArray<TPair<int32, int32>>& OutPairs)
	{
		OutPairs.Reset();
		const FCraftingSlots* Slots = StationE.try_get<FCraftingSlots>();
		if (!Slots) return;
		const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
		if (!Static || !Static->Profile) return;

		flecs::world World = StationE.world();

		const TArray<FSlotLayoutDef>& Layout = Static->Profile->SlotLayout;
		const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);

		for (int32 i = 0; i < SlotCount; ++i)
		{
			const uint32 RoleBit = (1u << static_cast<uint32>(Layout[i].Role));
			if ((RoleMask & RoleBit) == 0) continue;

			const int64 SlotId = Slots->SlotEntityIds[i];
			if (SlotId == 0) continue;

			World.each([SlotId, &OutPairs](flecs::entity ItemE, const FContainedIn& CI, const FItemInstance& Inst)
			{
				if (CI.ContainerEntityId != SlotId) return;
				const FItemStaticData* ItemStatic = ItemE.try_get<FItemStaticData>();
				if (!ItemStatic) return;

				// Accumulate into existing entry for the TypeId when possible — ring-buffer style.
				for (TPair<int32, int32>& Pair : OutPairs)
				{
					if (Pair.Key == ItemStatic->TypeId)
					{
						Pair.Value += Inst.Count;
						return;
					}
				}
				OutPairs.Emplace(ItemStatic->TypeId, Inst.Count);
			});
		}

		OutPairs.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
		{
			return A.Key < B.Key;
		});
	}

	/** Read first item in fuel slot and return its FCraftingFuelItemData (or nullptr if empty). */
	const FCraftingFuelItemData* PeekFuelSlotFirstItem(flecs::entity StationE, flecs::entity& OutFirstItem)
	{
		OutFirstItem = flecs::entity();

		const FFuelSlot* FuelSlot = StationE.try_get<FFuelSlot>();
		if (!FuelSlot || FuelSlot->FuelSlotEntityId == 0) return nullptr;

		flecs::world World = StationE.world();
		const FCraftingFuelItemData* FoundData = nullptr;
		flecs::entity FoundItem;

		World.each([FuelSlot, &FoundData, &FoundItem](flecs::entity ItemE, const FContainedIn& CI)
		{
			if (FoundData) return;  // Keep first match.
			if (CI.ContainerEntityId != FuelSlot->FuelSlotEntityId) return;

			const FCraftingFuelItemData* D = ItemE.try_get<FCraftingFuelItemData>();
			if (!D) return;
			FoundData = D;
			FoundItem = ItemE;
		});

		OutFirstItem = FoundItem;
		return FoundData;
	}
}

uint64 ComputeSlotDigest(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return 0;

	const uint32 Mask = (1u << static_cast<uint32>(ESlotRole::MaterialInput))
	                  | (1u << static_cast<uint32>(ESlotRole::Fuel));

	TArray<TPair<int32, int32>> Pairs;
	CollectSlotContents(StationE, Mask, Pairs);

	uint32 Seed = 0;
	for (const TPair<int32, int32>& Pair : Pairs)
	{
		Seed = HashCombine(Seed, GetTypeHash(Pair.Key));
		Seed = HashCombine(Seed, GetTypeHash(Pair.Value));
	}

	// Fold in active fuel + reservoir bucket (coarse — we don't want tiny float deltas to churn).
	if (const FCraftingStationInstance* Inst = StationE.try_get<FCraftingStationInstance>())
	{
		Seed = HashCombine(Seed, GetTypeHash(static_cast<uint8>(Inst->ActiveFuelType)));
		const uint32 FuelBucket = static_cast<uint32>(Inst->FuelChargeSecondsRemaining);
		Seed = HashCombine(Seed, GetTypeHash(FuelBucket));
	}

	return static_cast<uint64>(Seed) | (static_cast<uint64>(Pairs.Num()) << 32);
}

const UFlecsCraftingRecipeDef* MatchRecipe(flecs::entity StationE, UFlecsCraftingRecipeRegistry* Registry)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return nullptr;
	if (StationE.has<FTagCraftingStationDestroying>()) return nullptr;

	FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>();
	if (!Inst) return nullptr;

	// MN5 — reset unconditionally to None so stale diagnostics don't carry when match succeeds.
	Inst->LastDiagnostic = ECraftingMatchDiagnostic::None;

	const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
	if (!Static)
	{
		Inst->MatchedRecipe = nullptr;
		Inst->LastDiagnostic = ECraftingMatchDiagnostic::NoMatch;
		return nullptr;
	}

	if (!Registry)
	{
		Inst->MatchedRecipe = nullptr;
		Inst->LastDiagnostic = ECraftingMatchDiagnostic::NoMatch;
		return nullptr;
	}

	// Gather MaterialInput contents.
	TArray<TPair<int32, int32>> Inputs;
	const uint32 InputMask = (1u << static_cast<uint32>(ESlotRole::MaterialInput));
	CollectSlotContents(StationE, InputMask, Inputs);

	// Determine active fuel classification.
	const EFuelType ActiveFuel = ResolveFuelType(StationE);
	const float FuelCharge = Inst->FuelChargeSecondsRemaining;

	const UFlecsCraftingRecipeDef* Match = Registry->FindMatch(
		Static->StationType, ActiveFuel, Inputs, FuelCharge);

	Inst->MatchedRecipe = Match;
	if (!Match)
	{
		Inst->LastDiagnostic = ECraftingMatchDiagnostic::NoMatch;
	}
	return Match;
}

// ═══════════════════════════════════════════════════════════════
// FUEL
// ═══════════════════════════════════════════════════════════════

EFuelType ResolveFuelType(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return EFuelType::None;

	const FCraftingStationInstance* Inst = StationE.try_get<FCraftingStationInstance>();
	if (Inst && Inst->FuelChargeSecondsRemaining > 0.f && Inst->ActiveFuelType != EFuelType::None)
	{
		return Inst->ActiveFuelType;
	}

	flecs::entity FirstItem;
	if (const FCraftingFuelItemData* Data = PeekFuelSlotFirstItem(StationE, FirstItem))
	{
		return Data->FuelType;
	}
	return EFuelType::None;
}

bool TryConsumeFuelSlot(flecs::entity StationE, float /*AmountSeconds*/)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return false;
	if (StationE.has<FTagCraftingStationDestroying>()) return false;

	FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>();
	if (!Inst) return false;

	const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
	if (!Static) return false;

	flecs::entity FirstItem;
	const FCraftingFuelItemData* Data = PeekFuelSlotFirstItem(StationE, FirstItem);

	// Case A — reservoir already primed, no item visible. ActiveFuelType stays (grace burn).
	if (!Data)
	{
		// When reservoir drained and slot empty, clear ActiveFuelType.
		if (Inst->FuelChargeSecondsRemaining <= 0.f && Inst->ActiveFuelType != EFuelType::None)
		{
			Inst->ActiveFuelType = EFuelType::None;
			return true;
		}
		return false;
	}

	// Station supports this fuel?
	const uint16 FuelBit = static_cast<uint16>(Data->FuelType);
	if ((Static->SupportedFuelMask & FuelBit) == 0)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("Station '%s' rejects fuel type %u (not in SupportedFuelMask 0x%04x)"),
			*Static->StationName.ToString(), static_cast<uint32>(Data->FuelType),
			static_cast<uint32>(Static->SupportedFuelMask));
		return false;
	}

	// Case B — reservoir empty, no active type: adopt this item's type and consume one unit.
	if (Inst->FuelChargeSecondsRemaining <= 0.f && Inst->ActiveFuelType == EFuelType::None)
	{
		Inst->ActiveFuelType = Data->FuelType;
		Inst->FuelChargeSecondsRemaining = Data->ChargeSecondsPerUnit;
		checkf(Inst->FuelChargeSecondsRemaining > 0.f || Inst->ActiveFuelType == EFuelType::None,
			TEXT("Invariant: FuelChargeSecondsRemaining > 0 implies ActiveFuelType != None"));

		// Consume one unit.
		if (FirstItem.is_valid() && FirstItem.is_alive())
		{
			FItemInstance* ItemInst = FirstItem.try_get_mut<FItemInstance>();
			if (ItemInst)
			{
				--ItemInst->Count;
				if (ItemInst->Count <= 0) FirstItem.destruct();
			}
		}
		return true;
	}

	// Case C — reservoir has charge and item matches active type: top up.
	if (Inst->ActiveFuelType == Data->FuelType)
	{
		Inst->FuelChargeSecondsRemaining += Data->ChargeSecondsPerUnit;
		if (FirstItem.is_valid() && FirstItem.is_alive())
		{
			FItemInstance* ItemInst = FirstItem.try_get_mut<FItemInstance>();
			if (ItemInst)
			{
				--ItemInst->Count;
				if (ItemInst->Count <= 0) FirstItem.destruct();
			}
		}
		return true;
	}

	// Case D — type mismatch while reservoir active: REJECT (item stays, no state change).
	UE_LOG(LogCrafting, Warning,
		TEXT("Station '%s' refusing fuel type %u while active type %u reservoir %.1fs burns"),
		*Static->StationName.ToString(),
		static_cast<uint32>(Data->FuelType),
		static_cast<uint32>(Inst->ActiveFuelType),
		Inst->FuelChargeSecondsRemaining);
	return false;
}

// ═══════════════════════════════════════════════════════════════
// SNAPSHOT
// ═══════════════════════════════════════════════════════════════

void BuildSnapshot(flecs::entity StationE, FCraftingStationSnapshot& OutSnapshot)
{
	OutSnapshot.StationName = NAME_None;
	OutSnapshot.DebugDisplayName = FText::GetEmpty();
	OutSnapshot.MatchedRecipeName = NAME_None;
	OutSnapshot.Diagnostic = 0;
	OutSnapshot.ActiveFuelType = 0;
	OutSnapshot.FuelChargeSecondsRemaining = 0.f;
	OutSnapshot.ProcessPhase = 0;          // Phase 3 — default Idle
	OutSnapshot.ProgressSeconds = 0.f;
	OutSnapshot.DurationSecondsCached = 0.f;
	OutSnapshot.Slots.Reset();

	if (!StationE.is_valid() || !StationE.is_alive()) return;

	const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
	const FCraftingStationInstance* Inst = StationE.try_get<FCraftingStationInstance>();
	const FCraftingSlots* Slots = StationE.try_get<FCraftingSlots>();

	if (!Static || !Inst || !Slots || !Static->Profile) return;

	OutSnapshot.StationName      = Static->StationName;
	OutSnapshot.DebugDisplayName = Static->Profile->DisplayName;
	OutSnapshot.Diagnostic       = static_cast<uint8>(Inst->LastDiagnostic);
	OutSnapshot.ActiveFuelType   = static_cast<uint8>(Inst->ActiveFuelType);
	OutSnapshot.FuelChargeSecondsRemaining = Inst->FuelChargeSecondsRemaining;

	// Phase 3 — Smelter (or future Press / Forge) extension.
	if (const FSmelterInstance* SmInst = StationE.try_get<FSmelterInstance>())
	{
		OutSnapshot.ProcessPhase          = static_cast<uint8>(SmInst->Phase);
		OutSnapshot.ProgressSeconds       = SmInst->ProgressSeconds;
		OutSnapshot.DurationSecondsCached = SmInst->DurationCached;
	}

	if (Inst->MatchedRecipe)
	{
		OutSnapshot.MatchedRecipeName = Inst->MatchedRecipe->GetFName();
	}

	flecs::world World = StationE.world();
	const TArray<FSlotLayoutDef>& Layout = Static->Profile->SlotLayout;
	const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);

	OutSnapshot.Slots.Reserve(SlotCount);
	for (int32 i = 0; i < SlotCount; ++i)
	{
		FCraftingStationSlotSnapshot Row;
		Row.SlotName  = Layout[i].SlotName;
		Row.Role      = static_cast<uint8>(Layout[i].Role);
		Row.ItemCount = 0;

		const int64 SlotId = Slots->SlotEntityIds[i];
		if (SlotId != 0)
		{
			World.each([SlotId, &Row](flecs::entity ItemE, const FContainedIn& CI, const FItemInstance& ItemInst)
			{
				if (CI.ContainerEntityId != SlotId) return;
				++Row.ItemCount;

				if (Row.ItemLines.Num() >= Row.ItemLines.GetSlack() + 8) return;  // Inline cap guard.

				const FItemStaticData* Data = ItemE.try_get<FItemStaticData>();
				const FText ItemName = Data
					? FText::FromName(Data->ItemName)
					: FText::FromString(TEXT("Unknown"));
				Row.ItemLines.Add(FText::Format(
					FText::FromString(TEXT("{0}x {1}")),
					FText::AsNumber(ItemInst.Count),
					ItemName));
			});
		}

		OutSnapshot.Slots.Add(MoveTemp(Row));
	}
}

// ═══════════════════════════════════════════════════════════════
// SMELTER (Phase 3)
// ═══════════════════════════════════════════════════════════════

namespace
{
	// Read the station's world position from the bound Barrage body. Returns the
	// origin if the body cannot be resolved — Smelter callers fall back to a Warning.
	FVector ReadStationWorldPosition(flecs::entity Station)
	{
		const FBarrageBody* Body = Station.try_get<FBarrageBody>();
		if (!Body || !Body->IsValid()) return FVector::ZeroVector;

		UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
		if (!Sub) return FVector::ZeroVector;

		Sub->EnsureBarrageAccess();
		UBarrageDispatch* Barrage = Sub->GetBarrageDispatch();
		if (!Barrage) return FVector::ZeroVector;

		FBLet Prim = Barrage->GetShapeRef(Body->BarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim)) return FVector::ZeroVector;

		const FVector3f PosF = FBarragePrimitive::GetPosition(Prim);
		return FVector(PosF.X, PosF.Y, PosF.Z);
	}

	bool MaskIncludesRole(FlecsCraftingRuntime::ESlotRoleLockMask Mask, ESlotRole Role)
	{
		using FlecsCraftingRuntime::ESlotRoleLockMask;
		switch (Mask)
		{
		case ESlotRoleLockMask::MaterialInputOnly:
			return Role == ESlotRole::MaterialInput;
		case ESlotRoleLockMask::MaterialInputAndFuel:
			return Role == ESlotRole::MaterialInput || Role == ESlotRole::Fuel;
		case ESlotRoleLockMask::All:
			return true;
		}
		return false;
	}
}

bool BuildIngredientLedger(
	flecs::entity                                       Station,
	const FCraftingStationStatic&                       Static,
	const FCraftingSlots&                               Slots,
	const UFlecsCraftingRecipeDef*                      Recipe,
	TArray<FConsumedIngredient, TInlineAllocator<8>>&   OutLedger)
{
	OutLedger.Reset();
	if (!Recipe || !Static.Profile) return false;
	if (Recipe->Ingredients.Num() == 0) return false;

	flecs::world World = Station.world();
	const TArray<FSlotLayoutDef>& Layout = Static.Profile->SlotLayout;
	const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);

	// For each ingredient: walk every MaterialInput slot in profile order, accumulate
	// matching items by ItemTypeId until Count is reached. Record one FConsumedIngredient
	// per slot contribution (so refund can return to the exact source slot).
	for (const FCraftingIngredient& Ing : Recipe->Ingredients)
	{
		UFlecsEntityDefinition* IngDef = Ing.ResolvedDefinition.Get();
		if (!IngDef || !IngDef->ItemDefinition)
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("[Smelter] '%s' BuildIngredientLedger: recipe '%s' ingredient has unresolved definition"),
				*Static.StationName.ToString(), *Recipe->GetName());
			return false;
		}
		const int32 RequiredTypeId = IngDef->ItemDefinition->ItemTypeId;
		int32 Remaining = Ing.Count;

		for (int32 SlotIdx = 0; SlotIdx < SlotCount && Remaining > 0; ++SlotIdx)
		{
			if (Layout[SlotIdx].Role != ESlotRole::MaterialInput) continue;
			const int64 SlotId = Slots.SlotEntityIds[SlotIdx];
			if (SlotId == 0) continue;

			int32 SlotContribution = 0;
			World.each([SlotId, RequiredTypeId, &Remaining, &SlotContribution](
				flecs::entity ItemE, const FContainedIn& CI, const FItemInstance& II)
			{
				if (Remaining <= 0) return;
				if (CI.ContainerEntityId != SlotId) return;
				const FItemStaticData* IS = ItemE.try_get<FItemStaticData>();
				if (!IS || IS->TypeId != RequiredTypeId) return;
				const int32 Take = FMath::Min(II.Count, Remaining);
				SlotContribution += Take;
				Remaining        -= Take;
			});

			if (SlotContribution > 0)
			{
				FConsumedIngredient Cons;
				Cons.Definition      = IngDef;
				Cons.Count           = SlotContribution;
				Cons.SourceSlotIndex = static_cast<uint8>(SlotIdx);
				OutLedger.Add(Cons);
			}
		}

		if (Remaining > 0)
		{
			// Cannot fully satisfy this ingredient — bail. No partial mutation has occurred.
			UE_LOG(LogCrafting, Verbose,
				TEXT("[Smelter] '%s' BuildIngredientLedger: ingredient '%s' short by %d"),
				*Static.StationName.ToString(), *IngDef->GetName(), Remaining);
			OutLedger.Reset();
			return false;
		}
	}
	return true;
}

float ProbeFuelSlotProjectedCharge(flecs::entity Station, EFuelType RequiredType)
{
	if (!Station.is_valid() || !Station.is_alive()) return 0.f;

	const FFuelSlot* FuelSlot = Station.try_get<FFuelSlot>();
	if (!FuelSlot || FuelSlot->FuelSlotEntityId == 0) return 0.f;

	flecs::world World = Station.world();
	const int64 FuelSlotId = FuelSlot->FuelSlotEntityId;

	float Charge = 0.f;
	World.each([FuelSlotId, RequiredType, &Charge](
		flecs::entity ItemE, const FContainedIn& CI, const FItemInstance& II)
	{
		if (CI.ContainerEntityId != FuelSlotId) return;
		const FCraftingFuelItemData* Data = ItemE.try_get<FCraftingFuelItemData>();
		if (!Data) return;
		// Skip mismatched types (e.g. ElectricCell in a Coal-required recipe).
		if (RequiredType != EFuelType::None && Data->FuelType != RequiredType) return;
		Charge += static_cast<float>(II.Count) * Data->ChargeSecondsPerUnit;
	});
	return Charge;
}

void LockStationSlots(flecs::entity Station, const FCraftingSlots& Slots, ESlotRoleLockMask Mask)
{
	if (!Station.is_valid() || !Station.is_alive()) return;
	const FCraftingStationStatic* Static = Station.try_get<FCraftingStationStatic>();
	if (!Static || !Static->Profile) return;

	flecs::world World = Station.world();
	const TArray<FSlotLayoutDef>& Layout = Static->Profile->SlotLayout;
	const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);
	const int64 OwningStationId = static_cast<int64>(Station.id());

	int32 LockedCount = 0;
	for (int32 i = 0; i < SlotCount; ++i)
	{
		if (!MaskIncludesRole(Mask, Layout[i].Role)) continue;
		const int64 SlotId = Slots.SlotEntityIds[i];
		if (SlotId == 0) continue;
		flecs::entity SlotE = World.entity(static_cast<flecs::entity_t>(SlotId));
		if (!SlotE.is_alive()) continue;

		FCraftingSlotLockedByStation Lock;
		Lock.OwningStationEntityId = OwningStationId;
		SlotE.set<FCraftingSlotLockedByStation>(Lock);
		++LockedCount;
	}

	UE_LOG(LogCrafting, Verbose,
		TEXT("[Smelter] '%s' LockStationSlots mask=%u → locked %d slot(s)"),
		*Static->StationName.ToString(), static_cast<uint32>(Mask), LockedCount);
}

void UnlockStationSlots(flecs::entity Station, const FCraftingSlots& Slots, ESlotRoleLockMask Mask)
{
	if (!Station.is_valid() || !Station.is_alive()) return;
	const FCraftingStationStatic* Static = Station.try_get<FCraftingStationStatic>();
	if (!Static || !Static->Profile) return;

	flecs::world World = Station.world();
	const TArray<FSlotLayoutDef>& Layout = Static->Profile->SlotLayout;
	const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);

	int32 UnlockedCount = 0;
	for (int32 i = 0; i < SlotCount; ++i)
	{
		if (!MaskIncludesRole(Mask, Layout[i].Role)) continue;
		const int64 SlotId = Slots.SlotEntityIds[i];
		if (SlotId == 0) continue;
		flecs::entity SlotE = World.entity(static_cast<flecs::entity_t>(SlotId));
		if (!SlotE.is_alive()) continue;
		if (SlotE.has<FCraftingSlotLockedByStation>())
		{
			SlotE.remove<FCraftingSlotLockedByStation>();
			++UnlockedCount;
		}
	}

	UE_LOG(LogCrafting, Verbose,
		TEXT("[Smelter] '%s' UnlockStationSlots mask=%u → unlocked %d slot(s)"),
		*Static->StationName.ToString(), static_cast<uint32>(Mask), UnlockedCount);
}

void DropOverflowToFloor(flecs::entity Station, UFlecsEntityDefinition* ItemDef, int32 Count)
{
	if (!Station.is_valid() || !Station.is_alive() || !ItemDef || Count <= 0) return;

	const FVector StationPos = ReadStationWorldPosition(Station);
	if (StationPos.IsZero())
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Smelter] DropOverflowToFloor: station entity=%llu has no resolvable Barrage body — skip drop of %dx '%s'"),
			(unsigned long long)Station.id(), Count, *ItemDef->GetName());
		return;
	}

	const FVector DropLocation = StationPos + FVector(0.f, 0.f, kSmelterOutputOverflowLiftCm);

	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
	if (!Sub)
	{
		UE_LOG(LogCrafting, Warning, TEXT("[Smelter] DropOverflowToFloor: no FlecsArtillerySubsystem — skip"));
		return;
	}
	UWorld* WorldCtx = Sub->GetWorld();
	if (!WorldCtx)
	{
		UE_LOG(LogCrafting, Warning, TEXT("[Smelter] DropOverflowToFloor: subsystem has no UWorld — skip"));
		return;
	}

	// UFlecsEntityLibrary::SpawnEntity touches Barrage + ISM on the game thread synchronously
	// (Renderer->AddInstance requires UComponent mutation). Marshal there. One-tick latency is
	// expected and acceptable per blueprint §1.5.7.
	TWeakObjectPtr<UWorld> WeakWorld = WorldCtx;
	TWeakObjectPtr<UFlecsEntityDefinition> WeakDef = ItemDef;
	const int32 ItemCount = Count;
	AsyncTask(ENamedThreads::GameThread, [WeakWorld, WeakDef, DropLocation, ItemCount]()
	{
		UWorld* W = WeakWorld.Get();
		UFlecsEntityDefinition* Def = WeakDef.Get();
		if (!W || !Def) return;

		FEntitySpawnRequest Request = FEntitySpawnRequest::FromDefinition(Def, DropLocation);
		Request.ItemCount = ItemCount;
		Request.bPickupable = true;  // dropped output should be pickupable

		FSkeletonKey SpawnedKey = UFlecsEntityLibrary::SpawnEntity(W, Request);
		UE_LOG(LogCrafting, Log,
			TEXT("[Smelter] DropOverflowToFloor: spawned %dx '%s' at (%.1f, %.1f, %.1f) key=0x%llX"),
			ItemCount, *Def->GetName(), DropLocation.X, DropLocation.Y, DropLocation.Z,
			(unsigned long long)SpawnedKey.Obj);
	});
}

bool RemoveExactCountFromContainerFromStation(
	flecs::entity Station,
	int64 ContainerEntityId,
	UFlecsEntityDefinition* ItemDef,
	int32 Count)
{
	if (!Station.is_valid() || !ItemDef || !ItemDef->ItemDefinition || ContainerEntityId == 0 || Count <= 0)
		return false;

	const int32 RequiredTypeId = ItemDef->ItemDefinition->ItemTypeId;
	flecs::world World = Station.world();
	flecs::entity ContainerE = World.entity(static_cast<flecs::entity_t>(ContainerEntityId));
	if (!ContainerE.is_alive()) return false;

	// Collect candidates (largest stack first for minimum entity churn).
	struct FCandidate { flecs::entity E; int32 Count; const FItemStaticData* Static; };
	TArray<FCandidate, TInlineAllocator<8>> Candidates;

	World.each([ContainerEntityId, RequiredTypeId, &Candidates](
		flecs::entity ItemE, const FContainedIn& CI, const FItemInstance& II)
	{
		if (CI.ContainerEntityId != ContainerEntityId) return;
		const FItemStaticData* IS = ItemE.try_get<FItemStaticData>();
		if (!IS || IS->TypeId != RequiredTypeId) return;
		Candidates.Add({ItemE, II.Count, IS});
	});

	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Count > B.Count; });

	int32 Remaining = Count;
	int32 EntitiesDestructed = 0;
	float WeightRemoved = 0.f;
	const FContainerStatic* CS = ContainerE.try_get<FContainerStatic>();
	for (FCandidate& C : Candidates)
	{
		if (Remaining <= 0) break;
		FItemInstance* II = C.E.try_get_mut<FItemInstance>();
		if (!II) continue;
		const int32 Take = FMath::Min(II->Count, Remaining);
		II->Count -= Take;
		Remaining -= Take;
		if (C.Static) WeightRemoved += static_cast<float>(Take) * C.Static->Weight;

		if (II->Count <= 0)
		{
			// Free grid occupancy before destruct (mirror of Phase 1 RemoveItemFromContainer).
			const FContainedIn* CI = C.E.try_get<FContainedIn>();
			if (CI && CS && CS->Type == EContainerType::Grid && CI->IsInGrid())
			{
				if (FContainerGridInstance* Grid = ContainerE.try_get_mut<FContainerGridInstance>())
				{
					const FIntPoint ItemSize = C.Static ? C.Static->GridSize : FIntPoint(1, 1);
					Grid->Free(CI->GridPosition, ItemSize, CS->GridWidth);
				}
			}
			C.E.destruct();
			++EntitiesDestructed;
		}
	}

	// Bookkeeping on the container.
	if (FContainerInstance* CInst = ContainerE.try_get_mut<FContainerInstance>())
	{
		CInst->CurrentCount = FMath::Max(0, CInst->CurrentCount - EntitiesDestructed);
		CInst->CurrentWeight = FMath::Max(0.f, CInst->CurrentWeight - WeightRemoved);
	}

	// Dirty the station so the snapshot flush picks up the change in the same tick.
	MarkStationDirtyByContainer(World, ContainerEntityId, 0);

	if (Remaining > 0)
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Smelter] RemoveExactCountFromContainerFromStation: failed to remove all %d (%d remaining) of '%s' from container %lld"),
			Count, Remaining, *ItemDef->GetName(), ContainerEntityId);
		return false;
	}
	return true;
}

void TrySmelterStart(
	flecs::entity Station,
	const FCraftingStationStatic& Static,
	FCraftingStationInstance& Inst,
	FSmelterInstance& SmInst,
	const FCraftingSlots& Slots)
{
	if (Station.has<FTagCraftingStationDestroying>())
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Smelter] '%s' START rejected — station tearing down"),
			*Static.StationName.ToString());
		return;
	}

	const UFlecsCraftingRecipeDef* Recipe = Inst.MatchedRecipe;
	if (!Recipe)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Smelter] '%s' START rejected — no matched recipe (Diag=%u)"),
			*Static.StationName.ToString(),
			static_cast<uint32>(Inst.LastDiagnostic));
		return;
	}

	// Pre-check 2: ingredient ledger build (reads slot contents). Bail if any
	// ingredient is no longer present in expected count (race condition guard).
	TArray<FConsumedIngredient, TInlineAllocator<8>> Ledger;
	if (!BuildIngredientLedger(Station, Static, Slots, Recipe, Ledger))
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Smelter] '%s' START rejected — ingredients changed since match (recipe='%s')"),
			*Static.StationName.ToString(), *Recipe->GetName());
		Inst.bSnapshotDirty = true;  // re-match on next flush
		return;
	}

	// Pre-check 3: fuel sufficient. Reservoir + projected fuel-slot fill must >= req.
	float Available = Inst.FuelChargeSecondsRemaining;
	if (Recipe->RequiredFuelType != EFuelType::None &&
		Available < Recipe->FuelChargeSecondsRequired)
	{
		Available += ProbeFuelSlotProjectedCharge(Station, Recipe->RequiredFuelType);
	}
	if (Recipe->RequiredFuelType != EFuelType::None &&
		Available < Recipe->FuelChargeSecondsRequired)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Smelter] '%s' START rejected — insufficient fuel (have %.1fs, need %.1fs)"),
			*Static.StationName.ToString(),
			Available, Recipe->FuelChargeSecondsRequired);
		Inst.LastDiagnostic = ECraftingMatchDiagnostic::InsufficientFuel;
		Inst.bSnapshotDirty = true;
		return;
	}

	// Commit: physically remove ingredient items via internal API (no lock yet on first commit).
	bool bCommitFailed = false;
	for (const FConsumedIngredient& Cons : Ledger)
	{
		const int64 SlotEntId = Slots.SlotEntityIds[Cons.SourceSlotIndex];
		if (!RemoveExactCountFromContainerFromStation(Station, SlotEntId, Cons.Definition, Cons.Count))
		{
			UE_LOG(LogCrafting, Error,
				TEXT("[Smelter] '%s' START LEDGER INCONSISTENCY — partial commit, refunding"),
				*Static.StationName.ToString());
			ensureMsgf(false, TEXT("Smelter ingredient commit failed mid-stream"));
			bCommitFailed = true;
			break;
		}

		UE_LOG(LogCrafting, Log,
			TEXT("[Smelter] '%s' COMMIT %dx '%s' from slot %u"),
			*Static.StationName.ToString(),
			Cons.Count,
			*Cons.Definition->GetName(),
			(uint32)Cons.SourceSlotIndex);
	}

	if (bCommitFailed)
	{
		// Best-effort partial refund: walk the ledger and add-back to the source slot
		// (which is still unlocked since the lock add happens AFTER successful commit below).
		for (const FConsumedIngredient& Cons : Ledger)
		{
			const int64 SlotEntId = Slots.SlotEntityIds[Cons.SourceSlotIndex];
			int32 Added = UFlecsContainerLibrary::AddItemToContainerFromStation(
				UFlecsArtillerySubsystem::SelfPtr, SlotEntId, Cons.Definition, Cons.Count, true);
			if (Added < Cons.Count)
			{
				DropOverflowToFloor(Station, Cons.Definition, Cons.Count - Added);
			}
		}
		Inst.bSnapshotDirty = true;
		return;
	}

	// Lock MaterialInput + Fuel slots — player cannot interfere mid-process.
	LockStationSlots(Station, Slots, ESlotRoleLockMask::MaterialInputAndFuel);

	SmInst.ConsumedLedger  = MoveTemp(Ledger);
	SmInst.ProgressSeconds = 0.f;
	SmInst.DurationCached  = Recipe->DurationSeconds;
	SmInst.Phase           = EProcessPhase::Processing;
	Inst.bSnapshotDirty    = true;

	UE_LOG(LogCrafting, Log,
		TEXT("[Smelter] '%s' START recipe='%s' duration=%.1fs fuel-req=%.1fs reservoir=%.1fs"),
		*Static.StationName.ToString(),
		*Recipe->GetName(),
		SmInst.DurationCached,
		Recipe->FuelChargeSecondsRequired,
		Inst.FuelChargeSecondsRemaining);
}

void SmelterTickProcessing(
	flecs::entity Station,
	const FCraftingStationStatic& Static,
	FCraftingStationInstance& Inst,
	FSmelterInstance& SmInst,
	float DT)
{
	// Reservoir empty? Try to refill from the fuel slot (Phase 1 helper).
	if (Inst.FuelChargeSecondsRemaining <= 0.f)
	{
		if (!TryConsumeFuelSlot(Station, 0.f))
		{
			SmInst.Phase = EProcessPhase::Stalled;
			Inst.bSnapshotDirty = true;
			UE_LOG(LogCrafting, Log,
				TEXT("[Smelter] '%s' STALL — reservoir empty, no fuel item (Progress=%.2f/%.2f)"),
				*Static.StationName.ToString(),
				SmInst.ProgressSeconds, SmInst.DurationCached);
			return;
		}
	}

	const float Spend = FMath::Min(DT, Inst.FuelChargeSecondsRemaining);
	Inst.FuelChargeSecondsRemaining -= Spend;
	SmInst.ProgressSeconds          += Spend;

	// Snapshot dirties on every tick during Processing — flush publishes progress smoothly.
	Inst.bSnapshotDirty = true;

	if (SmInst.ProgressSeconds >= SmInst.DurationCached)
	{
		SmInst.Phase = EProcessPhase::Completing;
		UE_LOG(LogCrafting, Log,
			TEXT("[Smelter] '%s' COMPLETE — Progress %.2f/%.2f reached"),
			*Static.StationName.ToString(),
			SmInst.ProgressSeconds, SmInst.DurationCached);
	}
}

void SmelterTickStalled(
	flecs::entity Station,
	FCraftingStationInstance& Inst,
	FSmelterInstance& SmInst)
{
	if (TryConsumeFuelSlot(Station, 0.f))
	{
		SmInst.Phase = EProcessPhase::Processing;
		Inst.bSnapshotDirty = true;

		const FCraftingStationStatic* Static = Station.try_get<FCraftingStationStatic>();
		UE_LOG(LogCrafting, Log,
			TEXT("[Smelter] '%s' RESUME — fuel restored, reservoir=%.2fs"),
			Static ? *Static->StationName.ToString() : TEXT("?"),
			Inst.FuelChargeSecondsRemaining);
	}
	// else: silent no-op tick; UI keeps rendering Stalled.
}

void SmelterCancel(
	flecs::entity Station,
	const FCraftingStationStatic& Static,
	FCraftingStationInstance& Inst,
	FSmelterInstance& SmInst,
	const FCraftingSlots& Slots)
{
	if (Station.has<FTagCraftingStationDestroying>())
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Smelter] '%s' CANCEL during destruction — discarding ledger silently"),
			*Static.StationName.ToString());
		SmInst.ConsumedLedger.Reset();
		SmInst.Phase = EProcessPhase::Cancelled;
		return;
	}

	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
	const int32 LedgerSize = SmInst.ConsumedLedger.Num();
	int32 RefundedItems    = 0;

	for (const FConsumedIngredient& Cons : SmInst.ConsumedLedger)
	{
		// First try original source slot.
		const int64 SourceSlotId = Slots.SlotEntityIds[Cons.SourceSlotIndex];
		int32 Added = 0;
		if (Sub)
		{
			Added = UFlecsContainerLibrary::AddItemToContainerFromStation(
				Sub, SourceSlotId, Cons.Definition, Cons.Count, /*bAutoStack=*/true);
		}
		if (Added >= Cons.Count) { ++RefundedItems; continue; }

		// Source slot full → walk other MaterialInput slots in profile order.
		if (Static.Profile)
		{
			const TArray<FSlotLayoutDef>& Layout = Static.Profile->SlotLayout;
			const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);
			for (int32 i = 0; i < SlotCount && Added < Cons.Count; ++i)
			{
				if (Layout[i].Role != ESlotRole::MaterialInput) continue;
				if (i == Cons.SourceSlotIndex) continue;
				const int64 OtherId = Slots.SlotEntityIds[i];
				if (OtherId == 0 || !Sub) continue;
				const int32 More = UFlecsContainerLibrary::AddItemToContainerFromStation(
					Sub, OtherId, Cons.Definition, Cons.Count - Added, /*bAutoStack=*/true);
				Added += More;
			}
		}
		if (Added >= Cons.Count) { ++RefundedItems; continue; }

		const int32 RemainingToDrop = Cons.Count - Added;
		DropOverflowToFloor(Station, Cons.Definition, RemainingToDrop);
		UE_LOG(LogCrafting, Warning,
			TEXT("[Smelter] '%s' CANCEL refund %dx '%s' overflowed → floor drop"),
			*Static.StationName.ToString(),
			RemainingToDrop, *Cons.Definition->GetName());
	}

	if (LedgerSize > 0 && RefundedItems < LedgerSize / 2)
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Smelter] '%s' CANCEL refund destination invalid for >half items (%d of %d) — investigate slot configuration"),
			*Static.StationName.ToString(),
			LedgerSize - RefundedItems, LedgerSize);
	}

	UE_LOG(LogCrafting, Log,
		TEXT("[Smelter] '%s' CANCEL refund=%d/%d items, reservoir UNCHANGED at %.1fs"),
		*Static.StationName.ToString(),
		RefundedItems, LedgerSize,
		Inst.FuelChargeSecondsRemaining);

	SmInst.ConsumedLedger.Reset();
	SmInst.ProgressSeconds = 0.f;
	SmInst.Phase = EProcessPhase::Cancelled;
	Inst.bSnapshotDirty = true;
}

void SmelterFinalizeCancel(
	flecs::entity Station,
	FCraftingStationInstance& Inst,
	FSmelterInstance& SmInst,
	const FCraftingSlots& Slots)
{
	UnlockStationSlots(Station, Slots, ESlotRoleLockMask::MaterialInputAndFuel);
	SmInst.Phase = EProcessPhase::Idle;
	Inst.bSnapshotDirty = true;

	const FCraftingStationStatic* Static = Station.try_get<FCraftingStationStatic>();
	UE_LOG(LogCrafting, Log,
		TEXT("[Smelter] '%s' Cancelled → Idle (locks released)"),
		Static ? *Static->StationName.ToString() : TEXT("?"));
}

void SmelterComplete(
	flecs::entity Station,
	const FCraftingStationStatic& Static,
	FCraftingStationInstance& Inst,
	FSmelterInstance& SmInst,
	const FCraftingSlots& Slots)
{
	const UFlecsCraftingRecipeDef* Recipe = Inst.MatchedRecipe;
	if (!Recipe)
	{
		UE_LOG(LogCrafting, Error,
			TEXT("[Smelter] '%s' COMPLETE but MatchedRecipe is null — discarding ledger, returning Idle"),
			*Static.StationName.ToString());
		SmInst.ConsumedLedger.Reset();
		UnlockStationSlots(Station, Slots, ESlotRoleLockMask::MaterialInputAndFuel);
		SmInst.Phase = EProcessPhase::Idle;
		SmInst.ProgressSeconds = 0.f;
		Inst.bSnapshotDirty = true;
		return;
	}

	// Find first Output slot (Phase 3 — first wins; multi-output containers handled later).
	int32 OutputSlotProfileIdx = INDEX_NONE;
	if (Static.Profile)
	{
		const TArray<FSlotLayoutDef>& Layout = Static.Profile->SlotLayout;
		const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);
		for (int32 i = 0; i < SlotCount; ++i)
		{
			if (Layout[i].Role == ESlotRole::Output)
			{
				OutputSlotProfileIdx = i;
				break;
			}
		}
	}
	const int64 OutputSlotEntId =
		(OutputSlotProfileIdx != INDEX_NONE) ? Slots.SlotEntityIds[OutputSlotProfileIdx] : 0;

	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;

	for (const FCraftingOutput& Out : Recipe->Outputs)
	{
		UFlecsEntityDefinition* Def = Out.ResolvedDefinition.Get();
		if (!Def)
		{
			UE_LOG(LogCrafting, Error,
				TEXT("[Smelter] '%s' COMPLETE skipping output — null ResolvedDefinition"),
				*Static.StationName.ToString());
			continue;
		}

		int32 Added = 0;
		bool bSlotOk = false;
		if (OutputSlotEntId != 0 && Sub)
		{
			Added = UFlecsContainerLibrary::AddItemToContainerFromStation(
				Sub, OutputSlotEntId, Def, Out.Count, /*bAutoStack=*/true);
			bSlotOk = (Added >= Out.Count);
		}

		if (!bSlotOk)
		{
			const int32 RemainingToDrop = Out.Count - Added;
			DropOverflowToFloor(Station, Def, RemainingToDrop);
			UE_LOG(LogCrafting, Warning,
				TEXT("[Smelter] '%s' OUTPUT %dx '%s' overflow → floor drop (slot took %d)"),
				*Static.StationName.ToString(),
				RemainingToDrop, *Def->GetName(), Added);
		}
		else
		{
			UE_LOG(LogCrafting, Log,
				TEXT("[Smelter] '%s' OUTPUT %dx '%s' → output slot"),
				*Static.StationName.ToString(),
				Out.Count, *Def->GetName());
		}
	}

	SmInst.ConsumedLedger.Reset();
	UnlockStationSlots(Station, Slots, ESlotRoleLockMask::MaterialInputAndFuel);
	SmInst.Phase = EProcessPhase::Idle;
	SmInst.ProgressSeconds = 0.f;
	Inst.bSnapshotDirty = true;
}

} // namespace FlecsCraftingRuntime
