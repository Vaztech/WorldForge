#include "Resilience/CivilizationResilienceScoringPass.h"
#include "CivilizationResilienceSystem.h"

void FCivilizationResilienceScoringPass::BuildScoreBundle(
    UCivilizationResilienceSystem* Owner,
    const FCivilizationData& Civ,
    UWorld* World,
    float& OutTerritoryScore,
    float& OutEconomyScore,
    float& OutPopulationScore,
    float& OutMilitaryScore,
    float& OutHealth)
{
    if (!Owner)
    {
        OutTerritoryScore = 0.0f;
        OutEconomyScore = 0.0f;
        OutPopulationScore = 0.0f;
        OutMilitaryScore = 0.0f;
        OutHealth = 0.0f;
        return;
    }

    Owner->BuildScoreBundleFromWorld(
        Civ,
        World,
        OutTerritoryScore,
        OutEconomyScore,
        OutPopulationScore,
        OutMilitaryScore,
        OutHealth);
}