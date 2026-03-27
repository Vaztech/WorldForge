#include "CivRaceSeedPass.h"
#include "../CivilizationGenerator.h"
#include "../../Core/WorldForgeRNG.h"

void FCivRaceSeedPass::AssignRaceForStartingTile(
    const UCivilizationGenerator& Generator,
    FCivilizationData& Civ,
    const TArray<FWorldTileData>& WorldTiles,
    int32 StartTile,
    UWorldForgeRNG* RNG
)
{
    // KEPT EXISTING: preserve fallback behavior if the provided start tile is invalid
    if (!WorldTiles.IsValidIndex(StartTile))
    {
        Civ.RaceType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")));
        return;
    }

    // KEPT EXISTING: exact race assignment logic still routes through CivilizationGenerator
    Civ.RaceType = Generator.AssignRaceFromBiome(WorldTiles[StartTile].BiomeType, RNG);
}