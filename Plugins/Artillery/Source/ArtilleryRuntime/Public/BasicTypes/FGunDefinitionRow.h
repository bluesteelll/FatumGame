#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Engine/DataTable.h"

#include "FGunDefinitionRow.generated.h"

USTRUCT(BlueprintType)
struct FGunDefinitionRow : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString GunDefinitionId;
	
	//these could BOTH be true. enjoy that, I guess. it's not implemented though.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	bool IsCPP;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	bool IsBP;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString LoadableCPP;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString LoadableBP; 

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString ProjectileDefinitionID; 

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString PreFireAbility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString PreFireCosmeticAbility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString FireAbility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString FireCosmeticAbility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString PostFireAbility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString PostFireCosmeticAbility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	FString FailureCosmeticAbility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	int32 BaseDamage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	int32 BaseRange;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	int32 BaseRateOfFire;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	int32 BaseRecoil;
	
	//Unsure at this point in implementation if this value will always be respected.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GunDefinition)
	E_ArtilleryIntents IntendedRegistrationPattern;
};
