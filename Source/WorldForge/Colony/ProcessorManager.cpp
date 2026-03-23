#include "ProcessorManager.h"

#include "Processors/NeedsProcessor.h"
#include "Processors/JobProcessor.h"
#include "Processors/ReservationProcessor.h"
#include "Processors/ConstructionProcessor.h"
#include "Processors/ProductionProcessor.h"
#include "Processors/MoraleProcessor.h"
#include "Processors/HealthProcessor.h"
#include "Processors/IncidentProcessor.h"
#include "Processors/EconomicProcessor.h"

#include "ColonistVisualManager.h"   // NEW: spawn visual actors when agents are registered
#include "EventBus.h"
#include "SimulationTimeManager.h"
#include "WorldForgeTags.h"
#include "HAL/PlatformTime.h"
#include "CultureDriftSystem.h"                // ADDED
#include "CivilizationResilienceSystem.h"      // ADDED

// =====================================================
// CHANGE MADE:
// Required for FGameplayTag::RequestGameplayTag
// =====================================================
#include "GameplayTagContainer.h"

void UProcessorManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<USimulationTimeManager>();

    Super::Initialize(Collection);

    InitializeProcessors();

    // ADDED
    // Cache Section B world-level systems.
    // These are invoked after colony processors and before EventBus flush.
    if (UWorld* World = GetWorld())
    {
        CultureDrift = World->GetSubsystem<UCultureDriftSystem>();
        CivResilience = World->GetSubsystem<UCivilizationResilienceSystem>();
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ProcessorManager] Initialized. %d processors."),
        OrderedProcessors.Num()
    );
}

void UProcessorManager::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    if (USimulationTimeManager* ST = InWorld.GetSubsystem<USimulationTimeManager>())
    {
        ST->OnSimulationStep.AddDynamic(this, &UProcessorManager::RunSimulationStep);
        UE_LOG(LogTemp, Log, TEXT("[ProcessorManager] Bound to SimulationTimeManager."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcessorManager] SimulationTimeManager not found in OnWorldBeginPlay!"));
    }
}

void UProcessorManager::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        if (USimulationTimeManager* ST = World->GetSubsystem<USimulationTimeManager>())
        {
            ST->OnSimulationStep.RemoveDynamic(this, &UProcessorManager::RunSimulationStep);
        }
    }

    SettlementAgentScratch.Reset();

    // ADDED
    CultureDrift = nullptr;
    CivResilience = nullptr;

    Super::Deinitialize();
}

void UProcessorManager::InitializeProcessors()
{
    OrderedProcessors.Add(NewObject<UNeedsProcessor>(this));
    OrderedProcessors.Add(NewObject<UJobProcessor>(this));
    OrderedProcessors.Add(NewObject<UReservationProcessor>(this));
    OrderedProcessors.Add(NewObject<UConstructionProcessor>(this));
    OrderedProcessors.Add(NewObject<UProductionProcessor>(this));
    OrderedProcessors.Add(NewObject<UMoraleProcessor>(this));
    OrderedProcessors.Add(NewObject<UHealthProcessor>(this));
    OrderedProcessors.Add(NewObject<UIncidentProcessor>(this));
    OrderedProcessors.Add(NewObject<UEconomicProcessor>(this));

    ProcessorTimingsMs.SetNum(OrderedProcessors.Num());
    ProcessorNames.Reset();

    for (UBaseProcessor* P : OrderedProcessors)
    {
        ProcessorNames.Add(P ? P->GetProcessorName() : NAME_None);
    }
}

void UProcessorManager::RunSimulationStep(int32 StepNumber)
{
    if (ActiveSettlementIDs.Num() == 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (float& T : ProcessorTimingsMs)
    {
        T = 0.f;
    }

    for (int32 SID : ActiveSettlementIDs)
    {
        for (int32 i = 0; i < OrderedProcessors.Num(); i++)
        {
            if (!IsValid(OrderedProcessors[i]))
            {
                UE_LOG(LogTemp, Warning, TEXT("[ProcessorManager] Invalid processor at index %d"), i);
                continue;
            }

            const double T0 = FPlatformTime::Seconds();
            OrderedProcessors[i]->ProcessStep(StepNumber, SID, Agents, World);
            ProcessorTimingsMs[i] += static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);
        }

        // ADDED
        // Section B world-level systems must run after all colony processors
        // and before EventBus flush so queued events land in the same flush.
        if (CultureDrift)
        {
            CultureDrift->OnSimStep(StepNumber, SID, Agents, World);
        }

        if (CivResilience)
        {
            CivResilience->OnSimStep(StepNumber, SID, Agents, World);
        }
    }

    if (StepNumber % 25 == 0)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ProcessorManager] Completed simulation step %d for %d settlement(s)."),
            StepNumber,
            ActiveSettlementIDs.Num()
        );
    }

    if (UEventBus* Bus = World->GetSubsystem<UEventBus>())
    {
        Bus->FlushPendingEvents();
    }
}

