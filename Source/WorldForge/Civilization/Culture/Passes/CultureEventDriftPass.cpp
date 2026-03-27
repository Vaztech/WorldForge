#include "Culture/Passes/CultureEventDriftPass.h"
#include "Culture/CultureDriftContext.h"
#include "Culture/CultureDNAUtils.h"

void FCultureEventDriftPass::Run(FCultureDriftContext& Context, FCivilizationData& Civ)
{
    if (!Context.RecentEvents)
    {
        return;
    }

    int32 WarEventCount = 0;
    int32 TradeBoomCount = 0;
    int32 CosmicEventCount = 0;

    for (const FSimulationEvent& E : *Context.RecentEvents)
    {
        if (E.EventType.MatchesTag(FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.WarStarted")))))
        {
            WarEventCount++;
        }

        if (E.EventType.MatchesTag(FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.TradeBoom")))))
        {
            TradeBoomCount++;
        }

        if (E.EventType.MatchesTag(FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.CosmicIntervention")))))
        {
            CosmicEventCount++;
        }
    }

    if (WarEventCount > 0)
    {
        const float WarStrength = FMath::Min(static_cast<float>(WarEventCount) * 0.05f, 0.20f);

        CultureDNAUtils::SetAxisValue(Civ.CulturalDNA, CA_Militarism,
            CultureDNAUtils::DriftAxis(CultureDNAUtils::GetAxisValue(Civ.CulturalDNA, CA_Militarism), +WarStrength * 100.0f));
        CultureDNAUtils::SetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility,
            CultureDNAUtils::DriftAxis(CultureDNAUtils::GetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility), +WarStrength * 30.0f));
        CultureDNAUtils::SetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness,
            CultureDNAUtils::DriftAxis(CultureDNAUtils::GetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness), -WarStrength * 30.0f));
    }

    if (TradeBoomCount > 0)
    {
        const float TradeStrength = FMath::Min(static_cast<float>(TradeBoomCount) * 0.04f, 0.16f);

        CultureDNAUtils::SetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness,
            CultureDNAUtils::DriftAxis(CultureDNAUtils::GetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness), +TradeStrength * 100.0f));
        CultureDNAUtils::SetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility,
            CultureDNAUtils::DriftAxis(CultureDNAUtils::GetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility), -TradeStrength * 50.0f));
    }

    if (CosmicEventCount > 0)
    {
        CultureDNAUtils::SetAxisValue(Civ.CulturalDNA, CA_CosmicDevotion,
            CultureDNAUtils::DriftAxis(CultureDNAUtils::GetAxisValue(Civ.CulturalDNA, CA_CosmicDevotion), +2.0f));
        CultureDNAUtils::SetAxisValue(Civ.CulturalDNA, CA_TheologicalRigid,
            CultureDNAUtils::DriftAxis(CultureDNAUtils::GetAxisValue(Civ.CulturalDNA, CA_TheologicalRigid), +1.0f));
    }
}