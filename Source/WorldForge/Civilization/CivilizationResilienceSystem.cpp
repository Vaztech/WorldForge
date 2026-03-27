#include "CivilizationResilienceSystem.h"
#include "WorldMapSubsystem.h"
#include "WorldEconomySimulation.h"
#include "SettlementManager.h" // ADDED -- BF-04
#include "EventBus.h"
#include "Engine/World.h"

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

    // BF-04: Reset population
    for (FCivilizationData& Civ : Civs)
    {
        Civ.TotalPopulation = 0;
    }

    const TArray<FSettlementState>& Settlements = SettlementManager->GetAllSettlements();

    for (const FSettlementState& Settlement : Settlements)
    {
        const FWorldTileData* Tile = WMS->GetTileDataPtr(Settlement.WorldTileID);

        int32 TargetCivID = -1;

        if (Tile && Tile->ControllingCivID >= 0)
        {
            TargetCivID = Tile->ControllingCivID;
        }
        else
        {
            // ADDED -- BF-04 FIX:
            // Fallback to Civ 0 when tile has no controlling civ
            TargetCivID = 0;
        }

        for (FCivilizationData& Civ : Civs)
        {
            if (Civ.CivID == TargetCivID)
            {
                Civ.TotalPopulation += Settlement.Population;
                break;
            }
        }
    }

    for (FCivilizationData& Civ : Civs)
    {
        ProcessOneCiv(Civ, StepNumber, World);
    }
}

void UCivilizationResilienceSystem::ProcessOneCiv(
    FCivilizationData& Civ, int32 StepNumber, UWorld* World)
{
    if (Civ.CivID < 0) return;

    FResilienceRecord& Record = Records.FindOrAdd(Civ.CivID);
    Record.CivID = Civ.CivID;

    if (Record.bHasCollapsed) return;

    UWorldEconomySimulation* EconSim = World
        ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;

    const float TerritoryScore = FMath::Clamp(DeriveTerritoryScore(Civ), 0.0f, 1.0f);
    const float EconomyScore = FMath::Clamp(DeriveEconomyScore(Civ, EconSim), 0.0f, 1.0f);
    const float PopulationScore = FMath::Clamp(DerivePopulationScore(Civ), 0.0f, 1.0f);
    const float MilitaryScore = FMath::Clamp(DeriveMilitaryScore(Civ), 0.0f, 1.0f);

    const float Health =
        (TerritoryScore * 0.30f) +
        (EconomyScore * 0.30f) +
        (PopulationScore * 0.20f) +
        (MilitaryScore * 0.20f);

    // ADDED -- BF-04 VALIDATION LOG
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

    if (Health < CollapseThreshold)
    {
        Record.ConsecutiveLowHealthTicks++;

        if (Record.ConsecutiveLowHealthTicks >= ConsecutiveCollapseTicksRequired)
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
    }
    else
    {
        Record.ConsecutiveLowHealthTicks = FMath::Max(0, Record.ConsecutiveLowHealthTicks - 1);
    }
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

    // ADDED -- BF-05: write history event
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