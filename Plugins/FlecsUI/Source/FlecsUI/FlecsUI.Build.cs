// FlecsUI - General UI infrastructure for Flecs simulation

using UnrealBuildTool;

public class FlecsUI : ModuleRules
{
	public FlecsUI(ReadOnlyTargetRules Target) : base(Target)
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

			// UI
			"UMG",
			"SlateCore",

			// Input
			"EnhancedInput",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"InputCore",
		});
	}
}
