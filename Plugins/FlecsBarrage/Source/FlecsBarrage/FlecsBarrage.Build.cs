// FlecsBarrage - Bridge between Flecs ECS and Barrage Physics

using UnrealBuildTool;

public class FlecsBarrage : ModuleRules
{
	public FlecsBarrage(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// Flecs
			"FlecsLibrary",
			"UnrealFlecs",
			"SolidMacros",

			// Barrage
			"Barrage",
			"SkeletonKey"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});
	}
}
