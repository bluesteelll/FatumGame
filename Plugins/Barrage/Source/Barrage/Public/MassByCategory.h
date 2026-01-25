#pragma once

#include "CoreMinimal.h"
#include "MassByCategory.generated.h"

//right now, this is just a thick enum wrapper, but it may get functionality as we go.
USTRUCT(BlueprintType)
struct FMassByCategory
{
	GENERATED_BODY()
	
public:
	enum BMassCategories
	{
		CharacterSelf = 1, // IN GENERAL DO NOT USE THIS. Characters are _kinematic_ and forces are managed carefully on them.
		MostEnemies = 1,
		MostScenery = 4,
		BossEnemies = 8,
		MegaEnemies = 32,
		CharacterExternal = 100,
		FunctionallyImmobile = 1000000
	};
	
	BMassCategories Category;
	
	FMassByCategory(BMassCategories MassClass)
	{
		Category = MassClass;
	}
	
	FMassByCategory()
	{
		Category = FunctionallyImmobile;
	}
};

//hey so you're probably wondering why in the hell this works this way.
//well, blueprint only supports enums that are one byte wide. That's not a haha fun joke.
//that's literally the case. so to get this to work with, you know, weights above 255,
//we actually indirect through this facade enum. :|
UENUM()
namespace EBWeightClasses {
	enum Type {
		NormalEnemy					UMETA(DisplayName = "Normal Enemy"),
		BigEnemy					UMETA(DisplayName = "Big Enemy"),
		HugeEnemy					UMETA(DisplayName = "Huge Enemy")
	};
}

namespace Weights
{
	const FMassByCategory NormalEnemy = FMassByCategory(FMassByCategory::MostEnemies);
	const FMassByCategory BigEnemy = FMassByCategory(FMassByCategory::BossEnemies);
	const FMassByCategory HugeEnemy = FMassByCategory(FMassByCategory::MegaEnemies);
}
