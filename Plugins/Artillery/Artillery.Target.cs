// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ArtilleryRuntimeTarget : TargetRules
{
	public ArtilleryRuntimeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		// this.bAllowLTCG = true; //enable for shipping builds.
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("Bristle54");
		ProjectDefinitions.Remove("WINVER=0x0601");
		ProjectDefinitions.Remove("_WIN32_WINNT=0x0601");
		ProjectDefinitions.Add("WINVER=0x0602");
		ProjectDefinitions.Add("_WIN32_WINNT=0x0602");
		bUseAdaptiveUnityBuild = false;
	}
}
