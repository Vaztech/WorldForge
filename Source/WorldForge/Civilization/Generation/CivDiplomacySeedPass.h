#pragma once

#include "CoreMinimal.h"
#include "../../Data/WorldForgeDataTypes.h"
#include "CivGenTypes.h" // ADDED

class UWorldForgeRNG;

// MODIFIED: Dedicated diplomacy seed pass now performs real initial relationship seeding.
class WORLDFORGE_API FCivDiplomacySeedPass
{
public:
    static void Execute(
        TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& WorldTiles,
        UWorldForgeRNG* RNG
    );
};