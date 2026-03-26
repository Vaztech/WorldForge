#include "CivilizationResilienceSystem.h"
#include "WorldMapSubsystem.h"
#include "WorldEconomySimulation.h"
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
    (void)SettlementID; // ADDED
    (void)Agents;       // ADDED

    if (!World) return;
    if (StepNumber % LivingWorldTickInterval != 0) return;

    UWorldMapSubsystem* WMS = World->GetSubsystem<UWorldMapSubsystem>();
    if (!WMS) return;

    TArray<FCivilizationData>& Civs = WMS->GetMutableCivilizations();
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

    const float Health = ComputeCivHealth(Civ);

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
    // MODIFIED -- BF-02: use real controlled territory instead of TechLevel proxy.
    // ControlledTileIDs is maintained by UWorldMapSubsystem each world tick.
    // 20 tiles = full score; 0 tiles = zero.
    return FMath::Clamp(
        static_cast<float>(Civ.ControlledTileIDs.Num()) / 20.0f,
        0.0f,
        1.0f);
}

float UCivilizationResilienceSystem::DeriveEconomyScore(
    const FCivilizationData& Civ,
    UWorldEconomySimulation* EconSim) const
{
    if (!EconSim) return 0.5f;

    const FGovernmentEconomicProfile* Gov = EconSim->GetGovernment(Civ.CivID);
    if (!Gov) return 0.5f;

    return FMath::Clamp(Gov->EnforcementBudget, 0.0f, 1.0f);
}

float UCivilizationResilienceSystem::DerivePopulationScore(const FCivilizationData& Civ) const
{
    (void)Civ; // ADDED
    return 0.5f; // ADDED
}

float UCivilizationResilienceSystem::DeriveMilitaryScore(const FCivilizationData& Civ) const
{
    // MODIFIED
    // Actual project uses FCultureData fields, not TArray<float>.
    return FMath::Clamp(Civ.CulturalDNA.Militarism / 100.0f, 0.0f, 1.0f);
}

ECivilizationCollapseType UCivilizationResilienceSystem::DetermineCollapseType(
    const FCivilizationData& Civ) const
{
    // MODIFIED
    // Actual project uses FCultureData fields, not TArray<float>.
    const float Militarism = Civ.CulturalDNA.Militarism;
    const float Expansion = Civ.CulturalDNA.ExpansionDrive;

    if (Militarism > 70.0f) return ECivilizationCollapseType::Fragmentation;
    if (Expansion > 60.0f)  return ECivilizationCollapseType::Diaspora;
    return ECivilizationCollapseType::EconomicCollapse;
}

void UCivilizationResilienceSystem::TriggerCollapse(
    FCivilizationData& Civ, int32 StepNumber, UWorld* World)
{
    // MODIFIED
    // Keep the actual civilization state in sync with the resilience record.
    Civ.bCollapsed = true;

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
    E.AgentID = Civ.CivID; // MODIFIED
    // Actual FSimulationEvent has no IntPayload in this project.
    // Reuse AgentID as the existing integer carrier for CivID.
    E.StepNumber = StepNumber;
    E.Payload = FGameplayTag(); // ADDED
    E.FloatPayload = 0.0f;      // ADDED

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
    E.AgentID = Civ.CivID; // MODIFIED
    // Actual FSimulationEvent has no IntPayload in this project.
    // Reuse AgentID as the existing integer carrier for CivID.
    E.StepNumber = StepNumber;
    E.Payload = FGameplayTag(); // ADDED
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