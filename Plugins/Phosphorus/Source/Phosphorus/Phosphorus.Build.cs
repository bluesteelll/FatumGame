// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Phosphorus : ModuleRules
{
	public Phosphorus(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true;

		PublicIncludePaths.AddRange([
			Path.Combine(PluginDirectory, "Source/Phosphorus"),
			Path.Combine(PluginDirectory, "Source/Phosphorus/Public"),
		]);

		PrivateIncludePaths.AddRange([
			Path.Combine(PluginDirectory, "Source/Phosphorus/Private"),
		]);

		PublicDependencyModuleNames.AddRange([
			"Core",
			"CoreUObject",
			"GameplayTags",
		]);

		PrivateDependencyModuleNames.AddRange([
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
		]);

		DynamicallyLoadedModuleNames.AddRange([]);
	}
}
