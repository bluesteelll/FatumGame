// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FatumGame : ModuleRules
{
	public FatumGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				"FatumGame"
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
				"SolidMacros",
				// Gameplay systems
				"GameplayTags",
			}
		);
	}
}
