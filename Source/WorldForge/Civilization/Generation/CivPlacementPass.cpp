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

        const int32 StartTile = Generator.FindStartingTile(WorldTiles, UsedTileIDs, RNG);
        if (StartTile >= 0 && WorldTiles.IsValidIndex(StartTile))
        {
            Civ.ControlledTileIDs.Add(StartTile);
            UsedTileIDs.Add(StartTile);

            // ADDED: Race assignment extracted into dedicated pass
            FCivRaceSeedPass::AssignRaceForStartingTile(Generator, Civ, WorldTiles, StartTile, RNG);
        }
        else
        {
            // KEPT EXISTING: preserve fallback behavior when no valid start tile is available
            Civ.RaceType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")));
        }

        Result.Add(Civ);
    }
}