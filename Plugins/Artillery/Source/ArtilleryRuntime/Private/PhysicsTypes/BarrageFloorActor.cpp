// Simple Blueprint-friendly actor for creating Barrage floor physics

#include "PhysicsTypes/BarrageFloorActor.h"
#include "Systems/BarrageBlueprintLibrary.h"
#include "Components/BillboardComponent.h"
#include "DrawDebugHelpers.h"

#if WITH_EDITORONLY_DATA
#include "Engine/Texture2D.h"
#endif

ABarrageFloorActor::ABarrageFloorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

#if WITH_EDITORONLY_DATA
	// Visual helper in editor
	SpriteComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	SpriteComponent->SetupAttachment(RootComponent);
	SpriteComponent->bIsScreenSizeScaled = true;
	SpriteComponent->SetRelativeLocation(FVector(0, 0, 50));

	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinderOptional<UTexture2D> NoteTextureObject;
		FConstructorStatics()
			: NoteTextureObject(TEXT("/Engine/EditorResources/S_Note"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	if (ConstructorStatics.NoteTextureObject.Get())
	{
		SpriteComponent->Sprite = ConstructorStatics.NoteTextureObject.Get();
	}
#endif
}

void ABarrageFloorActor::BeginPlay()
{
	Super::BeginPlay();

	// Create floor physics using Blueprint Library
	FVector FloorCenter = GetActorLocation() + FloorOffset;

	FloorKey = UBarrageBlueprintLibrary::CreateFloorBox(
		this,
		FloorCenter,
		FloorSize.X,
		FloorSize.Y,
		FloorSize.Z
	);

	if (FloorKey.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("BarrageFloorActor '%s': Created floor physics at %s, size %s"),
			*GetName(), *FloorCenter.ToString(), *FloorSize.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BarrageFloorActor '%s': Failed to create floor physics!"), *GetName());
	}
}

#if WITH_EDITOR
void ABarrageFloorActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (bShowDebugBox)
	{
		// Draw debug box in editor to show floor extent
		FVector FloorCenter = GetActorLocation() + FloorOffset;
		FVector HalfSize = FloorSize * 0.5f;

		DrawDebugBox(
			GetWorld(),
			FloorCenter,
			HalfSize,
			FColor::Cyan,
			true,  // Persistent
			-1.0f,  // Lifetime
			0,      // Depth priority
			10.0f   // Thickness
		);
	}
}
#endif
