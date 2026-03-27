#include "HistorySim/Passes/HistExpansionPass.h"
#include "../../HistoricalSimulation.h"
#include "../HistoricalSimContext.h"
#include "../../Core/WorldForgeRNG.h"

void FHistExpansionPass::GenerateTradeEvent(
    int32 Year, UHistoricalSimulation& Simulation, FHistoricalSimContext& Context)
{
    if (Context.Civs.Num() < 2) return;

    const int32 A = Context.RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Context.Civs.Num() - 1);
    int32 B = Context.RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Context.Civs.Num() - 1);
    if (B == A) B = (A + 1) % Context.Civs.Num();

    for (FCurrencyDefinition& Cur : Context.Currencies)
    {
        if (Cur.IssuingCivID == Context.Civs[A].CivID || Cur.IssuingCivID == Context.Civs[B].CivID)
        {
            Cur.CurrentAnchorValue *= 1.02f;
            Cur.TrustState.TradeReputation = FMath::Min(1.0f, Cur.TrustState.TradeReputation + 0.02f);
        }
    }

    Simulation.RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Expansion"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Economic"))),
        { Context.Civs[A].CivID, Context.Civs[B].CivID },
        TEXT("Trade agreement"),
        TEXT("Mutual interest"),
        0.05f,
        Context);
}