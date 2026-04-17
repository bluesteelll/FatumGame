// FlecsBulletHitLibrary — shared bullet-hit resolution for projectile and hitscan paths.

#include "Library/FlecsBulletHitLibrary.h"

#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "IsolatedJoltIncludes.h"

#include "FlecsBarrageComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsPenetrationComponents.h"
#include "FlecsDestructibleComponents.h"
#include "FlecsGameTags.h"

// Helper: lazy-init surface integrity grid for a target if needed. Returns Grid pointer and
// sets OutBodyPos/OutBodyRot + OutCellIdx (OutCellIdx = -1 on failure).
static FSurfaceIntegrity* EnsureSurfaceGrid(
	flecs::entity Target,
	UBarrageDispatch* Barrage,
	const FPenetrationMaterial* PenMat,
	const FVector& ImpactPoint,
	FVector& OutBodyPos,
	FQuat& OutBodyRot,
	int32& OutCellIdx)
{
	OutCellIdx = -1;
	OutBodyPos = FVector::ZeroVector;
	OutBodyRot = FQuat::Identity;

	FSurfaceIntegrity* Grid = Target.try_get_mut<FSurfaceIntegrity>();
	const FBarrageBody* TBody = Target.try_get<FBarrageBody>();
	if (!TBody || !TBody->IsValid() || !Barrage) return Grid;

	FBLet TPrim = Barrage->GetShapeRef(TBody->BarrageKey);
	if (!FBarragePrimitive::IsNotNull(TPrim)) return Grid;

	OutBodyPos = FVector(FBarragePrimitive::GetPosition(TPrim));
	OutBodyRot = FQuat(FBarragePrimitive::OptimisticGetAbsoluteRotation(TPrim));

	if (!Grid)
	{
		JPH::Vec3 JMin, JMax;
		if (!Barrage->GetBodyWorldBoundsJolt(TPrim->KeyIntoBarrage, JMin, JMax))
			return nullptr;

		const FVector AMin(JMin.GetX() * 100.f, JMin.GetZ() * 100.f, JMin.GetY() * 100.f);
		const FVector AMax(JMax.GetX() * 100.f, JMax.GetZ() * 100.f, JMax.GetY() * 100.f);
		const FVector LMin = OutBodyRot.UnrotateVector(AMin - OutBodyPos);
		const FVector LMax = OutBodyRot.UnrotateVector(AMax - OutBodyPos);
		const FVector TrueMin(FMath::Min(LMin.X, LMax.X), FMath::Min(LMin.Y, LMax.Y), FMath::Min(LMin.Z, LMax.Z));
		const FVector TrueMax(FMath::Max(LMin.X, LMax.X), FMath::Max(LMin.Y, LMax.Y), FMath::Max(LMin.Z, LMax.Z));

		FSurfaceIntegrity NewGrid;
		const uint8 GCols = PenMat ? PenMat->GridCols : 0;
		const uint8 GRows = PenMat ? PenMat->GridRows : 0;
		NewGrid.InitFromAABB(TrueMin, TrueMax, GCols, GRows);
		Target.set<FSurfaceIntegrity>(NewGrid);
		Grid = Target.try_get_mut<FSurfaceIntegrity>();
	}

	if (Grid)
	{
		OutCellIdx = Grid->WorldToCell(ImpactPoint, OutBodyPos, OutBodyRot);
	}
	return Grid;
}

// Compute ray-AABB slab thickness (entry/exit distances along IncomingDir from EntryPoint).
// Returns physical thickness in cm (>=0.1), sets OutExitPoint to exit intersection.
static float ComputeSlabThickness(
	const FVector& EntryPoint, const FVector& IncomingDir,
	const FVector& AABBMin, const FVector& AABBMax,
	FVector& OutExitPoint)
{
	float tMin = -1e10f, tMax = 1e10f;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const float Dir = IncomingDir[Axis];
		const float Origin = EntryPoint[Axis];
		if (FMath::Abs(Dir) > 1e-6f)
		{
			const float InvDir = 1.f / Dir;
			float t0 = (AABBMin[Axis] - Origin) * InvDir;
			float t1 = (AABBMax[Axis] - Origin) * InvDir;
			if (t0 > t1) Swap(t0, t1);
			tMin = FMath::Max(tMin, t0);
			tMax = FMath::Min(tMax, t1);
		}
	}
	if (tMax > tMin && tMax > 0.f)
	{
		const float Entry = FMath::Max(tMin, 0.f);
		OutExitPoint = EntryPoint + IncomingDir * tMax;
		return FMath::Max(tMax - Entry, 0.1f);
	}
	OutExitPoint = EntryPoint + IncomingDir * 5.f;
	return 5.f;
}

