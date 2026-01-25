// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

enum ActionPatternKey
{
	InternallyStateless = 0,
	SingleFrameFire = 1,
	ButtonHoldAllowOneMiss = 2,
	ButtonHold = 3,
	ButtonReleaseNoDelay = 4,
	StickFlick = 5,
	OnPress = 6
};
typedef ActionPatternKey ArtIPMKey;//artillery intent pattern matcher key
