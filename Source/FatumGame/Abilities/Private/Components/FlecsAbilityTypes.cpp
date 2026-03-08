// Factory method for FAbilitySystem — converts UObject Data Assets to sim-safe POD.
// Game thread only (accesses UObjects).

#include "FlecsAbilityTypes.h"
#include "FlecsAbilityLoadout.h"
#include "FlecsAbilityDefinition.h"

FAbilitySystem FAbilitySystem::FromLoadout(const UFlecsAbilityLoadout* Loadout)
{
	check(Loadout);
	FAbilitySystem Result;

	int32 Count = FMath::Min(Loadout->Abilities.Num(), MAX_ABILITY_SLOTS);
	for (int32 i = 0; i < Count; ++i)
	{
		const UFlecsAbilityDefinition* Def = Loadout->Abilities[i];
		if (!Def || Def->AbilityType == EAbilityType::None) continue;

		FAbilitySlot& Slot = Result.Slots[Result.SlotCount];
		Slot.TypeId = static_cast<EAbilityTypeId>(Def->AbilityType);
		Slot.MaxCharges = static_cast<int8>(Def->MaxCharges);
		Slot.Charges = static_cast<int8>(Def->StartingCharges);
		Slot.RechargeRate = Def->RechargeRate;
		Slot.CooldownDuration = Def->CooldownDuration;
		Slot.bAlwaysTick = Def->bAlwaysTick;
		Slot.bExclusive = Def->bExclusive;

		// Copy per-ability-type config into slot buffer
		if (Def->AbilityType == EAbilityType::KineticBlast)
		{
			FMemory::Memcpy(Slot.ConfigData, &Def->KineticBlastConfig, sizeof(FKineticBlastConfig));
		}
		else if (Def->AbilityType == EAbilityType::Telekinesis)
		{
			FTelekinesisSlotData SlotData;
			SlotData.Config = &Def->TelekinesisConfig;
			SlotData.CurrentHoldDistance = Def->TelekinesisConfig.HoldDistance;
			SlotData.MinHoldDistance = Def->TelekinesisConfig.MinHoldDistance;
			SlotData.MaxHoldDistance = Def->TelekinesisConfig.MaxHoldDistance;
			SlotData.ScrollSpeed = Def->TelekinesisConfig.ScrollSpeed;
			FMemory::Memcpy(Slot.ConfigData, &SlotData, sizeof(FTelekinesisSlotData));
		}

		// Activation costs
		{
			checkf(Def->ActivationCosts.Num() <= MAX_ABILITY_COSTS,
				TEXT("Ability %s has %d ActivationCosts but MAX_ABILITY_COSTS is %d"),
				*Def->GetName(), Def->ActivationCosts.Num(), MAX_ABILITY_COSTS);
			int32 CostCount = FMath::Min(Def->ActivationCosts.Num(), static_cast<int32>(MAX_ABILITY_COSTS));
			for (int32 c = 0; c < CostCount; ++c)
			{
				const FAbilityCostDefinition& CostDef = Def->ActivationCosts[c];
				if (CostDef.ResourceType == EResourceType::None) continue;
				Slot.ActivationCosts[Slot.ActivationCostCount].ResourceType = static_cast<EResourceTypeId>(CostDef.ResourceType);
				Slot.ActivationCosts[Slot.ActivationCostCount].Amount = CostDef.Amount;
				Slot.ActivationCostCount++;
			}
		}

		// Sustain costs
		{
			checkf(Def->SustainCosts.Num() <= MAX_ABILITY_COSTS,
				TEXT("Ability %s has %d SustainCosts but MAX_ABILITY_COSTS is %d"),
				*Def->GetName(), Def->SustainCosts.Num(), MAX_ABILITY_COSTS);
			int32 CostCount = FMath::Min(Def->SustainCosts.Num(), static_cast<int32>(MAX_ABILITY_COSTS));
			for (int32 c = 0; c < CostCount; ++c)
			{
				const FAbilityCostDefinition& CostDef = Def->SustainCosts[c];
				if (CostDef.ResourceType == EResourceType::None) continue;
				Slot.SustainCosts[Slot.SustainCostCount].ResourceType = static_cast<EResourceTypeId>(CostDef.ResourceType);
				Slot.SustainCosts[Slot.SustainCostCount].Amount = CostDef.Amount;
				Slot.SustainCostCount++;
			}
		}

		Slot.DeactivationRefund = Def->DeactivationRefund;
		Result.SlotCount++;
	}

	return Result;
}
