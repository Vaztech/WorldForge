#pragma once

#include "CoreMinimal.h"
#include "WorldForgeDataTypes.h"

class UWorld;
class UWorldMapSubsystem;
class UEventBus;

struct FCultureDriftContext
{
    UWorld* World = nullptr;
    UWorldMapSubsystem* WorldMapSubsystem = nullptr;
    UEventBus* EventBus = nullptr;
    const TArray<FSimulationEvent>* RecentEvents = nullptr;
    int32 StepNumber = 0;
};