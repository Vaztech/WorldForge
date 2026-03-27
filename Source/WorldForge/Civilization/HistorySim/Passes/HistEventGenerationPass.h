#pragma once

#include "CoreMinimal.h"

class UHistoricalSimulation;
struct FHistoricalSimContext;

class FHistEventGenerationPass
{
public:
    static void GenerateEmergenceEvent(int32 Year, float Roll, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
    static void GenerateExpansionEvent(int32 Year, float Roll, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
    static void GenerateConflictEvent(int32 Year, float Roll, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
    static void GenerateStabilizationEvent(int32 Year, float Roll, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
};