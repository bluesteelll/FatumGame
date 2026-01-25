// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class FatumGameEditorTarget : FatumGameTarget
{
	public FatumGameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
	}
}
