// Source/WorldForge/Grid/LayerViewTypes.h
//
// Shared layer-view types for Going Medieval / Dwarf Fortress-style Z layer tools.

#pragma once

#include "CoreMinimal.h"
#include "LayerViewTypes.generated.h"

/** Mining designation height behavior. */
UENUM(BlueprintType)
enum class EWFMiningHeightMode : uint8
{
    /** Select/designate exactly the active voxel layer. */
    SingleVoxel UMETA(DisplayName = "1 Voxel Block"),

    /** Select/designate the active voxel plus one voxel below it. Default. */
    TwoVoxelColumn UMETA(DisplayName = "2 Block Column")
};
