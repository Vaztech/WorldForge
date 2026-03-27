#include "HistorySim/Passes/HistPhaseEpochPass.h"
#include "../../HistoricalSimulation.h"
#include "../HistoricalSimContext.h"
#include "HistEventGenerationPass.h"
#include "../../Core/WorldForgeRNG.h"

void FHistPhaseEpochPass::RunEmergence(
    int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context)
{
    const int32 EventCount = FMath::Max(2, Context.Civs.Num());
    RunEpoch(YS, YE, EventCount, &FHistEventGenerationPass::GenerateEmergenceEvent, Simulation, Context);
}

void FHistPhaseEpochPass::RunExpansion(
    int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context)
{
    const int32 EventCount = FMath::Max(3, Context.Civs.Num() * 2);
    RunEpoch(YS, YE, EventCount, &FHistEventGenerationPass::GenerateExpansionEvent, Simulation, Context);
}

void FHistPhaseEpochPass::RunConflict(
    int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context)
{
    const int32 EventCount = FMath::Max(4, Context.Civs.Num() * 2);
    RunEpoch(YS, YE, EventCount, &FHistEventGenerationPass::GenerateConflictEvent, Simulation, Context);
}

void FHistPhaseEpochPass::RunStabilization(
    int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context)
{
    const int32 EventCount = FMath::Max(3, Context.Civs.Num());
    RunEpoch(YS, YE, EventCount, &FHistEventGenerationPass::GenerateStabilizationEvent, Simulation, Context);
}

void FHistPhaseEpochPass::RunEpoch(
    int32 YS,
    int32 YE,
    int32 EventCount,
    void (*EventGenerator)(int32, float, UHistoricalSimulation&, FHistoricalSimContext&),
    UHistoricalSimulation& Simulation,
    FHistoricalSimContext& Context)
{
    for (int32 i = 0; i < EventCount; i++)
    {
        const int32 Year = YS + Context.RNG->NextInt(
            UWorldForgeRNG::BRANCH_PREHISTORY,
            0,
            FMath::Max(0, YE - YS - 1));

        const float Roll = Context.RNG->NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY);
        EventGenerator(Year, Roll, Simulation, Context);
    }
}