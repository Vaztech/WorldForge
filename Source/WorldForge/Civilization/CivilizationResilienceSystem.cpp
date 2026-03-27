#include "CivilizationResilienceSystem.h"
#include "WorldMapSubsystem.h"
#include "WorldEconomySimulation.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "Engine/World.h"
#include "Resilience/CivilizationResilienceContext.h"
#include "Resilience/CivilizationResiliencePopulationPass.h"
#include "Resilience/CivilizationResilienceEvaluationPass.h"
#include "Resilience/CivilizationResilienceOutcomePass.h"

void UCivilizationResilienceSystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[CivResilience] Initialized."));
}

void UCivilizationResilienceSystem::Deinitialize()
{
    Super::Deinitialize();
}

void UCivilizationResilienceSystem::OnSimStep(int32 StepNumber, int32 SettlementID,
    TArray<FAgentData>& Agents, UWorld* World)
{
    (void)SettlementID;
    (void)Agents;

    if (!World) return;
    if (StepNumber % LivingWorldTickInterval != 0) return;

    UWorldMapSubsystem* WMS = World->GetSubsystem<UWorldMapSubsystem>();
    if (!WMS) return;

    USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
    if (!SettlementManager) return;

    TArray<FCivilizationData>& Civs = WMS->GetMutableCivilizations();

    FResilienceContext Context;
    Context.World = World;
    Context.WMS = WMS;
    Context.SettlementManager = SettlementManager;
    Context.StepNumber = StepNumber;

    FCivilizationResiliencePopulationPass::Execute(Context, Civs);
    FCivilizationResilienceEvaluationPass::Execute(Context, Civs, this);
}

void UCivilizationResilienceSystem::ProcessOneCiv(
    FCivilizationData& Civ, int32 StepNumber, UWorld* World)
{
    if (Civ.CivID < 0) return;

    FResilienceRecord& Record = FindOrAddRecord(Civ.CivID);
    Record.CivID = Civ.CivID;

    if (Record.bHasCollapsed) return;

    float TerritoryScore = 0.0f;
    float EconomyScore = 0.0f;
    float PopulationScore = 0.0f;
    float MilitaryScore = 0.0f;
    float Health = 0.0f;

    BuildScoreBundleFromWorld(
        Civ,
        World,
        TerritoryScore,
        EconomyScore,
        PopulationScore,
        MilitaryScore,
        Health);

    LogCivHealthFromScores(
        Civ,
        StepNumber,
        TerritoryScore,
        EconomyScore,
        PopulationScore,
        MilitaryScore,
        Health);

    ApplyHealthOutcome(Civ, Record, StepNumber, World, Health);
}

FResilienceRecord& UCivilizationResilienceSystem::FindOrAddRecord(int32 CivID)
{
    return Records.FindOrAdd(CivID);
}

void UCivilizationResilienceSystem::BuildScoreBundleFromWorld(
    const FCivilizationData& Civ,
    UWorld* World,
    float& OutTerritoryScore,
    float& OutEconomyScore,
    float& OutPopulationScore,
    float& OutMilitaryScore,
    float& OutHealth) const
{
    UWorldEconomySimulation* EconSim = World
        ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;

    OutTerritoryScore = FMath::Clamp(DeriveTerritoryScore(Civ), 0.0f, 1.0f);
    OutEconomyScore = FMath::Clamp(DeriveEconomyScore(Civ, EconSim), 0.0f, 1.0f);
    OutPopulationScore = FMath::Clamp(DerivePopulationScore(Civ), 0.0f, 1.0f);
    OutMilitaryScore = FMath::Clamp(DeriveMilitaryScore(Civ), 0.0f, 1.0f);

    OutHealth =
        (OutTerritoryScore * 0.30f) +
        (OutEconomyScore * 0.30f) +
        (OutPopulationScore * 0.20f) +
        (OutMilitaryScore * 0.20f);
}

float UCivilizationResilienceSystem::ComputeCivHealthFromWorld(
    const FCivilizationData& Civ,
    UWorld* World) const
{
    float TerritoryScore = 0.0f;
    float EconomyScore = 0.0f;
    float PopulationScore = 0.0f;
    float MilitaryScore = 0.0f;
    float Health = 0.0f;

    BuildScoreBundleFromWorld(
        Civ,
        World,
        TerritoryScore,
        EconomyScore,
        PopulationScore,
        MilitaryScore,
        Health);

    return Health;
}

