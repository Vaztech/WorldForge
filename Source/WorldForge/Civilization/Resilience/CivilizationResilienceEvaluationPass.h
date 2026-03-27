#pragma once

#include "CoreMinimal.h"
#include "Resilience/CivilizationResilienceContext.h"

struct FCivilizationData;
class UCivilizationResilienceSystem;

class WORLDFORGE_API FCivilizationResilienceEvaluationPass
{
public:
    static void Execute(
        const FResilienceContext& Context,
        TArray<FCivilizationData>& Civs,
        UCivilizationResilienceSystem* Owner);

private:
    struct FResilienceScoreBundle
    {
        float TerritoryScore = 0.0f;
        float EconomyScore = 0.0f;
        float PopulationScore = 0.0f;
        float MilitaryScore = 0.0f;
        float Health = 0.0f;
    };

    static void ProcessCiv(
        const FResilienceContext& Context,
        FCivilizationData& Civ,
        UCivilizationResilienceSystem* Owner);
};