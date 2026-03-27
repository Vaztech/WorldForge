#include "HistoricalSimulation.h"
#include "../Core/WorldForgeRNG.h"
#include "HistorySim/HistoricalSimContext.h"
#include "HistorySim/Passes/HistPhaseEpochPass.h"

void UHistoricalSimulation::RunSimulation(
    FHistoricalSimConfig Config,
    TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& Tiles,
    TArray<FCurrencyDefinition>& Currencies,
    TArray<FHistoryEventData>& OutHistory,
    UWorldForgeRNG* RNG)
{
    if (!RNG || Civs.Num() == 0)
    {
        return;
    }

    OutHistory.Reset();
    AllEvents.Reset();
    NextEventID = 0;

    FHistoricalSimContext Context(
        Config,
        Civs,
        Tiles,
        Currencies,
        OutHistory,
        RNG,
        AllEvents,
        NextEventID);

    const int32 Y = Config.TotalYears;
    FHistPhaseEpochPass::RunEmergence(0, Y / 4, *this, Context);
    FHistPhaseEpochPass::RunExpansion(Y / 4, Y / 2, *this, Context);
    FHistPhaseEpochPass::RunConflict(Y / 2, 3 * Y / 4, *this, Context);
    FHistPhaseEpochPass::RunStabilization(3 * Y / 4, Y, *this, Context);

    UE_LOG(LogTemp, Log, TEXT("[HistSim] Complete -- %d events"), OutHistory.Num());
}

void UHistoricalSimulation::RecordEvent(
    int32 Year,
    FGameplayTag EpochTag,
    FGameplayTag EventType,
    const TArray<int32>& PIDs,
    const FString& Result,
    const FString& Cause,
    float EconImpact,
    FHistoricalSimContext& Context)
{
    FHistoryEventData E;
    E.EventID = Context.NextEventID++;
    E.Year = Year;
    E.EpochTag = EpochTag;
    E.EventType = EventType;
    E.ParticipantIDs = PIDs;
    E.Result = Result;
    E.Cause = Cause;
    E.EconomicImpact = EconImpact;

    Context.OutHistory.Add(E);
    Context.AllEvents.Add(E);
}