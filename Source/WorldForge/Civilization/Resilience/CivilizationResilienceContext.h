#pragma once

#include "CoreMinimal.h"

class UWorld;
class UWorldMapSubsystem;
class USettlementManager;

struct WORLDFORGE_API FResilienceContext
{
    UWorld* World = nullptr;
    UWorldMapSubsystem* WMS = nullptr;
    USettlementManager* SettlementManager = nullptr;
    int32 StepNumber = 0;
};