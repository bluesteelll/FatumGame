
#include "FlecsLightSourceActor.h"
#include "FlecsStealthComponents.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "Components/PointLightComponent.h"

AFlecsLightSourceActor::AFlecsLightSourceActor()
{
	PrimaryActorTick.bCanEverTick = false;

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PointLight"));
	RootComponent = PointLight;
}

void AFlecsLightSourceActor::BeginPlay()
{
	Super::BeginPlay();

	checkf(LightDefinition, TEXT("AFlecsLightSourceActor [%s]: LightDefinition is null! Set it in the editor."), *GetName());

	// Spawn Flecs stealth light entity via unified API
	FEntitySpawnRequest Request = FEntitySpawnRequest::FromDefinition(
		LightDefinition,
		GetActorLocation(),
		GetActorRotation()
	);

	SpawnedEntityKey = UFlecsEntityLibrary::SpawnEntity(this, Request);

	checkf(SpawnedEntityKey.IsValid(),
		TEXT("AFlecsLightSourceActor [%s]: Failed to spawn stealth light entity!"), *GetName());

	UE_LOG(LogStealth, Log, TEXT("AFlecsLightSourceActor [%s]: Spawned stealth light Key=%llu at %s"),
		*GetName(), static_cast<uint64>(SpawnedEntityKey), *GetActorLocation().ToString());
}
