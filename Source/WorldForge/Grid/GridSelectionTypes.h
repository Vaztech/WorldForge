// Source/WorldForge/Grid/GridSelectionTypes.h
//
// Shared grid-selection data for WorldForge tools.
// This layer is player-facing only: it knows about hovered cells,
// drag previews, and selected cells. Tool systems decide what those cells mean.

#pragma once

#include "CoreMinimal.h"
#include "GridSelectionTypes.generated.h"

/** How mouse drag turns into selected cells. */
UENUM(BlueprintType)
enum class EGridSelectionShape : uint8
{
    /** Single click selects one cell. Dragging can still preview a rectangle when enabled by caller. */
    Single      UMETA(DisplayName = "Single"),

    /** Click-drag from start to current hover creates a filled rectangle. */
    Rectangle   UMETA(DisplayName = "Rectangle"),

    /** Every cell crossed by the cursor is added. Later pass; not the default first implementation. */
    Paint       UMETA(DisplayName = "Paint"),

    /** Click-drag creates a line along the dominant axis. Later pass for wall/road tools. */
    Line        UMETA(DisplayName = "Line")
};

/** Simple C++/Blueprint-safe snapshot of one selected cell. */
USTRUCT(BlueprintType)
struct FGridSelectionCell
{
    GENERATED_BODY()

    /** True voxel/grid coordinate used by TileGridOverlayActor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|GridSelection")
    FIntVector VoxelCoord = FIntVector::ZeroValue;

    /** World-space terrain Z from the raycast hit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|GridSelection")
    float TerrainZ = 0.0f;
};
