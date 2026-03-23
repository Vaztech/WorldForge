#include "CultureDriftSystem.h"
#include "EventBus.h"
#include "WorldMapSubsystem.h"
#include "Engine/World.h"

// ADDED
// Compatibility helpers:
// The guide assumes FCivilizationData.CulturalDNA is TArray<float>.
// In this project it is FCultureData with named fields, so we adapt
// Section B to the existing architecture instead of changing data shape.
namespace
{
    static float GetAxisValue(const FCultureData& DNA, int32 Axis)
    {
        switch (Axis)
        {
        case CA_Hierarchy:         return DNA.HierarchyTolerance;
        case CA_OutgroupHostility: return DNA.OutgroupHostility;
        case CA_LaborEthics:       return DNA.LaborEthics;
        case CA_Militarism:        return DNA.Militarism;
        case CA_MagicReverence:    return DNA.MagicReverence;
        case CA_Individualism:     return DNA.Individualism;
        case CA_TheologicalRigid:  return DNA.TheologicalRigidity;
        case CA_EconomicOpenness:  return DNA.EconomicOpenness;
        case CA_ExpansionDrive:    return DNA.ExpansionDrive;
        case CA_CosmicDevotion:    return DNA.CosmicDevotion;
        default:                   return 0.0f;
        }
    }

    static void SetAxisValue(FCultureData& DNA, int32 Axis, float Value)
    {
        switch (Axis)
        {
        case CA_Hierarchy:         DNA.HierarchyTolerance = Value; break;
        case CA_OutgroupHostility: DNA.OutgroupHostility = Value; break;
        case CA_LaborEthics:       DNA.LaborEthics = Value; break;
        case CA_Militarism:        DNA.Militarism = Value; break;
        case CA_MagicReverence:    DNA.MagicReverence = Value; break;
        case CA_Individualism:     DNA.Individualism = Value; break;
        case CA_TheologicalRigid:  DNA.TheologicalRigidity = Value; break;
        case CA_EconomicOpenness:  DNA.EconomicOpenness = Value; break;
        case CA_ExpansionDrive:    DNA.ExpansionDrive = Value; break;
        case CA_CosmicDevotion:    DNA.CosmicDevotion = Value; break;
        default: break;
        }
    }
}

void UCultureDriftSystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[CultureDriftSystem] Initialized."));
}

void UCultureDriftSystem::Deinitialize()
{
    Super::Deinitialize();
}

void UCultureDriftSystem::OnSimStep(int32 StepNumber, int32 SettlementID,
    TArray<FAgentData>& Agents, UWorld* World)
{
    (void)SettlementID; // ADDED
    (void)Agents;       // ADDED

    if (!World) return;
    if (StepNumber % LivingWorldTickInterval != 0) return;

    UWorldMapSubsystem* WMS = World->GetSubsystem<UWorldMapSubsystem>();
    UEventBus* Bus = World->GetSubsystem<UEventBus>();
    if (!WMS || !Bus) return;

    const TArray<FSimulationEvent>& RecentEvents = Bus->GetRecentEvents();

    TArray<FCivilizationData>& Civs = WMS->GetMutableCivilizations();
    for (FCivilizationData& Civ : Civs)
    {
        FCivilizationData Before = Civ; // ADDED
        ApplyEventDrivenDrift(Civ, RecentEvents);
        CapAllDNAValues(Civ);
        LogSignificantDrift(Before, Civ, StepNumber);
    }
}

