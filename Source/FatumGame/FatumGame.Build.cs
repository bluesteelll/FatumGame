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
				// Artillery ecosystem
				"ArtilleryRuntime",
				"Barrage",
				"SkeletonKey",
				"Cabling",
				// Collision system (includes Phosphorus)
				"BarrageCollision",
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
