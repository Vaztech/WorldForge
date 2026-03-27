#pragma once

#include "CoreMinimal.h"
#include "Resilience/CivilizationResilienceContext.h"

struct FCivilizationData;

class WORLDFORGE_API FCivilizationResiliencePopulationPass
{
public:
    static void Execute(
        const FResilienceContext& Context,
        TArray<FCivilizationData>& Civs);
};