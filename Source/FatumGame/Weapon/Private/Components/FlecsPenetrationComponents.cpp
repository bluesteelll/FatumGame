// FromProfile() factory methods for penetration components.
// FSurfaceIntegrity implementation (DegradeWithSpread, InitFromAABB).

#include "FlecsPenetrationComponents.h"
#include "FlecsProjectileProfile.h"
#include "FlecsPhysicsProfile.h"

FPenetrationStatic FPenetrationStatic::FromProfile(const UFlecsProjectileProfile* Profile)
{
	check(Profile);

	FPenetrationStatic S;
	S.PenetrationBudget = Profile->PenetrationBudget;
	S.MaxPenetrations = Profile->MaxPenetrations;
	S.DamageFalloffFactor = Profile->PenetrationDamageFalloff;
	S.VelocityFalloffFactor = Profile->PenetrationVelocityFalloff;
	S.RicochetCosAngleThreshold = FMath::Cos(FMath::DegreesToRadians(Profile->PenetrationRicochetAngleDeg));
	S.ImpulseTransferFactor = Profile->PenetrationImpulseTransfer;
	return S;
}

FPenetrationMaterial FPenetrationMaterial::FromProfile(const UFlecsPhysicsProfile* Profile)
{
	check(Profile);

	FPenetrationMaterial M;

	// Prefer new MaterialCategory enum; fall back to legacy MaterialResistance float
	if (Profile->MaterialCategory != EPenetrationMaterialCategory::Impenetrable)
	{
		M.MaterialCategory = Profile->MaterialCategory;
	}
	else if (Profile->MaterialResistance > 0.f)
	{
		// Legacy path: find closest category from resistance value
		// This preserves backwards compatibility with existing data assets
		float BestDist = FLT_MAX;
		EPenetrationMaterialCategory BestCat = EPenetrationMaterialCategory::Wood_Thin;
		for (uint8 i = 1; i < 13; ++i)  // Skip Impenetrable(0)
		{
			const float Dist = FMath::Abs(GMaterialTable[i].Resistance - Profile->MaterialResistance);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestCat = static_cast<EPenetrationMaterialCategory>(i);
			}
		}
		M.MaterialCategory = BestCat;
	}
	// else: stays Impenetrable (caller should not add component)

	M.RicochetCosAngleThreshold = FMath::Cos(FMath::DegreesToRadians(Profile->PenetrationRicochetAngleDeg));
	M.bDegradable = Profile->bDegradable;
	M.BaseDegradeRate = Profile->BaseDegradeRate;
	M.DegradeSpreadFactor = Profile->DegradeSpreadFactor;
	M.GridCols = Profile->SurfaceGridCols;
	M.GridRows = Profile->SurfaceGridRows;
	return M;
}

// ═══════════════════════════════════════════════════════════════
// FSurfaceIntegrity
// ═══════════════════════════════════════════════════════════════

void FSurfaceIntegrity::DegradeWithSpread(int32 CenterIdx, float NormalizedAmount, float SpreadFactor)
{
	const uint16 CenterLoss = static_cast<uint16>(FMath::Min(NormalizedAmount * 65535.f, 65535.f));
	DegradeCell(CenterIdx, CenterLoss);

	const uint16 SpreadLoss = static_cast<uint16>(CenterLoss * SpreadFactor);
	if (SpreadLoss == 0) return;

	const int32 Col = CenterIdx & 7;
	const int32 Row = CenterIdx >> 3;

	// 4 direct neighbors (cardinal)
	if (Col > 0)              DegradeCell(CenterIdx - 1, SpreadLoss);
	if (Col < ActiveCols - 1) DegradeCell(CenterIdx + 1, SpreadLoss);
	if (Row > 0)              DegradeCell(CenterIdx - 8, SpreadLoss);
	if (Row < ActiveRows - 1) DegradeCell(CenterIdx + 8, SpreadLoss);
}

void FSurfaceIntegrity::InitFromAABB(const FVector& AABBMin, const FVector& AABBMax, uint8 DesiredCols, uint8 DesiredRows)
{
	const FVector Size = AABBMax - AABBMin;

	// Auto-detect projection: thinnest axis = depth (perpendicular to surface)
	float Axes[3] = { static_cast<float>(Size.X), static_cast<float>(Size.Y), static_cast<float>(Size.Z) };
	int32 ThinAxis = 0;
	if (Axes[1] < Axes[ThinAxis]) ThinAxis = 1;
	if (Axes[2] < Axes[ThinAxis]) ThinAxis = 2;

	float ExtentU, ExtentV;
	switch (ThinAxis)
	{
		case 1: // Y thinnest -> project on XZ (typical wall facing Y)
			Projection = EGridProjection::XZ;
			GridOriginU = static_cast<float>(AABBMin.X);
			GridOriginV = static_cast<float>(AABBMin.Z);
			ExtentU = Axes[0]; ExtentV = Axes[2];
			break;
		case 0: // X thinnest -> project on YZ
			Projection = EGridProjection::YZ;
			GridOriginU = static_cast<float>(AABBMin.Y);
			GridOriginV = static_cast<float>(AABBMin.Z);
			ExtentU = Axes[1]; ExtentV = Axes[2];
			break;
		default: // Z thinnest -> project on XY (floor/ceiling)
			Projection = EGridProjection::XY;
			GridOriginU = static_cast<float>(AABBMin.X);
			GridOriginV = static_cast<float>(AABBMin.Y);
			ExtentU = Axes[0]; ExtentV = Axes[1];
			break;
	}

	// Calculate grid resolution from target cell size (~40cm)
	constexpr float TargetCellSize = 40.f;
	ActiveCols = DesiredCols > 0 ? DesiredCols :
		FMath::Clamp(static_cast<uint8>(FMath::CeilToInt(ExtentU / TargetCellSize)), uint8(1), uint8(8));
	ActiveRows = DesiredRows > 0 ? DesiredRows :
		FMath::Clamp(static_cast<uint8>(FMath::CeilToInt(ExtentV / TargetCellSize)), uint8(1), uint8(8));

	InvCellSizeU = (ExtentU > 0.01f) ? static_cast<float>(ActiveCols) / ExtentU : 1.f;
	InvCellSizeV = (ExtentV > 0.01f) ? static_cast<float>(ActiveRows) / ExtentV : 1.f;

	// Initialize all cells to max integrity
	for (int32 i = 0; i < 64; ++i)
		Integrity[i] = 65535;
}
