// FlecsNiagaraManager - Array Data Interface VFX for ECS entities

#include "FlecsNiagaraManager.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlecsNiagara, Log, All);

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsNiagaraManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UFlecsNiagaraManager::Deinitialize()
{
	// Destroy all spawned Niagara actors
	for (auto& [System, Group] : EffectGroups)
	{
		if (Group.NiagaraActor && !Group.NiagaraActor->IsActorBeingDestroyed())
		{
			Group.NiagaraActor->Destroy();
		}
	}

	// Release tracer pool host + components
	TracerPool.Reset();
	if (TracerHostActor && !TracerHostActor->IsActorBeingDestroyed())
	{
		TracerHostActor->Destroy();
	}
	TracerHostActor = nullptr;

	EffectGroups.Empty();
	EntityToEffect.Empty();

	Super::Deinitialize();
}

UFlecsNiagaraManager* UFlecsNiagaraManager::Get(UWorld* World)
{
	return World ? World->GetSubsystem<UFlecsNiagaraManager>() : nullptr;
}

// ═══════════════════════════════════════════════════════════════
// REGISTRATION API (game thread only)
// ═══════════════════════════════════════════════════════════════

void UFlecsNiagaraManager::RegisterEntity(FSkeletonKey Key, UNiagaraSystem* Effect, float Scale, FVector Offset)
{
	check(IsInGameThread());
	checkf(Effect, TEXT("NiagaraManager::RegisterEntity: null Effect for Key %llu"), static_cast<uint64>(Key));
	checkf(Key.IsValid(), TEXT("NiagaraManager::RegisterEntity: invalid Key"));

	// Skip duplicate registration (entity may already be registered from SpawnEntity path)
	if (EntityToEffect.Contains(Key))
	{
		return;
	}

	FEffectGroup& Group = GetOrCreateEffectGroup(Effect);
	Group.RegisteredKeys.Add(Key);
	EntityToEffect.Add(Key, Effect);

	UE_LOG(LogFlecsNiagara, Log, TEXT("RegisterEntity: Key=%llu Effect=%s (total=%d)"),
		static_cast<uint64>(Key), *Effect->GetName(), Group.RegisteredKeys.Num());
}

void UFlecsNiagaraManager::UnregisterEntity(FSkeletonKey Key)
{
	check(IsInGameThread());

	UNiagaraSystem** FoundEffect = EntityToEffect.Find(Key);
	if (!FoundEffect)
	{
		return;
	}

	FEffectGroup* Group = EffectGroups.Find(*FoundEffect);
	if (Group)
	{
		Group->RegisteredKeys.Remove(Key);
	}

	EntityToEffect.Remove(Key);

	UE_LOG(LogFlecsNiagara, Verbose, TEXT("UnregisterEntity: Key=%llu"), static_cast<uint64>(Key));
}

// ═══════════════════════════════════════════════════════════════
// TICK API (called by UFlecsArtillerySubsystem::Tick)
// ═══════════════════════════════════════════════════════════════

void UFlecsNiagaraManager::UpdateEffects()
{
	check(IsInGameThread());

	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return;
	}

	for (auto& [System, Group] : EffectGroups)
	{
		Group.Positions.Reset();   // keeps heap allocation
		Group.Velocities.Reset();

		for (const FSkeletonKey& Key : Group.RegisteredKeys)
		{
			FBLet Body = Physics->GetShapeRef(Key);
			if (!FBarragePrimitive::IsNotNull(Body))
			{
				continue;
			}

			FVector Pos(FBarragePrimitive::GetPosition(Body));
			if (Pos.ContainsNaN())
			{
				continue;
			}

			FVector Vel(FBarragePrimitive::GetVelocity(Body));

			Group.Positions.Add(Pos);
			Group.Velocities.Add(Vel);
		}

		if (Group.NiagaraComponent && Group.Positions.Num() > 0)
		{
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
				Group.NiagaraComponent, FName("EntityPositions"), Group.Positions);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
				Group.NiagaraComponent, FName("EntityVelocities"), Group.Velocities);
		}
	}
}

