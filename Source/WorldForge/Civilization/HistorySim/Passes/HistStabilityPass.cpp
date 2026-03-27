#include "HistorySim/Passes/HistStabilityPass.h"
#include "../../HistoricalSimulation.h"
#include "../HistoricalSimContext.h"
#include "../../Core/WorldForgeRNG.h"

void FHistStabilityPass::GenerateDisasterEvent(
    int32 Year, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context)
{
    if (Context.Civs.Num() == 0) return;

    const int32 VI = Context.RNG->NextInt(UWorldForgeRNG::BRANCH_DISASTERS, 0, Context.Civs.Num() - 1);
    if (Context.Civs[VI].ControlledTileIDs.Num() > 0)
    {
        const int32 TI = Context.RNG->NextInt(UWorldForgeRNG::BRANCH_DISASTERS, 0, Context.Civs[VI].ControlledTileIDs.Num() - 1);
        const int32 TID = Context.Civs[VI].ControlledTileIDs[TI];
        if (Context.Tiles.IsValidIndex(TID))
        {
            Context.Tiles[TID].Corruption += 0.1f;
            Context.Tiles[TID].FearPressure += 0.1f;
        }
    }

    Simulation.RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Conflict"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Violence"))),
        { Context.Civs[VI].CivID },
        TEXT("Disaster struck"),
        TEXT("Natural catastrophe"),
        -0.08f,
        Context);
}