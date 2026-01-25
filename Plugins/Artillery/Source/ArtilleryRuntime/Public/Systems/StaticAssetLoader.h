#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UCablingWorldSubsystem.h"
#include "FArtilleryGun.h"
#include "StaticAssetLoader.generated.h"

//THIS IS NOT GUARANTEED TO WORK FOR NON-BLUEPRINT TYPES. IN FACT, IT PROBABLY WILL FUCKING EXPLODE.
//HAVE A NICE DAY WITH THAT LOL LMAO.
UCLASS(Blueprintable, BlueprintType)
class UStaticGunLoader : public UGameInstanceSubsystem //note game instance lifecycle. this means we do not reload when levels change.
///TODO: decide if this is good. pretty sure it is, cause we can add an explicit reload.
///pretty sure we want it to work this way, cause this is one step towards the tech that lets us have inventories open during loading screens.
///that said, I haven't done an actual feasibility assess on that in UE yet.
{
	GENERATED_BODY()
	
	inline static auto GamePath = TEXT("DataTable'/Game/DataTables/GunDefinitions.GunDefinitions'");
	//we don't really recommend using this path for long, but we ship with it because we believe you should be
	//able to run software. I k n o w I'm old fashioned.
	inline static auto EcoPath = TEXT("DataTable'/Artillery/DataTables/GunDefinitions.GunDefinitions'");
public:
	
	//If you rename this, it had better be a funnier name than this.
	TMap<FString, UScriptStruct*> ZardozMapping; //The Gun Is, For Lack Of A Better Term, Good.
	//I think we should use a skeletonkey here or a tag or SOMETHING. SOMETHING other than a raw-string as the key.
	//I don't know WHAT yet tho. I'm real anxious about this. this has to be a solved problem.
	TMap<FString, FString> CommonNameToProperNameMapping; //yea. yea ok.

//	A success might emit a set of log lines that looks a lot like this:
// 	LogTemp: Warning: GunLoader: Loading from /Script/Bristle54.GunWeeRocket
//	LogTemp: Warning: GunLoader: Loading...
//	LogTemp: Warning: GunLoader: Loaded UScriptStruct: [GunWeeRocket]
//	LogTemp: Warning: GunLoader: Initing USSI...
//	LogTemp: Warning: GunLoader: Loaded via [GunWeeRocket] with shortname: [FGunWeeRocket]
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//There are a number of ways to get around the fixed typing of the shared pointer when deriving other loaders, but I recommend
	//defactoring here and building an internal templated impl class rather than futzing around. atm, yagni tho
	virtual TSharedPtr<FArtilleryGun> GetNewInstanceUninitialized(FString RequestedGunDefinitionID);
	virtual ~UStaticGunLoader() override;
	
	UDataTable* Definitions = nullptr;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FArtilleryGun BaseTypeBinding; //we need this so that we can extract a uclass object for artillerygun

	auto AssetTable() { return GamePath; }
};

/** We'll be using this UE method. It talks about objects, but that's not UObjects, it's just... objects.
 * What's a little inobvious is that you can hand in ustruct or a parent class.
 *
 ** Find an optional object.
 * @see StaticFindObject()
 *
  template< class T >
  inline T* FindObject(FTopLevelAssetPath InPath, bool ExactClass = false)
  {
	return (T*)StaticFindObject(T::StaticClass(), InPath, ExactClass);
  }
 *
 * Other options include TStructOnScope, which is kinda a miss, since we're already getting lifecycle management from tsp
 * unless it has features that I missed in my cursory read over, or the much more interestin' FInstancedStruct. Now that's part of
 * a plugin called StructUtils, but I think that's either moved to core or we've had it on. Not sure. Anyway, if this doesn't work,
 * we have some good options.
 * anyway, today's special ingredient is datatables.
 *Allez cuisine!
 * 
 */
