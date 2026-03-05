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
				// Domain-based structure
				"FatumGame/Core",
				"FatumGame/Core/Components",
				"FatumGame/Definitions",
				"FatumGame/Weapon",
				"FatumGame/Weapon/Components",
				"FatumGame/Weapon/Systems",
				"FatumGame/Weapon/Library",
				"FatumGame/Movement",
				"FatumGame/Movement/Components",
				"FatumGame/Character",
				"FatumGame/Abilities",
				"FatumGame/Abilities/Components",
				"FatumGame/Destructible",
				"FatumGame/Destructible/Components",
				"FatumGame/Destructible/Systems",
				"FatumGame/Destructible/Library",
				"FatumGame/Door",
				"FatumGame/Door/Components",
				"FatumGame/Door/Systems",
				"FatumGame/Item",
				"FatumGame/Item/Components",
				"FatumGame/Item/Systems",
				"FatumGame/Item/Library",
				"FatumGame/Interaction",
				"FatumGame/Interaction/Library",
				"FatumGame/Spawning",
				"FatumGame/Spawning/Library",
				"FatumGame/Rendering",
				"FatumGame/UI",
				"FatumGame/Input",
				"FatumGame/Utils"
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
				"FlecsUI",
				"SolidMacros",
				// Gameplay systems
				"GameplayTags",
				// UI
				"UMG",
				"Slate",
				"SlateCore",
				"CommonUI",
				// VFX
				"Niagara",
			}
		);
	}
}
