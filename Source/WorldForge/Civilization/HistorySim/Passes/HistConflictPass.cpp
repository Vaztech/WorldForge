#include "HistorySim/Passes/HistConflictPass.h"
#include "../../HistoricalSimulation.h"
#include "../HistoricalSimContext.h"
#include "../../Core/WorldForgeRNG.h"

void FHistConflictPass::GenerateWarEvent(
    int32 Year,
    UHistoricalSimulation& Simulation,
    FHistoricalSimContext& Context)
{
    if (Context.Civs.Num() < 2)
    {
        return;
    }

    const int32 AI = Context.RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Context.Civs.Num() - 1);
    int32 DI = Context.RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Context.Civs.Num() - 1);
    if (DI == AI)
    {
        DI = (AI + 1) % Context.Civs.Num();
    }

    const bool bAttWins = Context.Civs[AI].CulturalDNA.Militarism >= Context.Civs[DI].CulturalDNA.Militarism;
    const int32 LoserID = bAttWins ? Context.Civs[DI].CivID : Context.Civs[AI].CivID;

    for (FCurrencyDefinition& Cur : Context.Currencies)
    {
        if (Cur.IssuingCivID == LoserID)
        {
            Cur.CurrentAnchorValue *= 0.85f;
            Cur.TrustState.MilitaryStability *= 0.8f;
        }
    }

    Simulation.RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Conflict"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Violence"))),
        { Context.Civs[AI].CivID, Context.Civs[DI].CivID },
        bAttWins ? TEXT("Attacker victory") : TEXT("Defender repelled"),
        TEXT("Border dispute"),
        -0.1f,
        Context);
}

void FHistConflictPass::GenerateCollapseEvent(
    int32 Year,
    UHistoricalSimulation& Simulation,
    FHistoricalSimContext& Context)
{
    if (Context.Civs.Num() < 2)
    {
        return;
    }

    int32 WI = INDEX_NONE;
    for (int32 i = 0; i < Context.Civs.Num(); i++)
    {
        if (Context.Civs[i].bCollapsed)
        {
            continue;
        }

        if (WI == INDEX_NONE || Context.Civs[i].TechLevel < Context.Civs[WI].TechLevel)
        {
            WI = i;
        }
    }

    if (WI == INDEX_NONE)
    {
        return;
    }

    Context.Civs[WI].bCollapsed = true;

    for (FCurrencyDefinition& Cur : Context.Currencies)
    {
        if (Cur.IssuingCivID == Context.Civs[WI].CivID)
        {
            Cur.bCollapsed = true;
            Cur.CurrentAnchorValue = 0.01f;
            Cur.TrustState.MilitaryStability = 0.0f;
            Cur.TrustState.PoliticalCohesion = 0.0f;
        }
    }

    Simulation.RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Conflict"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Economic"))),
        { Context.Civs[WI].CivID },
        TEXT("Civilization collapsed"),
        TEXT("Military defeat and economic strain"),
        -0.5f,
        Context);
}