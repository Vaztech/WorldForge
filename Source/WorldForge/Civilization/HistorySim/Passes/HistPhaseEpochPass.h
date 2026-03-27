#pragma once

#include "CoreMinimal.h"

class UHistoricalSimulation;
struct FHistoricalSimContext;

class FHistPhaseEpochPass
{
public:
    static void RunEmergence(int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
    static void RunExpansion(int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
    static void RunConflict(int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);
    static void RunStabilization(int32 YS, int32 YE, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context);

private:
    static void RunEpoch(
        int32 YS,
        int32 YE,
        int32 EventCount,
        void (*EventGenerator)(int32, float, UHistoricalSimulation&, FHistoricalSimContext&),
        UHistoricalSimulation& Simulation,
        FHistoricalSimContext& Context);
};