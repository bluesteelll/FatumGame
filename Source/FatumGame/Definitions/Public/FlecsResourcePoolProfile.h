// Data Asset defining resource pools (Mana, Stamina, etc.) for a character.
// Referenced by AFlecsCharacter, captured at entity creation → FResourcePools ECS component.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsResourcePoolProfile.generated.h"

// Blueprint-exposed resource type enum. Values MUST match EResourceTypeId in FlecsResourceTypes.h.
UENUM(BlueprintType)
enum class EResourceType : uint8
{
	None = 0,
	Mana = 1,
	Stamina = 2,
	Energy = 3,
	Rage = 4,
	MAX UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct FResourcePoolDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	EResourceType ResourceType = EResourceType::None;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"))
	float MaxValue = 100.f;

	/** Starting value. -1 = start at max. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "-1"))
	float StartingValue = -1.f;

	/** Regeneration per second. 0 = no passive regen. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float RegenRate = 0.f;

	/** Seconds after consumption before regen resumes. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float RegenDelay = 0.f;

	/** Allow regen while a channeled (sustain-cost) ability is draining this pool? */
	UPROPERTY(EditAnywhere)
	bool bRegenWhileChanneling = true;

	float GetStartingValue() const { return StartingValue < 0.f ? MaxValue : StartingValue; }
};

// Cost definition for ability data assets (Blueprint-exposed mirror of FAbilityCostEntry).
USTRUCT(BlueprintType)
struct FAbilityCostDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	EResourceType ResourceType = EResourceType::None;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float Amount = 0.f;
};

UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsResourcePoolProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Resource pools for this character. Max 4. */
	UPROPERTY(EditAnywhere, Category = "Resources", meta = (TitleProperty = "ResourceType"))
	TArray<FResourcePoolDefinition> Pools;
};
