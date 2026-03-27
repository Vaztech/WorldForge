#pragma once

#include "CoreMinimal.h"
#include "../../Data/WorldForgeDataTypes.h"

class UWorldForgeRNG;

// ADDED: Shared context for Civilization Generation passes
struct FCivGenContext
{
    // KEPT EXISTING: Inputs
    int32 StartingCivCount = 0;
    const TArray<FWorldTileData>* WorldTiles = nullptr;
    int32 GridWidth = 0;
    UWorldForgeRNG* RNG = nullptr;

    // KEPT EXISTING: Working data
    TArray<int32> UsedTileIDs;

    // KEPT EXISTING: Output
    TArray<FCivilizationData> GeneratedCivs;

    // ADDED: Safety helper
    bool IsValid() const
    {
        return WorldTiles != nullptr && RNG != nullptr;
    }
};