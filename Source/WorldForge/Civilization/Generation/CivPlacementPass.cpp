#include "CivPlacementPass.h"
#include "../CivilizationGenerator.h"
#include "CivGenContext.h"
#include "CivRaceSeedPass.h" // ADDED
#include "CivNamingPass.h"   // ADDED
#include "../../Core/WorldForgeRNG.h"

void FCivPlacementPass::Execute(const UCivilizationGenerator& Generator, FCivGenContext& Context)
{
    // ADDED: Preserve existing behavior exactly; just moved into coherent passes
    if (!Context.IsValid() || Context.WorldTiles == nullptr)
    {
        return;
    }

    const TArray<FWorldTileData>& WorldTiles = *Context.WorldTiles;
    TArray<FCivilizationData>& Result = Context.GeneratedCivs;
    TArray<int32>& UsedTileIDs = Context.UsedTileIDs;
    UWorldForgeRNG* RNG = Context.RNG;

    for (int32 i = 0; i < Context.StartingCivCount; i++)
    {
        FCivilizationData Civ;
        Civ.CivID = i;

        // ADDED: Naming extracted into dedicated pass
        FCivNamingPass::AssignDefaultName(Civ);

        const int32 StartTile = Generator.FindStartingTile(WorldTiles, UsedTileIDs, Context.GridWidth, RNG);
        if (StartTile >= 0 && WorldTiles.IsValidIndex(StartTile))
        {
            // ADDED:
            // Store the civilization's true starting tile.
            // ClaimTerritories() needs this value later to expand/control nearby tiles.
            Civ.StartTileIndex = StartTile;

            // Existing behavior:
            // The starting tile is always controlled by the civilization.
            Civ.ControlledTileIDs.Add(StartTile);

            // Existing behavior:
            // Prevent another civilization from selecting the same starting tile.
            UsedTileIDs.Add(StartTile);

            // Existing behavior:
            // Pick race based on the starting tile/biome rules.
            FCivRaceSeedPass::AssignRaceForStartingTile(Generator, Civ, WorldTiles, StartTile, RNG);
        }
        else
        {
            // Existing fallback:
            // If no valid start tile is found, preserve old behavior and default to Dwarf.
            Civ.RaceType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")));
        }

        Result.Add(Civ);
    }
}
