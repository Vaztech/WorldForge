#pragma once

#include "CoreMinimal.h"

class UHistoricalSimulation;
struct FHistoricalSimContext;

class FHistExpansionPass
{
public:
    static void GenerateTradeEvent(int32 Year, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
};