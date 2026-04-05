// Source/WorldForge/Player/BuildModeTypes.h
#pragma once

#include "CoreMinimal.h"
#include "BuildModeTypes.generated.h"

UENUM(BlueprintType)
enum class EBuildMode : uint8
{
	None,           // Not building
	Prefab,         // Placing a prefab building
	CustomDraw      // Painting individual cells
};

UENUM(BlueprintType)
enum class EBuildGhostState : uint8
{
	Valid,          // Green — can place here
	Invalid,        // Red — blocked/occupied
	Hidden          // Not in build mode
};
