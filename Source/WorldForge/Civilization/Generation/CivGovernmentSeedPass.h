#pragma once

#include "CoreMinimal.h"
#include "../../Data/WorldForgeDataTypes.h"
#include "CivGenTypes.h" // ADDED

class UWorldForgeRNG;

// ADDED: Dedicated government seed pass scaffold.
// ADDED: Intentionally a no-op at this stage to preserve existing behavior while establishing structure.
class WORLDFORGE_API FCivGovernmentSeedPass
{
public:
    static void Execute(
        TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& WorldTiles,
        UWorldForgeRNG* RNG
    );
};