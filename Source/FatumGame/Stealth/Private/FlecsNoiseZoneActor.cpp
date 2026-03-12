// AFlecsNoiseZoneActor — editor visualization + Flecs entity spawn.

#include "FlecsNoiseZoneActor.h"
#include "FlecsStealthComponents.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsNoiseZoneProfile.h"
#include "Components/BoxComponent.h"

// Box colors per surface type
static FColor GetZoneColor(ESurfaceNoise Type)
{
	switch (Type)
	{
	case ESurfaceNoise::Quiet:    return FColor(0, 200, 255);   // cyan
	case ESurfaceNoise::Normal:   return FColor(0, 255, 100);   // green
	case ESurfaceNoise::Loud:     return FColor(255, 165, 0);   // orange
	case ESurfaceNoise::VeryLoud: return FColor(255, 50, 50);   // red
	default:                      return FColor::White;
	}
}

AFlecsNoiseZoneActor::AFlecsNoiseZoneActor()
{
	PrimaryActorTick.bCanEverTick = false;

	ZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBox"));
	RootComponent = ZoneBox;

	ZoneBox->SetBoxExtent(FVector(200.f, 200.f, 100.f));
	ZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneBox->SetHiddenInGame(true);
	ZoneBox->SetLineThickness(2.f);
}

void AFlecsNoiseZoneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!EntityDefinition || !EntityDefinition->NoiseZoneProfile) return;

	const UFlecsNoiseZoneProfile* Profile = EntityDefinition->NoiseZoneProfile;
	ZoneBox->SetBoxExtent(Profile->Extent);
	ZoneBox->ShapeColor = GetZoneColor(Profile->SurfaceType);
}

void AFlecsNoiseZoneActor::BeginPlay()
{
	Super::BeginPlay();

	checkf(EntityDefinition, TEXT("AFlecsNoiseZoneActor [%s]: EntityDefinition is null!"), *GetName());
	checkf(EntityDefinition->NoiseZoneProfile,
		TEXT("AFlecsNoiseZoneActor [%s]: EntityDefinition has no NoiseZoneProfile!"), *GetName());

	FEntitySpawnRequest Request = FEntitySpawnRequest::FromDefinition(EntityDefinition, GetActorLocation());
	const FSkeletonKey Key = UFlecsEntityLibrary::SpawnEntity(this, Request);

	checkf(Key.IsValid(), TEXT("AFlecsNoiseZoneActor [%s]: Failed to spawn noise zone entity!"), *GetName());

	UE_LOG(LogStealth, Log, TEXT("AFlecsNoiseZoneActor [%s]: Spawned Key=%llu at %s, Extent=%s, Surface=%d"),
		*GetName(), static_cast<uint64>(Key),
		*GetActorLocation().ToString(),
		*EntityDefinition->NoiseZoneProfile->Extent.ToString(),
		static_cast<int32>(EntityDefinition->NoiseZoneProfile->SurfaceType));

	Destroy();
}
