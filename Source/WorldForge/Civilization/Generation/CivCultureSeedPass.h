#pragma once

#include "CoreMinimal.h"
#include "CivGenTypes.h" // ADDED
#include "../../Data/WorldForgeDataTypes.h" // ADDED

class UCivilizationGenerator;
class UWorldForgeRNG;

// ADDED: Coherent cultural DNA seeding pass extracted from CivilizationGenerator
class WORLDFORGE_API FCivCultureSeedPass
{
public:
    static void Execute(
        const UCivilizationGenerator& Generator,
        TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& WorldTiles,
        UWorldForgeRNG* RNG
    );
};