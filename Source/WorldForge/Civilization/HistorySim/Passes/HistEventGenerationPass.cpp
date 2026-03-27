#include "HistorySim/Passes/HistEventGenerationPass.h"
#include "../../HistoricalSimulation.h"
#include "../HistoricalSimContext.h"
#include "HistExpansionPass.h"
#include "HistStabilityPass.h"
#include "HistConflictPass.h"

void FHistEventGenerationPass::GenerateEmergenceEvent(
    int32 Year,
    float Roll,
    UHistoricalSimulation& Simulation,
    FHistoricalSimContext& Context)
{
    if (Roll < 0.50f)
    {
        FHistExpansionPass::GenerateTradeEvent(Year, Simulation, Context);
    }
    else if (Roll < 0.75f)
    {
        FHistConflictPass::GenerateWarEvent(Year, Simulation, Context);
    }
    else
    {
        FHistStabilityPass::GenerateDisasterEvent(Year, Simulation, Context);
    }
}

void FHistEventGenerationPass::GenerateExpansionEvent(
    int32 Year,
    float Roll,
    UHistoricalSimulation& Simulation,
    FHistoricalSimContext& Context)
{
    if (Roll < 0.40f)
    {
        FHistExpansionPass::GenerateTradeEvent(Year, Simulation, Context);
    }
    else if (Roll < 0.70f)
    {
        FHistConflictPass::GenerateWarEvent(Year, Simulation, Context);
    }
    else
    {
        FHistStabilityPass::GenerateDisasterEvent(Year, Simulation, Context);
    }
}

void FHistEventGenerationPass::GenerateConflictEvent(
    int32 Year,
    float Roll,
    UHistoricalSimulation& Simulation,
    FHistoricalSimContext& Context)
{
    if (Roll < 0.55f)
    {
        FHistConflictPass::GenerateWarEvent(Year, Simulation, Context);
    }
    else if (Roll < 0.75f)
    {
        FHistStabilityPass::GenerateDisasterEvent(Year, Simulation, Context);
    }
    else if (Roll < 0.90f)
    {
        FHistConflictPass::GenerateCollapseEvent(Year, Simulation, Context);
    }
    else
    {
        FHistExpansionPass::GenerateTradeEvent(Year, Simulation, Context);
    }
}

void FHistEventGenerationPass::GenerateStabilizationEvent(
    int32 Year,
    float Roll,
    UHistoricalSimulation& Simulation,
    FHistoricalSimContext& Context)
{
    if (Roll < 0.60f)
    {
        FHistExpansionPass::GenerateTradeEvent(Year, Simulation, Context);
    }
    else if (Roll < 0.80f)
    {
        FHistConflictPass::GenerateWarEvent(Year, Simulation, Context);
    }
    else
    {
        FHistStabilityPass::GenerateDisasterEvent(Year, Simulation, Context);
    }
}