void UCivilizationResilienceSystem::LogCivHealth(
    const FCivilizationData& Civ,
    int32 StepNumber,
    UWorld* World,
    float Health) const
{
    float TerritoryScore = 0.0f;
    float EconomyScore = 0.0f;
    float PopulationScore = 0.0f;
    float MilitaryScore = 0.0f;
    float ComputedHealth = 0.0f;

    BuildScoreBundleFromWorld(
        Civ,
        World,
        TerritoryScore,
        EconomyScore,
        PopulationScore,
        MilitaryScore,
        ComputedHealth);

    LogCivHealthFromScores(
        Civ,
        StepNumber,
        TerritoryScore,
        EconomyScore,
        PopulationScore,
        MilitaryScore,
        Health);
}

void UCivilizationResilienceSystem::LogCivHealthFromScores(
    const FCivilizationData& Civ,
    int32 StepNumber,
    float TerritoryScore,
    float EconomyScore,
    float PopulationScore,
    float MilitaryScore,
    float Health) const
{
    UE_LOG(LogTemp, Log,
        TEXT("[CivResilience] Step=%d Civ=%d TotalPopulation=%d Territory=%.2f Economy=%.2f Population=%.2f Military=%.2f Health=%.2f"),
        StepNumber,
        Civ.CivID,
        Civ.TotalPopulation,
        TerritoryScore,
        EconomyScore,
        PopulationScore,
        MilitaryScore,
        Health);
}

void UCivilizationResilienceSystem::ApplyHealthOutcome(
    FCivilizationData& Civ,
    FResilienceRecord& Record,
    int32 StepNumber,
    UWorld* World,
    float Health)
{
    FCivilizationResilienceOutcomePass::ApplyOutcome(
        this,
        Civ,
        Record,
        StepNumber,
        World,
        Health);
}

float UCivilizationResilienceSystem::GetCollapseThreshold() const
{
    return CollapseThreshold;
}

int32 UCivilizationResilienceSystem::GetConsecutiveCollapseTicksRequired() const
{
    return ConsecutiveCollapseTicksRequired;
}

void UCivilizationResilienceSystem::ExecuteCollapse(
    FCivilizationData& Civ,
    FResilienceRecord& Record,
    int32 StepNumber,
    UWorld* World)
{
    TriggerCollapse(Civ, StepNumber, World);
    Record.bHasCollapsed = true;
    Record.CollapseType = DetermineCollapseType(Civ);
    Record.CollapseStepNumber = StepNumber;

    UE_LOG(LogTemp, Warning,
        TEXT("[CivResilience] Civ %d COLLAPSED at step %d (type %d)"),
        Civ.CivID, StepNumber,
        static_cast<int32>(Record.CollapseType));
}

float UCivilizationResilienceSystem::ComputeCivHealth(const FCivilizationData& Civ) const
{
    UWorld* World = GetWorld();
    UWorldEconomySimulation* EconSim = World
        ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;

    const float T = FMath::Clamp(DeriveTerritoryScore(Civ), 0.0f, 1.0f);
    const float E = FMath::Clamp(DeriveEconomyScore(Civ, EconSim), 0.0f, 1.0f);
    const float P = FMath::Clamp(DerivePopulationScore(Civ), 0.0f, 1.0f);
    const float M = FMath::Clamp(DeriveMilitaryScore(Civ), 0.0f, 1.0f);

    return T * 0.30f + E * 0.30f + P * 0.20f + M * 0.20f;
}

float UCivilizationResilienceSystem::DeriveTerritoryScore(const FCivilizationData& Civ) const
{
    return FMath::Clamp(
        static_cast<float>(Civ.ControlledTileIDs.Num()) / 20.0f,
        0.0f,
        1.0f);
}

