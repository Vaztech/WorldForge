#pragma once

#include "CoreMinimal.h"

class UHistoricalSimulation;
struct FHistoricalSimContext;

class FHistStabilityPass
{
public:
    static void GenerateDisasterEvent(int32 Year, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
};