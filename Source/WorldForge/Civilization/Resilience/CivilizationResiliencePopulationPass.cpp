#include "Resilience/CivilizationResiliencePopulationPass.h"
#include "WorldMapSubsystem.h"
#include "SettlementManager.h"
#include "WorldForgeDataTypes.h"

void FCivilizationResiliencePopulationPass::Execute(
    const FResilienceContext& Context,
    TArray<FCivilizationData>& Civs)
{
    UWorldMapSubsystem* WMS = Context.WMS;
    USettlementManager* SettlementManager = Context.SettlementManager;

    if (!WMS) return;
    if (!SettlementManager) return;

    const TArray<FSettlementState>& Settlements = SettlementManager->GetAllSettlements();

    // BF-04: Reset population
    for (FCivilizationData& Civ : Civs)
    {
        Civ.TotalPopulation = 0;
    }

    for (const FSettlementState& Settlement : Settlements)
    {
        const FWorldTileData* Tile = WMS->GetTileDataPtr(Settlement.WorldTileID);

        int32 TargetCivID = -1;

        if (Tile && Tile->ControllingCivID >= 0)
        {
            TargetCivID = Tile->ControllingCivID;
        }
        else
        {
            // ADDED -- BF-04 FIX:
            // Fallback to Civ 0 when tile has no controlling civ
            TargetCivID = 0;
        }

        for (FCivilizationData& Civ : Civs)
        {
            if (Civ.CivID == TargetCivID)
            {
                Civ.TotalPopulation += Settlement.Population;
                break;
            }
        }
    }
}