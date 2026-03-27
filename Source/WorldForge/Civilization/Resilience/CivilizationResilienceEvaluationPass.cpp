#include "Resilience/CivilizationResilienceEvaluationPass.h"
#include "CivilizationResilienceSystem.h"
#include "Resilience/CivilizationResilienceScoringPass.h"
#include "Resilience/CivilizationResilienceOutcomePass.h"
#include "WorldForgeDataTypes.h"

void FCivilizationResilienceEvaluationPass::Execute(
    const FResilienceContext& Context,
    TArray<FCivilizationData>& Civs,
    UCivilizationResilienceSystem* Owner)
{
    if (!Owner) return;

    for (FCivilizationData& Civ : Civs)
    {
        ProcessCiv(Context, Civ, Owner);
    }
}

void FCivilizationResilienceEvaluationPass::ProcessCiv(
    const FResilienceContext& Context,
    FCivilizationData& Civ,
    UCivilizationResilienceSystem* Owner)
{
    if (!Owner) return;
    if (Civ.CivID < 0) return;

    FResilienceRecord& Record = Owner->FindOrAddRecord(Civ.CivID);
    Record.CivID = Civ.CivID;

    if (Record.bHasCollapsed) return;

    FResilienceScoreBundle Scores;
    FCivilizationResilienceScoringPass::BuildScoreBundle(
        Owner,
        Civ,
        Context.World,
        Scores.TerritoryScore,
        Scores.EconomyScore,
        Scores.PopulationScore,
        Scores.MilitaryScore,
        Scores.Health);

    Owner->LogCivHealthFromScores(
        Civ,
        Context.StepNumber,
        Scores.TerritoryScore,
        Scores.EconomyScore,
        Scores.PopulationScore,
        Scores.MilitaryScore,
        Scores.Health);

    FCivilizationResilienceOutcomePass::ApplyOutcome(
        Owner,
        Civ,
        Record,
        Context.StepNumber,
        Context.World,
        Scores.Health);
}