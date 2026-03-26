#include "CommandersDeskManager.h"
#include "WorldEconomySimulation.h"
#include "EventBus.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"

void UCommandersDeskManager::Initialize(FSubsystemCollectionBase& Collection)
{
    // KEPT EXISTING
    Super::Initialize(Collection);

    // KEPT EXISTING
    UE_LOG(LogTemp, Log, TEXT("[CommandersDesk] Initialized."));

    // KEPT EXISTING
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // KEPT EXISTING
    UEventBus* Bus = World->GetSubsystem<UEventBus>();
    if (Bus)
    {
        Bus->OnEventFlushed.AddDynamic(this, &UCommandersDeskManager::OnSimEventFlushed);
    }
}

// ADDED
void UCommandersDeskManager::EnqueueNarrativeReport(const FString& Report)
{
    PendingNarrativeReports.Add(Report);

    UE_LOG(LogTemp, Log, TEXT("[NarrativeReport] %s"), *Report);
}

void UCommandersDeskManager::DrainNarrativeReports()
{
    // KEPT EXISTING
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // KEPT EXISTING
    UWorldEconomySimulation* EconomySim = World->GetSubsystem<UWorldEconomySimulation>();
    if (EconomySim)
    {
        const TArray<FString> Reports = EconomySim->DrainNarrativeReports();
        for (const FString& Report : Reports)
        {
            // MODIFIED
            EnqueueNarrativeReport(Report);
        }
    }

    // ADDED
    if (PendingNarrativeReports.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[NarrativeDrain] No narrative reports pending."));
        return;
    }

    // ADDED
    TArray<FString> ReportsToDrain = PendingNarrativeReports;
    PendingNarrativeReports.Empty();

    // ADDED
    for (const FString& Report : ReportsToDrain)
    {
        UE_LOG(LogTemp, Log, TEXT("[NarrativeDrain] %s"), *Report);
    }
}

void UCommandersDeskManager::OnSimEventFlushed(const FSimulationEvent& Event)
{
    // KEPT EXISTING
    static const FGameplayTag NarrativeBeatTag =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.NarrativeBeat")));

    // KEPT EXISTING
    static const FGameplayTag CivCollapsedTag =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.CivCollapsed")));

    if (Event.EventType == NarrativeBeatTag)
    {
        // MODIFIED
        const FString Report = TEXT("A narrative shift is occurring in the world.");
        EnqueueNarrativeReport(Report);

        // KEPT EXISTING
        UE_LOG(LogTemp, Log, TEXT("[CommandersDesk][Event] A narrative shift is occurring in the world."));
    }
    else if (Event.EventType == CivCollapsedTag)
    {
        // ADDED
        const FString Report =
            FString::Printf(TEXT("Intelligence: Civilization %d has collapsed."), Event.AgentID);
        EnqueueNarrativeReport(Report);

        // KEPT EXISTING
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[CommandersDesk][Event] Intelligence: Civilization %d has collapsed."),
            Event.AgentID
        );
    }
}