float UCivilizationResilienceSystem::DeriveEconomyScore(
    const FCivilizationData& Civ,
    UWorldEconomySimulation* EconSim) const
{
    if (!EconSim)
    {
        return 0.5f;
    }

    const TArray<FCurrencyDefinition>& AllCurrencies = EconSim->GetAllCurrencies();

    const FCurrencyDefinition* FoundCurrency = nullptr;

    for (const FCurrencyDefinition& Currency : AllCurrencies)
    {
        if (Currency.CurrencyTag == Civ.CurrencyTag)
        {
            FoundCurrency = &Currency;
            break;
        }
    }

    if (!FoundCurrency)
    {
        return 0.5f;
    }

    const float AnchorScore = FMath::Clamp(
        FoundCurrency->CurrentAnchorValue / FMath::Max(1.0f, FoundCurrency->BaseAnchorValue),
        0.0f,
        1.0f);

    const float TrustScore = FMath::Clamp(
        FoundCurrency->TrustState.GovernmentCredibility,
        0.0f,
        1.0f);

    return FMath::Clamp((AnchorScore * 0.6f) + (TrustScore * 0.4f), 0.0f, 1.0f);
}

float UCivilizationResilienceSystem::DerivePopulationScore(const FCivilizationData& Civ) const
{
    return FMath::Clamp(
        static_cast<float>(Civ.TotalPopulation) / 500.0f,
        0.0f,
        1.0f);
}

float UCivilizationResilienceSystem::DeriveMilitaryScore(const FCivilizationData& Civ) const
{
    return FMath::Clamp(Civ.CulturalDNA.Militarism / 100.0f, 0.0f, 1.0f);
}

ECivilizationCollapseType UCivilizationResilienceSystem::DetermineCollapseType(
    const FCivilizationData& Civ) const
{
    const float Militarism = Civ.CulturalDNA.Militarism;
    const float Expansion = Civ.CulturalDNA.ExpansionDrive;

    if (Militarism > 70.0f) return ECivilizationCollapseType::Fragmentation;
    if (Expansion > 60.0f)  return ECivilizationCollapseType::Diaspora;
    return ECivilizationCollapseType::EconomicCollapse;
}

void UCivilizationResilienceSystem::TriggerCollapse(
    FCivilizationData& Civ, int32 StepNumber, UWorld* World)
{
    Civ.bCollapsed = true;

    UWorldMapSubsystem* WMS = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    if (WMS)
    {
        FHistoryEventData Event;

        Event.Year = StepNumber / 50;

        Event.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.CivCollapsed")));

        Event.ParticipantIDs.Add(Civ.CivID);

        Event.Result = FString::Printf(
            TEXT("Civilization %d collapsed."), Civ.CivID);

        Event.Cause = TEXT("Low resilience score across territory/economy/population.");

        const int32 EventID = WMS->AddHistoryEvent(Event);

        Civ.HistoryEventIDs.Add(EventID);
    }

    DispatchCollapseEvent(Civ, StepNumber, World);
    SpawnRefugeeEvent(Civ, StepNumber, World);
}

void UCivilizationResilienceSystem::DispatchCollapseEvent(
    const FCivilizationData& Civ, int32 StepNumber, UWorld* World) const
{
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (!Bus) return;

    FSimulationEvent E;
    E.EventType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.CivCollapsed")));
    E.SettlementID = -1;
    E.AgentID = Civ.CivID;
    E.StepNumber = StepNumber;
    E.Payload = FGameplayTag();
    E.FloatPayload = 0.0f;

    Bus->Dispatch(E);
}

void UCivilizationResilienceSystem::SpawnRefugeeEvent(
    const FCivilizationData& Civ, int32 StepNumber, UWorld* World) const
{
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (!Bus) return;

    FSimulationEvent E;
    E.EventType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.RefugeesArriving")));
    E.SettlementID = -1;
    E.AgentID = Civ.CivID;
    E.StepNumber = StepNumber;
    E.Payload = FGameplayTag();
    E.FloatPayload = 10.0f;

    Bus->Dispatch(E);
}

FResilienceSaveState UCivilizationResilienceSystem::GetResilienceSaveState() const
{
    FResilienceSaveState S;
    Records.GenerateValueArray(S.Records);
    return S;
}

void UCivilizationResilienceSystem::RestoreResilienceFromSave(const FResilienceSaveState& S)
{
    Records.Empty();
    for (const FResilienceRecord& R : S.Records)
    {
        Records.Add(R.CivID, R);
    }
}