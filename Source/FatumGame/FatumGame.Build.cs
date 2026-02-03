// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FatumGame : ModuleRules
{
	public FatumGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				"FatumGame",
				"FatumGame/Flecs/Subsystem",
				"FatumGame/Flecs/Components",
				"FatumGame/Flecs/Definitions",
				"FatumGame/Flecs/Spawner",
				"FatumGame/Flecs/Character",
				"FatumGame/Flecs/Library"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"EnhancedInput",
				// Artillery ecosystem
				"ArtilleryRuntime",
				"Barrage",
				"SkeletonKey",
				"Cabling",
				// ECS (Flecs integration)
				"UnrealFlecs",
				"FlecsLibrary",
				"FlecsBarrage",
				"SolidMacros",
				// Gameplay systems
				"GameplayTags",
			}
		);
	}
}
