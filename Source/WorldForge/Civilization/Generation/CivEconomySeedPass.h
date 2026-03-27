#pragma once

#include "CoreMinimal.h"
#include "../../Data/WorldForgeDataTypes.h"
#include "CivGenTypes.h" // ADDED

class UWorldForgeRNG;

// MODIFIED: Dedicated economy seed pass now performs real currency seeding.
// ADDED: Uses live civ/world context while preserving the existing driver signature.
class WORLDFORGE_API FCivEconomySeedPass
{
public:
    static void Execute(
        TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& WorldTiles,
        UWorldForgeRNG* RNG
    );
};