FBulletHitResult UFlecsBulletHitLibrary::ApplyBulletHit(
	flecs::world& World,
	UBarrageDispatch* Barrage,
	flecs::entity ShooterOrProjectile,
	flecs::entity Target,
	const FBulletHitContext& Ctx)
{
	FBulletHitResult Out;

	// 1) Validate
	if (!Target.is_valid() || !Target.is_alive() || Target.has<FTagDead>())
		return Out;

	// 2) Owner self-damage check (via FProjectileInstance owner, if present)
	if (ShooterOrProjectile.is_valid())
	{
		if (const FProjectileInstance* ProjInst = ShooterOrProjectile.try_get<FProjectileInstance>())
		{
			if (ProjInst->IsOwnedBy(Ctx.TargetEntityId))
				return Out;
		}
	}
	// Hitscan: shooter supplied directly
	if (Ctx.ShooterEntityId != 0 && Ctx.ShooterEntityId == Ctx.TargetEntityId)
		return Out;

	// 3) Material / incidence data
	const FPenetrationMaterial* TargetPenMat = Target.try_get<FPenetrationMaterial>();

	FVector SurfaceNormal = Ctx.ImpactNormal;
	FVector IncomingDir = Ctx.IncomingDir;
	if (!IncomingDir.IsNearlyZero())
		IncomingDir = IncomingDir.GetSafeNormal();
	if (!SurfaceNormal.IsNearlyZero() && FVector::DotProduct(IncomingDir, SurfaceNormal) > 0.f)
		SurfaceNormal = -SurfaceNormal;

	const float CosAngle = SurfaceNormal.IsNearlyZero() || IncomingDir.IsNearlyZero()
		? 1.f
		: FMath::Abs(FVector::DotProduct(IncomingDir, SurfaceNormal));

	// 4) Distance falloff
	float DistanceFalloff = 1.f;
	if (!Ctx.SpawnPosition.IsZero() && Ctx.InvFalloffRange > 0.f)
	{
		const float Dist = static_cast<float>((Ctx.ImpactPoint - Ctx.SpawnPosition).Size());
		const float OverStart = Dist - Ctx.DamageFalloffStart;
		if (OverStart > 0.f)
		{
			const float T = FMath::Clamp(OverStart * Ctx.InvFalloffRange, 0.f, 1.f);
			DistanceFalloff = FMath::Lerp(1.f, Ctx.MinDamageMultiplier, T);
		}
	}

	// 5) Structural (vs destructibles)
	float BaseDamage = Ctx.BaseDamage;
	if (Ctx.StructuralDamage > 0.f && Target.has<FDestructibleStatic>())
		BaseDamage = Ctx.StructuralDamage;

	// 6) Crit
	const bool bCritical = (Ctx.CritChance > 0.f && FMath::FRand() < Ctx.CritChance);
	const float CritFactor = bCritical ? Ctx.CritMultiplier : 1.f;

	// 7) Penetration multiplier (compounds from FPenetrationInstance)
	float PenMultiplier = 1.f;
	if (Ctx.PenStatic && Ctx.InOutCurrentDamageMultiplier)
		PenMultiplier = *Ctx.InOutCurrentDamageMultiplier;

	// 8) Thickness / effective resistance / penetrate decision
	bool bWillPenetrate = false;
	float EffectiveThickness = 0.f;
	float PhysicalThickness = 5.f;
	FVector ExitPoint = Ctx.ImpactPoint + IncomingDir * PhysicalThickness;
	float DegradeResistance = TargetPenMat ? TargetPenMat->GetResistance() : 999.f;

	// Diagnostic: material category (None if no FPenetrationMaterial component)
	const FString MatCategoryName = TargetPenMat
		? FString::Printf(TEXT("Cat=%d Res=%.2f"), (int32)TargetPenMat->MaterialCategory, TargetPenMat->GetResistance())
		: FString(TEXT("NO-MATERIAL (impenetrable)"));

	UE_LOG(LogTemp, Warning,
		TEXT("[PEN-DBG] target=%llu %s | cosAngle=%.3f (incidence) | PenStatic=%s Budget=%.2f penCount=%d/%d"),
		(uint64)Target.id(), *MatCategoryName, CosAngle,
		Ctx.PenStatic ? TEXT("OK") : TEXT("NULL"),
		(Ctx.PenStatic && Ctx.InOutRemainingBudget) ? *Ctx.InOutRemainingBudget : -1.f,
		(Ctx.PenStatic && Ctx.InOutPenetrationCount) ? *Ctx.InOutPenetrationCount : -1,
		Ctx.PenStatic ? Ctx.PenStatic->MaxPenetrations : -1);

	if (Ctx.PenStatic && Ctx.InOutRemainingBudget && Ctx.InOutPenetrationCount
		&& TargetPenMat && TargetPenMat->GetResistance() < 900.f)
	{
		const bool MaxAllowed = (Ctx.PenStatic->MaxPenetrations < 0)
			|| (*Ctx.InOutPenetrationCount < Ctx.PenStatic->MaxPenetrations);
		const bool bBudgetOk = *Ctx.InOutRemainingBudget > 0.01f;
		const float RicochetThreshold = FMath::Max(
			Ctx.PenStatic->RicochetCosAngleThreshold,
			TargetPenMat->RicochetCosAngleThreshold);

		if (CosAngle < RicochetThreshold)
		{
			Out.bRicocheted = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[PEN-DBG]   → RICOCHET (cosAngle %.3f < threshold %.3f, too oblique)"),
				CosAngle, RicochetThreshold);
		}
		else if (!MaxAllowed)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PEN-DBG]   → STOP (MaxPenetrations %d reached)"),
				Ctx.PenStatic->MaxPenetrations);
		}
		else if (!bBudgetOk)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PEN-DBG]   → STOP (budget exhausted: %.3f)"),
				*Ctx.InOutRemainingBudget);
		}
		else if (MaxAllowed && bBudgetOk && Barrage)
		{
			// Fetch AABB
			FVector AMin = Ctx.CachedAABBMin, AMax = Ctx.CachedAABBMax;
			if (AMin.IsZero() && AMax.IsZero())
			{
				const FBarrageBody* TBody = Target.try_get<FBarrageBody>();
				if (TBody && TBody->IsValid())
				{
					FBLet TPrim = Barrage->GetShapeRef(TBody->BarrageKey);
					JPH::Vec3 JMin, JMax;
					if (FBarragePrimitive::IsNotNull(TPrim)
						&& Barrage->GetBodyWorldBoundsJolt(TPrim->KeyIntoBarrage, JMin, JMax))
					{
						AMin = FVector(JMin.GetX()*100.f, JMin.GetZ()*100.f, JMin.GetY()*100.f);
						AMax = FVector(JMax.GetX()*100.f, JMax.GetZ()*100.f, JMax.GetY()*100.f);
					}
				}
			}
			if (!(AMin.IsZero() && AMax.IsZero()))
			{
				PhysicalThickness = ComputeSlabThickness(Ctx.ImpactPoint, IncomingDir, AMin, AMax, ExitPoint);
			}

			// Integrity-adjusted resistance
			float EffRes = TargetPenMat->GetResistance();
			{
				FVector BodyPos; FQuat BodyRot; int32 CellIdx;
				FSurfaceIntegrity* Grid = EnsureSurfaceGrid(Target, Barrage, TargetPenMat, Ctx.ImpactPoint, BodyPos, BodyRot, CellIdx);
				if (Grid && CellIdx >= 0)
				{
					const float LocInt = Grid->GetIntegrity(CellIdx);
					EffRes = TargetPenMat->GetResistance() * IntegrityToResistance(LocInt);
				}
			}
			EffectiveThickness = PhysicalThickness * EffRes / FMath::Max(CosAngle, 0.1f);
			bWillPenetrate = (EffectiveThickness < *Ctx.InOutRemainingBudget);
			DegradeResistance = TargetPenMat->GetResistance();

			UE_LOG(LogTemp, Warning,
				TEXT("[PEN-DBG]   physThick=%.2fcm × effRes=%.2f / cosAngle=%.3f = effThick=%.2fcm vs budget=%.2f → penetrate=%d"),
				PhysicalThickness, EffRes, FMath::Max(CosAngle, 0.1f),
				EffectiveThickness, *Ctx.InOutRemainingBudget, bWillPenetrate ? 1 : 0);
		}
	}
	else
	{
		// Penetration system inactive for this hit — either no PenStatic, no material, or
		// material is impenetrable (resistance >= 900). Target always stops the blade/bullet.
		UE_LOG(LogTemp, Warning,
			TEXT("[PEN-DBG]   → STOP (penetration inactive: %s%s)"),
			(!Ctx.PenStatic || !Ctx.InOutRemainingBudget || !Ctx.InOutPenetrationCount)
				? TEXT("no-pen-state") : TEXT(""),
			(!TargetPenMat || TargetPenMat->GetResistance() >= 900.f)
				? TEXT(" impenetrable") : TEXT(""));
	}

	// 9) Surface degradation (every hit that can degrade)
	if (Ctx.bCanDegrade && TargetPenMat && TargetPenMat->bDegradable
		&& TargetPenMat->GetResistance() < 900.f && Barrage)
	{
		FVector BodyPos; FQuat BodyRot; int32 CellIdx;
		FSurfaceIntegrity* Grid = EnsureSurfaceGrid(Target, Barrage, TargetPenMat, Ctx.ImpactPoint, BodyPos, BodyRot, CellIdx);
		if (Grid && CellIdx >= 0)
		{
			const float DegradeRate = (TargetPenMat->BaseDegradeRate != 0.08f)
				? TargetPenMat->BaseDegradeRate
				: GetDegradeRateForCategory(TargetPenMat->MaterialCategory);
			const float RefDamage = FMath::Max(BaseDamage, 1.f);
			float NormDegrade = DegradeRate * (RefDamage / 25.f);
			if (bWillPenetrate) NormDegrade *= 0.5f;
			Grid->DegradeWithSpread(CellIdx, NormDegrade, TargetPenMat->DegradeSpreadFactor);

			// Fragmentation trigger on near-zero cell
			if (Grid->GetIntegrity(CellIdx) < 0.05f)
			{
				const FDestructibleStatic* DS = Target.try_get<FDestructibleStatic>();
				if (DS && DS->IsValid() && !Target.has<FPendingFragmentation>())
				{
					FPendingFragmentation Frag;
					Frag.ImpactPoint = Ctx.ImpactPoint;
					Frag.ImpactDirection = IncomingDir.IsNearlyZero() ? FVector::UpVector : IncomingDir;
					Frag.ImpactImpulse = FMath::Max(Ctx.IncomingSpeed * 0.3f, BaseDamage * 10.f);
					Target.set<FPendingFragmentation>(Frag);
				}
			}
		}
	}

	// 10) Final damage
	float FinalDamage = BaseDamage * DistanceFalloff * PenMultiplier * CritFactor;
	if (bWillPenetrate && Ctx.PenStatic && Ctx.InOutRemainingBudget)
	{
		const float BudgetFraction = EffectiveThickness / FMath::Max(Ctx.PenStatic->PenetrationBudget, 0.01f);
		const float PenDmgMult = FMath::Max(0.f, 1.f - BudgetFraction * Ctx.PenStatic->DamageFalloffFactor);
		FinalDamage *= PenDmgMult;
	}

	// 11) Queue damage
	if (FinalDamage > 0.f && Target.has<FHealthInstance>())
	{
		FPendingDamage& Pending = Target.obtain<FPendingDamage>();
		Pending.AddHit(FinalDamage, Ctx.ShooterEntityId != 0 ? Ctx.ShooterEntityId : ShooterOrProjectile.id(),
			Ctx.DamageType, Ctx.ImpactPoint, bCritical, false);
		Target.modified<FPendingDamage>();
		Out.AppliedDamage = FinalDamage;
	}

	// 12) Pending fragmentation on destructibles (for penetration path explicitly)
	if (bWillPenetrate && Ctx.PenStatic)
	{
		const FDestructibleStatic* DS = Target.try_get<FDestructibleStatic>();
		if (DS && DS->IsValid() && !Target.has<FPendingFragmentation>())
		{
			FPendingFragmentation Frag;
			Frag.ImpactPoint = Ctx.ImpactPoint;
			Frag.ImpactDirection = IncomingDir;
			Frag.ImpactImpulse = Ctx.IncomingSpeed * Ctx.PenStatic->ImpulseTransferFactor;
			Target.set<FPendingFragmentation>(Frag);
		}
	}

	// 13) Impulse (hitscan / melee)
	if (Ctx.bApplyImpulse && Barrage && Ctx.ImpulseStrength > 0.f)
	{
		const FBarrageBody* TBody = Target.try_get<FBarrageBody>();
		if (TBody && TBody->IsValid())
		{
			FBLet TPrim = Barrage->GetShapeRef(TBody->BarrageKey);
			if (FBarragePrimitive::IsNotNull(TPrim))
			{
				const FVector Impulse = IncomingDir * (Ctx.ImpulseStrength * FMath::Max(FinalDamage, 1.f));
				Barrage->AddBodyImpulse(TPrim->KeyIntoBarrage, Impulse);
				UE_LOG(LogTemp, Warning,
					TEXT("[MELEE-DBG] ApplyBulletHit IMPULSE APPLIED: target=%llu impulse=(%s) mag=%.1f"),
					(uint64)Target.id(), *Impulse.ToString(), Impulse.Size());
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[MELEE-DBG] ApplyBulletHit IMPULSE SKIPPED: target=%llu barrage primitive null"),
					(uint64)Target.id());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[MELEE-DBG] ApplyBulletHit IMPULSE SKIPPED: target=%llu no FBarrageBody or not valid"),
				(uint64)Target.id());
		}
	}

	// 14) Update penetration instance
	Out.ExitPoint = ExitPoint;
	Out.bPenetrated = bWillPenetrate;
	Out.PostHitDamageMultiplier = PenMultiplier;

	if (bWillPenetrate && Ctx.PenStatic && Ctx.InOutRemainingBudget
		&& Ctx.InOutCurrentDamageMultiplier && Ctx.InOutPenetrationCount
		&& Ctx.InOutLastPenetratedTargetId)
	{
		const float BudgetFraction = EffectiveThickness / FMath::Max(Ctx.PenStatic->PenetrationBudget, 0.01f);
		const float PenDmgMult = FMath::Max(0.f, 1.f - BudgetFraction * Ctx.PenStatic->DamageFalloffFactor);
		*Ctx.InOutRemainingBudget -= EffectiveThickness;
		*Ctx.InOutPenetrationCount += 1;
		*Ctx.InOutCurrentDamageMultiplier *= PenDmgMult;
		*Ctx.InOutLastPenetratedTargetId = Ctx.TargetEntityId;
		Out.PostHitDamageMultiplier = *Ctx.InOutCurrentDamageMultiplier;

		UE_LOG(LogTemp, Warning,
			TEXT("[PEN-DBG]   AFTER PENETRATE: budgetLeft=%.2f penCount=%d nextHitDmgMult=%.2f (thisHit dmg=%.1f crit=%d)"),
			*Ctx.InOutRemainingBudget, *Ctx.InOutPenetrationCount,
			*Ctx.InOutCurrentDamageMultiplier, FinalDamage, bCritical ? 1 : 0);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PEN-DBG]   HIT FINAL: dmg=%.1f crit=%d penetrated=%d ricocheted=%d"),
			FinalDamage, bCritical ? 1 : 0,
			bWillPenetrate ? 1 : 0, Out.bRicocheted ? 1 : 0);
	}

	return Out;
}