void UFlecsNiagaraManager::ProcessPendingRegistrations()
{
	check(IsInGameThread());

	FPendingNiagaraRegistration Reg;
	while (PendingRegistrations.Dequeue(Reg))
	{
		if (Reg.Effect && Reg.Key.IsValid())
		{
			RegisterEntity(Reg.Key, Reg.Effect, Reg.Scale, Reg.Offset);
		}
	}
}

void UFlecsNiagaraManager::ProcessPendingRemovals()
{
	check(IsInGameThread());

	FSkeletonKey Key;
	while (PendingRemovals.Dequeue(Key))
	{
		UnregisterEntity(Key);
	}
}

void UFlecsNiagaraManager::ProcessPendingDeathEffects()
{
	check(IsInGameThread());

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FPendingDeathEffect FX;
	while (PendingDeathEffects.Dequeue(FX))
	{
		checkf(FX.Effect, TEXT("NiagaraManager::ProcessPendingDeathEffects: null Effect"));

		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World,
			FX.Effect,
			FX.Location,
			FX.Rotation.Rotator(),
			FVector(FX.Scale),
			true,  // bAutoDestroy
			true,  // bAutoActivate
			ENCPoolMethod::None);

		UE_LOG(LogFlecsNiagara, Verbose, TEXT("DeathEffect: %s at (%.0f,%.0f,%.0f)"),
			*FX.Effect->GetName(), FX.Location.X, FX.Location.Y, FX.Location.Z);
	}
}

// ═══════════════════════════════════════════════════════════════
// MPSC API (sim thread → game thread)
// ═══════════════════════════════════════════════════════════════

void UFlecsNiagaraManager::EnqueueRegistration(const FPendingNiagaraRegistration& Reg)
{
	PendingRegistrations.Enqueue(Reg);
}

void UFlecsNiagaraManager::EnqueueRemoval(FSkeletonKey Key)
{
	PendingRemovals.Enqueue(Key);
}

void UFlecsNiagaraManager::EnqueueDeathEffect(const FPendingDeathEffect& Effect)
{
	checkf(Effect.Effect, TEXT("NiagaraManager::EnqueueDeathEffect: null Effect"));
	PendingDeathEffects.Enqueue(Effect);
}

void UFlecsNiagaraManager::EnqueueTracer(const FPendingNiagaraTracer& Tracer)
{
	// Silent-drop null effects (hitscan weapons may leave TracerEffect unset).
	if (!Tracer.Effect) return;
	PendingTracers.Enqueue(Tracer);
}

// ═══════════════════════════════════════════════════════════════
// TRACER POOL
// ═══════════════════════════════════════════════════════════════

UNiagaraComponent* UFlecsNiagaraManager::AcquireTracerSlot(double /*NowSeconds*/)
{
	for (FTracerPoolSlot& Slot : TracerPool)
	{
		if (Slot.ReleaseTimeSeconds == 0.0 && Slot.Component)
		{
			return Slot.Component;
		}
	}
	return nullptr;
}

