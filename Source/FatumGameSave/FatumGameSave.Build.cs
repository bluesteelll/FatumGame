// FatumGameSave — Save/Load subsystem for FatumGame.
//
// Owns FFlecsSaveSubsystem (UGameInstanceSubsystem) + file I/O + snapshot writer/reader.
// Depends on FatumGame for FSimulationWorker access (sequence-counter fence).

using UnrealBuildTool;

public class FatumGameSave : ModuleRules
{
	public FatumGameSave(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Phase 2 — encoder folder must be reachable from public headers via "Encoders/...".
		PublicIncludePaths.AddRange(new string[]
		{
			"FatumGameSave/Public",
			"FatumGameSave/Public/Encoders",
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// Game module — for FSimulationWorker, UFlecsArtillerySubsystem, all
			// component headers we encode/decode (Health, Movement, Interaction, etc.)
			"FatumGame",

			// Physics + ECS — encoders include FlecsBarrageComponents.h (FBarrageBody,
			// FTagCollision*) and Flecs entity iteration.
			"Barrage",
			"SkeletonKey",
			"FlecsLibrary",
			"UnrealFlecs",
			"SolidMacros",
			"FlecsBarrage",

			// Gameplay tags — referenced from saved component payloads (FDamageType etc.) in later phases.
			"GameplayTags",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});
	}
}
