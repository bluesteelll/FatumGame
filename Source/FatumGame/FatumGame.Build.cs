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
				// Domain-based structure (Public headers)
				"FatumGame/Core/Public",
				"FatumGame/Core/Public/Components",
				"FatumGame/Definitions/Public",
				"FatumGame/Weapon/Public",
				"FatumGame/Weapon/Public/Components",
				"FatumGame/Weapon/Public/Library",
				"FatumGame/Movement/Public",
				"FatumGame/Movement/Public/Components",
				"FatumGame/Character/Public",
				"FatumGame/Abilities/Public",
				"FatumGame/Abilities/Public/Components",
				"FatumGame/Destructible/Public",
				"FatumGame/Destructible/Public/Components",
				"FatumGame/Destructible/Public/Library",
				"FatumGame/Door/Public",
				"FatumGame/Door/Public/Components",
				"FatumGame/Item/Public",
				"FatumGame/Item/Public/Components",
				"FatumGame/Item/Public/Library",
				"FatumGame/Interaction/Public",
				"FatumGame/Interaction/Public/Library",
				"FatumGame/Spawning/Public",
				"FatumGame/Spawning/Public/Library",
				"FatumGame/Rendering/Public",
				"FatumGame/UI/Public",
				"FatumGame/Input/Public",
				"FatumGame/Utils/Public",
				"FatumGame/Climbing/Public",
				"FatumGame/Climbing/Public/Components",
				"FatumGame/Stealth/Public",
				"FatumGame/Stealth/Public/Components"
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"FatumGame/Definitions/Private",
				"FatumGame/Core/Private",
				"FatumGame/Core/Private/Components",
				"FatumGame/Weapon/Private",
				"FatumGame/Weapon/Private/Components",
				"FatumGame/Weapon/Private/Library",
				"FatumGame/Weapon/Private/Systems",
				"FatumGame/Movement/Private",
				"FatumGame/Movement/Private/Components",
				"FatumGame/Character/Private",
				"FatumGame/Abilities/Private",
				"FatumGame/Abilities/Private/Components",
				"FatumGame/Destructible/Private",
				"FatumGame/Destructible/Private/Library",
				"FatumGame/Destructible/Private/Systems",
				"FatumGame/Door/Private",
				"FatumGame/Door/Private/Components",
				"FatumGame/Door/Private/Systems",
				"FatumGame/Item/Private",
				"FatumGame/Item/Private/Components",
				"FatumGame/Item/Private/Library",
				"FatumGame/Item/Private/Systems",
				"FatumGame/Interaction/Private",
				"FatumGame/Interaction/Private/Library",
				"FatumGame/Spawning/Private",
				"FatumGame/Spawning/Private/Library",
				"FatumGame/Rendering/Private",
				"FatumGame/UI/Private",
				"FatumGame/Input/Private",
				"FatumGame/Utils/Private",
				"FatumGame/Climbing/Private",
				"FatumGame/Stealth/Private",
				"FatumGame/Stealth/Private/Components",
				"FatumGame/Stealth/Private/Systems"
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