void UFlecsNiagaraManager::ProcessPendingTracers()
{
	check(IsInGameThread());

	UWorld* World = GetWorld();
	if (!World) return;

	const double Now = FApp::GetCurrentTime();

	// Lazy-init host actor + pool on first drain when there's actually something to spawn.
	if (TracerPool.Num() == 0 && !PendingTracers.IsEmpty())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TracerHostActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		checkf(TracerHostActor, TEXT("NiagaraManager: failed to spawn tracer host actor"));

		USceneComponent* Root = NewObject<USceneComponent>(TracerHostActor, TEXT("TracerRoot"));
		TracerHostActor->SetRootComponent(Root);
		Root->RegisterComponent();

		TracerPool.Reserve(TracerPoolSize);
		for (int32 i = 0; i < TracerPoolSize; ++i)
		{
			UNiagaraComponent* Comp = NewObject<UNiagaraComponent>(TracerHostActor);
			Comp->SetAutoActivate(false);
			Comp->SetAutoDestroy(false);
			Comp->bAutoManageAttachment = false;
			Comp->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Comp->RegisterComponent();

			FTracerPoolSlot Slot;
			Slot.Component = Comp;
			Slot.ReleaseTimeSeconds = 0.0;
			TracerPool.Add(Slot);
		}
	}

	// Release expired slots.
	for (FTracerPoolSlot& Slot : TracerPool)
	{
		if (Slot.ReleaseTimeSeconds != 0.0 && Now >= Slot.ReleaseTimeSeconds && Slot.Component)
		{
			Slot.Component->Deactivate();
			Slot.ReleaseTimeSeconds = 0.0;
		}
	}

	// Drain queue.
	FPendingNiagaraTracer T;
	while (PendingTracers.Dequeue(T))
	{
		if (!T.Effect) continue;

		UNiagaraComponent* Comp = AcquireTracerSlot(Now);
		if (!Comp)
		{
			// Pool exhausted — fallback to one-shot spawn (matches death VFX path).
			UE_LOG(LogFlecsNiagara, Verbose, TEXT("Tracer pool exhausted — fallback SpawnSystemAtLocation"));
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World, T.Effect, T.Start, FRotator::ZeroRotator, FVector(1.f),
				true, true, ENCPoolMethod::None);
			continue;
		}

		Comp->SetAsset(T.Effect);
		Comp->SetWorldLocation(T.Start);
		Comp->SetVectorParameter(TEXT("BeamStart"), T.Start);
		Comp->SetVectorParameter(TEXT("BeamEnd"), T.End);
		Comp->SetFloatParameter(TEXT("BeamThickness"), T.Thickness);
		Comp->Activate(true);

		for (FTracerPoolSlot& Slot : TracerPool)
		{
			if (Slot.Component == Comp)
			{
				Slot.ReleaseTimeSeconds = Now + FMath::Max(T.Duration, 0.01f);
				break;
			}
		}
	}
}

// ═══════════════════════════════════════════════════════════════
// INTERNAL
// ═══════════════════════════════════════════════════════════════

UFlecsNiagaraManager::FEffectGroup& UFlecsNiagaraManager::GetOrCreateEffectGroup(UNiagaraSystem* Effect)
{
	check(IsInGameThread());
	checkf(Effect, TEXT("NiagaraManager::GetOrCreateEffectGroup: null Effect"));

	FEffectGroup* Existing = EffectGroups.Find(Effect);
	if (Existing)
	{
		return *Existing;
	}

	UWorld* World = GetWorld();
	checkf(World, TEXT("NiagaraManager::GetOrCreateEffectGroup: no World"));

	// Spawn ANiagaraActor at world origin — positions driven by Array DI
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ANiagaraActor* NiagaraActor = World->SpawnActor<ANiagaraActor>(
		ANiagaraActor::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);
	checkf(NiagaraActor, TEXT("NiagaraManager: failed to spawn ANiagaraActor for %s"), *Effect->GetName());

	UNiagaraComponent* NiagaraComp = NiagaraActor->GetNiagaraComponent();
	checkf(NiagaraComp, TEXT("NiagaraManager: ANiagaraActor has no NiagaraComponent"));

	NiagaraComp->SetAsset(Effect);
	NiagaraComp->Activate(true);

	FEffectGroup& Group = EffectGroups.Add(Effect);
	Group.NiagaraActor = NiagaraActor;
	Group.NiagaraComponent = NiagaraComp;

	// Pre-allocate scratch arrays
	Group.Positions.Reserve(64);
	Group.Velocities.Reserve(64);

	UE_LOG(LogFlecsNiagara, Log, TEXT("Created EffectGroup for %s"), *Effect->GetName());

	return Group;
}