void UCultureDriftSystem::ApplyEventDrivenDrift(
    FCivilizationData& Civ,
    const TArray<FSimulationEvent>& RecentEvents) const
{
    int32 WarEventCount = 0;
    int32 TradeBoomCount = 0;
    int32 CosmicEventCount = 0;

    for (const FSimulationEvent& E : RecentEvents)
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

        // MODIFIED
        // Drift against FCultureData fields instead of a float array.
        SetAxisValue(Civ.CulturalDNA, CA_Militarism,
            DriftAxis(GetAxisValue(Civ.CulturalDNA, CA_Militarism), +WarStrength * 100.0f));
        SetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility,
            DriftAxis(GetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility), +WarStrength * 30.0f));
        SetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness,
            DriftAxis(GetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness), -WarStrength * 30.0f));
    }

    if (TradeBoomCount > 0)
    {
        const float TradeStrength = FMath::Min(static_cast<float>(TradeBoomCount) * 0.04f, 0.16f);

        // MODIFIED
        SetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness,
            DriftAxis(GetAxisValue(Civ.CulturalDNA, CA_EconomicOpenness), +TradeStrength * 100.0f));
        SetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility,
            DriftAxis(GetAxisValue(Civ.CulturalDNA, CA_OutgroupHostility), -TradeStrength * 50.0f));
    }

    if (CosmicEventCount > 0)
    {
        // MODIFIED
        SetAxisValue(Civ.CulturalDNA, CA_CosmicDevotion,
            DriftAxis(GetAxisValue(Civ.CulturalDNA, CA_CosmicDevotion), +2.0f));
        SetAxisValue(Civ.CulturalDNA, CA_TheologicalRigid,
            DriftAxis(GetAxisValue(Civ.CulturalDNA, CA_TheologicalRigid), +1.0f));
    }
}

void UCultureDriftSystem::CapAllDNAValues(FCivilizationData& Civ)
{
    // MODIFIED
    // Clamp the actual FCultureData fields in-place.
    Civ.CulturalDNA.HierarchyTolerance = FMath::Clamp(Civ.CulturalDNA.HierarchyTolerance, 0.0f, 100.0f);
    Civ.CulturalDNA.OutgroupHostility = FMath::Clamp(Civ.CulturalDNA.OutgroupHostility, 0.0f, 100.0f);
    Civ.CulturalDNA.LaborEthics = FMath::Clamp(Civ.CulturalDNA.LaborEthics, 0.0f, 100.0f);
    Civ.CulturalDNA.Militarism = FMath::Clamp(Civ.CulturalDNA.Militarism, 0.0f, 100.0f);
    Civ.CulturalDNA.MagicReverence = FMath::Clamp(Civ.CulturalDNA.MagicReverence, 0.0f, 100.0f);
    Civ.CulturalDNA.Individualism = FMath::Clamp(Civ.CulturalDNA.Individualism, 0.0f, 100.0f);
    Civ.CulturalDNA.TheologicalRigidity = FMath::Clamp(Civ.CulturalDNA.TheologicalRigidity, 0.0f, 100.0f);
    Civ.CulturalDNA.EconomicOpenness = FMath::Clamp(Civ.CulturalDNA.EconomicOpenness, 0.0f, 100.0f);
    Civ.CulturalDNA.ExpansionDrive = FMath::Clamp(Civ.CulturalDNA.ExpansionDrive, 0.0f, 100.0f);
    Civ.CulturalDNA.CosmicDevotion = FMath::Clamp(Civ.CulturalDNA.CosmicDevotion, 0.0f, 100.0f);
}

void UCultureDriftSystem::LogSignificantDrift(
    const FCivilizationData& Before,
    const FCivilizationData& After,
    int32 StepNumber) const
{
    for (int32 Axis = 0; Axis < CA_MAX; ++Axis)
    {
        const float BeforeValue = GetAxisValue(Before.CulturalDNA, Axis); // ADDED
        const float AfterValue = GetAxisValue(After.CulturalDNA, Axis);   // ADDED
        const float Delta = FMath::Abs(AfterValue - BeforeValue);

        if (Delta >= 10.0f)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[CultureDriftSystem] Civ %d axis[%d] drifted %.1f -> %.1f at step %d"),
                After.CivID,
                Axis,
                BeforeValue,
                AfterValue,
                StepNumber);

            // ADDED
            // Guide history-log write is intentionally left as log-only for now.
            // This preserves current data flow and avoids adding new history plumbing.
        }
    }
}

float UCultureDriftSystem::DriftAxis(float Current, float Delta)
{
    return FMath::Clamp(Current + Delta, 0.0f, 100.0f);
}