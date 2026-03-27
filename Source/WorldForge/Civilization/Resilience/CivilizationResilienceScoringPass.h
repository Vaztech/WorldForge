#pragma once

#include "CoreMinimal.h"

struct FCivilizationData;
class UWorld;
class UCivilizationResilienceSystem;

class WORLDFORGE_API FCivilizationResilienceScoringPass
{
public:
    static void BuildScoreBundle(
        UCivilizationResilienceSystem* Owner,
        const FCivilizationData& Civ,
        UWorld* World,
        float& OutTerritoryScore,
        float& OutEconomyScore,
        float& OutPopulationScore,
        float& OutMilitaryScore,
        float& OutHealth);
};