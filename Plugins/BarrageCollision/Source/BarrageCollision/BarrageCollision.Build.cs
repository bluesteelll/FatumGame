// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class BarrageCollision : ModuleRules
{
	public BarrageCollision(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true;

		PublicIncludePaths.AddRange([
			Path.Combine(PluginDirectory, "Source/BarrageCollision/Public"),
		]);

		PrivateIncludePaths.AddRange([
			Path.Combine(PluginDirectory, "Source/BarrageCollision/Private"),
		]);

		PublicDependencyModuleNames.AddRange([
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			// Event dispatch framework
			"Phosphorus",
			// Artillery ecosystem
			"Barrage",
			"SkeletonKey",
			"ArtilleryRuntime",
		]);

		PrivateDependencyModuleNames.AddRange([
			"Core",
			"CoreUObject",
			"Engine",
		]);
	}
}
