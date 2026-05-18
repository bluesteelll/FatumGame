// ACheckpointActor — minimal trigger-volume save activator.

#include "CheckpointActor.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveSubsystem.h"

#include "Components/SphereComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

ACheckpointActor::ACheckpointActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerVolume = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerVolume"));
	RootComponent = TriggerVolume;

	TriggerVolume->InitSphereRadius(TriggerRadius);
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerVolume->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerVolume->SetGenerateOverlapEvents(true);
}

void ACheckpointActor::BeginPlay()
{
	Super::BeginPlay();

	// Sync sphere radius in case it was edited after the CDO captured the default.
	check(TriggerVolume);
	TriggerVolume->SetSphereRadius(TriggerRadius);

	// Bind overlap callback. Static binding via UFUNCTION → no need to unbind on EndPlay
	// (Destroy() tears down components which clears their delegates).
	TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ACheckpointActor::OnTriggerOverlap);

	UE_LOG(LogFlecsSave, Log,
		TEXT("ACheckpointActor [%s]: armed at slot %d, radius %.0f cm, cooldown %.1f s"),
		*GetName(), TargetSaveSlot, TriggerRadius, CooldownSeconds);
}

void ACheckpointActor::OnTriggerOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!bAutoSaveOnEnter)
	{
		// Manual mode — overlap event still useful for BP hooks but we do not fire a save.
		return;
	}

	// Restrict to the local player pawn — avoid AI/companions retriggering checkpoints.
	APawn* AsPawn = Cast<APawn>(OtherActor);
	if (!AsPawn || !AsPawn->IsPlayerControlled())
	{
		return;
	}

	FireSave();
}

bool ACheckpointActor::FireSave()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("ACheckpointActor [%s]: FireSave with no World"), *GetName());
		return false;
	}

	const double Now = World->GetTimeSeconds();
	if (Now < NextAllowedSaveTime)
	{
		UE_LOG(LogFlecsSave, Verbose,
			TEXT("ACheckpointActor [%s]: cooldown active (%.2fs remaining)"),
			*GetName(), NextAllowedSaveTime - Now);
		return false;
	}

	UGameInstance* GI = World->GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("ACheckpointActor [%s]: no GameInstance"), *GetName());
		return false;
	}
	UFlecsSaveSubsystem* SaveSub = GI->GetSubsystem<UFlecsSaveSubsystem>();
	if (!SaveSub)
	{
		UE_LOG(LogFlecsSave, Warning,
			TEXT("ACheckpointActor [%s]: UFlecsSaveSubsystem not present"), *GetName());
		return false;
	}

	const ESaveResult Result = SaveSub->RequestSave(TargetSaveSlot, CheckpointName.ToString());
	UE_LOG(LogFlecsSave, Log,
		TEXT("ACheckpointActor [%s]: RequestSave(slot=%d, name='%s') -> %d"),
		*GetName(), TargetSaveSlot, *CheckpointName.ToString(), (int32)Result);

	// Update cooldown regardless of result so a busy/failed request also throttles —
	// otherwise an AlreadyInProgress save would let the overlap path spam the log.
	NextAllowedSaveTime = Now + CooldownSeconds;

	return Result == ESaveResult::Pending || Result == ESaveResult::Success;
}
