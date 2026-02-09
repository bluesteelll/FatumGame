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
					"FatumGame/Flecs/Library",
				"FatumGame/Flecs/UI"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"EnhancedInput",
				// Physics + Identity
				"Barrage",
				"SkeletonKey",
				// ECS (Flecs integration)
				"UnrealFlecs",
				"FlecsLibrary",
				"FlecsBarrage",
				"SolidMacros",
				// Gameplay systems
				"GameplayTags",
				// UI
				"UMG",
				"Slate",
				"SlateCore",
			}
		);
	}
}
