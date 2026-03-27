#pragma once

#include "CoreMinimal.h"
#include "../../Data/WorldForgeDataTypes.h"
#include "CivGenTypes.h" // ADDED

class UCivilizationGenerator;
class UWorldForgeRNG;

// ADDED: Dedicated race seeding pass extracted from placement flow
class WORLDFORGE_API FCivRaceSeedPass
{
public:
    static void AssignRaceForStartingTile(
        const UCivilizationGenerator& Generator,
        FCivilizationData& Civ,
        const TArray<FWorldTileData>& WorldTiles,
        int32 StartTile,
        UWorldForgeRNG* RNG
    );
};