void UProcessorManager::RegisterSettlement(int32 ID)
{
    if (!ActiveSettlementIDs.Contains(ID))
    {
        ActiveSettlementIDs.Add(ID);
        UE_LOG(LogTemp, Log, TEXT("[ProcessorManager] Settlement %d registered."), ID);
    }
}

void UProcessorManager::RegisterAgent(const FAgentData& A)
{
    for (const FAgentData& X : Agents)
    {
        if (X.AgentID == A.AgentID)
        {
            return;
        }
    }

    FAgentData NewAgent = A;

    // =====================================================
    // CHANGE MADE:
    // Ensure agent always has a valid RaceTag for visual system.
    // Without this, Blueprint resolves None → no sprite renders.
    // =====================================================
    if (!NewAgent.RaceTag.IsValid())
    {
        NewAgent.RaceTag = FGameplayTag::RequestGameplayTag(
            FName("WorldForge.Race.Dwarf")
        );
    }

    // Test-level fallback so agents created from Blueprint or other paths
    // are still eligible for basic harvest jobs when no explicit skills
    // were assigned before registration.
    if (NewAgent.Skills.Num() == 0)
    {
        NewAgent.Skills.Add(TAG_Skill_Farming, 25.f);
        NewAgent.Skills.Add(TAG_Skill_Hauling, 25.f);
        NewAgent.Skills.Add(TAG_Skill_Mining, 25.f);
    }

    Agents.Add(NewAgent);
    RegisterSettlement(NewAgent.SettlementID);

    // NEW:
    // Automatically make sure visual actors exist for this settlement as soon as
    // agents are registered. This removes the need for a separate Blueprint call.
    if (UWorld* World = GetWorld())
    {
        if (UColonistVisualManager* VisualManager = World->GetSubsystem<UColonistVisualManager>())
        {
            VisualManager->SpawnColonistActors(NewAgent.SettlementID);
        }
    }
}

void UProcessorManager::SpawnTestAgent(int32 AgentID, int32 SettlementID, FIntPoint GridPos)
{
    FAgentData Agent;
    Agent.AgentID = AgentID;
    Agent.SettlementID = SettlementID;
    Agent.State = EAgentState::Idle;
    Agent.FoodNeed = 100.f;
    Agent.SleepNeed = 100.f;
    Agent.SocialNeed = 70.f;
    Agent.SafetyNeed = 70.f;
    Agent.PurposeNeed = 70.f;
    Agent.CurrentGridPos = GridPos;
    Agent.CurrentNavTarget = GridPos;

    // Minimal test bootstrap skills so fallback harvest jobs can be assigned.
    Agent.Skills.Add(TAG_Skill_Farming, 25.f);
    Agent.Skills.Add(TAG_Skill_Hauling, 25.f);
    Agent.Skills.Add(TAG_Skill_Mining, 25.f);

    // =====================================================
    // CHANGE MADE:
    // Assign default race so test agents render correctly
    // =====================================================
    Agent.RaceTag = FGameplayTag::RequestGameplayTag(
        FName("WorldForge.Race.Dwarf")
    );

    RegisterAgent(Agent);
    RegisterSettlement(SettlementID);
}

void UProcessorManager::UnregisterAgent(int32 ID)
{
    Agents.RemoveAll([ID](const FAgentData& A)
        {
            return A.AgentID == ID;
        });
}

FAgentData* UProcessorManager::GetMutableAgent(int32 ID)
{
    for (FAgentData& A : Agents)
    {
        if (A.AgentID == ID)
        {
            return &A;
        }
    }

    return nullptr;
}

const FAgentData* UProcessorManager::GetAgent(int32 ID) const
{
    for (const FAgentData& A : Agents)
    {
        if (A.AgentID == ID)
        {
            return &A;
        }
    }

    return nullptr;
}

const TArray<FAgentData>& UProcessorManager::GetAgentsForSettlement(int32 SettlementID) const
{
    SettlementAgentScratch.Reset();

    for (const FAgentData& Agent : Agents)
    {
        if (Agent.SettlementID == SettlementID)
        {
            SettlementAgentScratch.Add(Agent);
        }
    }

    return SettlementAgentScratch;
}

void UProcessorManager::RestoreAgents(const TArray<FAgentData>& Saved)
{
    Agents = Saved;
    ActiveSettlementIDs.Reset();

    for (const FAgentData& Agent : Agents)
    {
        if (Agent.SettlementID >= 0 && !ActiveSettlementIDs.Contains(Agent.SettlementID))
        {
            ActiveSettlementIDs.Add(Agent.SettlementID);
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ProcessorManager] Restored %d agents across %d settlement(s)."),
        Agents.Num(),
        ActiveSettlementIDs.Num()
    );
}