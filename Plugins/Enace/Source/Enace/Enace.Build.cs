// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

using UnrealBuildTool;

public class Enace : ModuleRules
{
	public Enace(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",

			// Artillery/Barrage integration
			"SkeletonKey",  // For FSkeletonKey, libcuckoo
			"Barrage",      // For physics bodies
			"ArtilleryRuntime"  // For tags, rendering
		});
	}
}
