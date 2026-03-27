#pragma once

#include "CoreMinimal.h"

class UHistoricalSimulation;
struct FHistoricalSimContext;

class FHistConflictPass
{
public:
    static void GenerateWarEvent(int32 Year, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
    static void GenerateCollapseEvent(int32 Year, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
};