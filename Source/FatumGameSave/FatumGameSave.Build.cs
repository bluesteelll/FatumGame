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

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// Game module — for FSimulationWorker, UFlecsArtillerySubsystem.
			"FatumGame",

			// Physics + ECS — needed by encoders in later phases. Phase 1 doesn't touch ECS
			// directly but the subsystem talks to FSimulationWorker which depends on these.
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
