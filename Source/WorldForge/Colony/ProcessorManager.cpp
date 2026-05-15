#include "ProcessorManager.h"

#include "Processors/NeedsProcessor.h"
#include "Processors/JobProcessor.h"
#include "Processors/MiningProcessor.h"
#include "Processors/ReservationProcessor.h"
#include "Processors/ConstructionProcessor.h"
#include "Processors/ProductionProcessor.h"
#include "Processors/MoraleProcessor.h"
#include "Processors/HealthProcessor.h"
#include "Processors/IncidentProcessor.h"
#include "Processors/EconomicProcessor.h"

#include "ColonistVisualManager.h"
#include "EventBus.h"
#include "SimulationTimeManager.h"
#include "WorldForgeTags.h"
#include "HAL/PlatformTime.h"
#include "CultureDriftSystem.h"
#include "CivilizationResilienceSystem.h"
#include "OverworldSubsystem.h"
#include "DiplomacySubsystem.h"
#include "NavalSystem.h"
#include "CosmicSimulationSubsystem.h"
#include "Visual/VoxelSpawnSubsystem.h"
#include "../Core/Narrative/CommandersDeskManager.h"
#include "../Farming/FarmingSubsystem.h"  // ════ Wild Harvest Integration ════

// Required for FGameplayTag::RequestGameplayTag
#include "GameplayTagContainer.h"

namespace
{
    TArray<FGameplayTag> BuildDefaultJobPriorityTags()
    {
        return TArray<FGameplayTag>{
            TAG_Job_Eat,
            TAG_Job_Sleep,
            TAG_Job_Haul,
            TAG_Job_Build,
            TAG_Job_Mine,
            TAG_Job_Harvest_Food,
            TAG_Job_Harvest_Wood,
            TAG_Job_Harvest_Stone,
            TAG_Job_Cook,
            TAG_Job_Craft,
            TAG_Job_Farm_PrepSoil,
            TAG_Job_Farm_Plant,
            TAG_Job_Farm_Water,
            TAG_Job_Farm_Harvest,
            TAG_Job_Farm_Clear,
            TAG_Job_Farm_Fertilize
        };
    }

    void EnsureDefaultJobPriorities(FAgentData& Agent)
    {
        for (const FGameplayTag& JobTag : BuildDefaultJobPriorityTags())
        {
            if (JobTag.IsValid() && !Agent.JobPriorities.Contains(JobTag))
            {
                Agent.JobPriorities.Add(JobTag, 4);
            }
        }

        // MVP/testing: all known jobs are high priority so any player-created
        // designation can be picked up immediately. Needs can still override
        // behavior through HandleCriticalNeeds in JobProcessor.
        for (const FGameplayTag& JobTag : BuildDefaultJobPriorityTags())
        {
            if (JobTag.IsValid())
            {
                Agent.JobPriorities.FindOrAdd(JobTag) = 4;
            }
        }
    }

    FColonistInspectionData MakeInspectionData(const FAgentData& Agent)
    {
        FColonistInspectionData Data;
        Data.AgentID = Agent.AgentID;
        Data.SettlementID = Agent.SettlementID;
        Data.DisplayName = Agent.DisplayName;
        Data.RaceTag = Agent.RaceTag;
        Data.State = Agent.State;
        Data.CurrentJobTag = Agent.CurrentJobTag;
        Data.CurrentGridPos = Agent.CurrentGridPos;
        Data.CurrentNavTarget = Agent.CurrentNavTarget;
        Data.FoodNeed = Agent.FoodNeed;
        Data.SleepNeed = Agent.SleepNeed;
        Data.SocialNeed = Agent.SocialNeed;
        Data.SafetyNeed = Agent.SafetyNeed;
        Data.PurposeNeed = Agent.PurposeNeed;
        Data.MoodScore = Agent.MoodScore;
        Data.Health = Agent.Health;
        Data.MaxHealth = Agent.MaxHealth;
        Data.CurrentEfficiency = Agent.CurrentEfficiency;
        Data.bInjured = Agent.bInjured;
        Data.TraitTags = Agent.TraitTags;
        Data.Skills = Agent.Skills;
        Data.EquippedItems = Agent.EquippedItems;

        for (const TPair<FGameplayTag, int32>& Pair : Agent.JobPriorities)
        {
            FColonistJobPriority Entry;
            Entry.JobTag = Pair.Key;
            Entry.Priority = Pair.Value;
            Data.JobPriorities.Add(Entry);
        }

        return Data;
    }
}

void UProcessorManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<USimulationTimeManager>();

    Super::Initialize(Collection);

    InitializeProcessors();

    // Cache world-level simulation subsystems.
    // Note: UVoxelSpawnSubsystem may initialize after ProcessorManager,
    // so we also lazily reacquire it during RunSimulationStep().
    if (UWorld* World = GetWorld())
    {
        CultureDrift = World->GetSubsystem<UCultureDriftSystem>();
        CivResilience = World->GetSubsystem<UCivilizationResilienceSystem>();
        Overworld = World->GetSubsystem<UOverworldSubsystem>();
        Diplomacy = World->GetSubsystem<UDiplomacySubsystem>();
        Naval = World->GetSubsystem<UNavalSystem>();
        CosmicSim = World->GetSubsystem<UCosmicSimulationSubsystem>();
        VoxelSpawn = World->GetSubsystem<UVoxelSpawnSubsystem>();
        FarmingSub = World->GetSubsystem<UFarmingSubsystem>();  // ════ Wild Harvest Integration ════
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

    CultureDrift = nullptr;
    CivResilience = nullptr;
    Overworld = nullptr;
    Diplomacy = nullptr;
    Naval = nullptr;
    CosmicSim = nullptr;
    VoxelSpawn = nullptr;
    FarmingSub = nullptr;  // ════ Wild Harvest Integration ════

    Super::Deinitialize();
}

void UProcessorManager::InitializeProcessors()
{
    OrderedProcessors.Add(NewObject<UNeedsProcessor>(this));
    OrderedProcessors.Add(NewObject<UJobProcessor>(this));
    OrderedProcessors.Add(NewObject<UMiningProcessor>(this));
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

    // VSS may initialize after ProcessorManager, so reacquire if needed.
    if (!VoxelSpawn)
    {
        VoxelSpawn = World->GetSubsystem<UVoxelSpawnSubsystem>();
    }

    // FarmingSubsystem can also initialize after ProcessorManager.
    // If FarmingSub was null during Initialize(), reacquire it here so
    // farm zones can tick and post Plant/Water/Harvest jobs.
    if (!FarmingSub)
    {
        FarmingSub = World->GetSubsystem<UFarmingSubsystem>();

        if (FarmingSub)
        {
            UE_LOG(LogTemp, Log, TEXT("[ProcessorManager] Late-bound FarmingSubsystem."));
        }
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

        // Section C overworld system runs after Section B systems
        // and before EventBus flush.
        if (Overworld)
        {
            Overworld->OnSimStep(StepNumber, SID, Agents, World);
        }

        // Section D diplomacy system runs after Overworld
        // and before EventBus flush.
        if (Diplomacy)
        {
            Diplomacy->OnSimStep(StepNumber, SID, Agents, World);
        }

        // Section E naval system runs after Diplomacy
        // and before EventBus flush.
        if (Naval)
        {
            Naval->OnSimStep(StepNumber, SID, Agents, World);
        }

        // Section F cosmic system runs after Naval
        // and before EventBus flush.
        if (CosmicSim)
        {
            CosmicSim->OnSimStep(StepNumber, SID, Agents, World);
        }

        // ════ Wild Harvest Integration ════
        // Farming tick runs after all colony processors + world-level systems,
        // before EventBus flush, so farm job events land in the same flush.
        if (FarmingSub)
        {
            FarmingSub->TickFarmZones(StepNumber);

            if (StepNumber % 10 == 0)
            {
                UE_LOG(LogTemp, Verbose, TEXT("[ProcessorManager] TickFarmZones called at Step=%d Settlement=%d"), StepNumber, SID);
            }
        }
        else if (StepNumber % 10 == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("[ProcessorManager] FarmingSubsystem still null at Step=%d"), StepNumber);
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

    if (VoxelSpawn)
    {
        VoxelSpawn->OnSimStep(StepNumber);
    }

    // H2 authoritative desk path:
    // drain economy narrative reports only after all processors have run
    // and after EventBus flush has surfaced any cosmic narrative beats.
    if (UCommandersDeskManager* Desk = World->GetSubsystem<UCommandersDeskManager>())
    {
        Desk->DrainNarrativeReports();
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

    // Ensure agent always has a valid RaceTag for visual system.
    // Without this, Blueprint resolves None -> no sprite renders.
    if (!NewAgent.RaceTag.IsValid())
    {
        NewAgent.RaceTag = FGameplayTag::RequestGameplayTag(
            FName("WorldForge.Race.Dwarf")
        );
    }

    if (NewAgent.DisplayName.IsEmpty())
    {
        NewAgent.DisplayName = FString::Printf(TEXT("Colonist %d"), NewAgent.AgentID);
    }

    EnsureDefaultJobPriorities(NewAgent);

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
    Agent.DisplayName = FString::Printf(TEXT("Test Colonist %d"), AgentID);
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

    // Assign default race so test agents render correctly.
    Agent.RaceTag = FGameplayTag::RequestGameplayTag(
        FName("WorldForge.Race.Dwarf")
    );

    EnsureDefaultJobPriorities(Agent);

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

bool UProcessorManager::GetAgentInspectionData(int32 AgentID, FColonistInspectionData& OutData) const
{
    if (const FAgentData* Agent = GetAgent(AgentID))
    {
        OutData = MakeInspectionData(*Agent);
        return true;
    }

    OutData = FColonistInspectionData();
    return false;
}

bool UProcessorManager::SetAgentJobPriority(int32 AgentID, FGameplayTag JobTag, int32 Priority)
{
    if (!JobTag.IsValid())
    {
        return false;
    }

    FAgentData* Agent = GetMutableAgent(AgentID);
    if (!Agent)
    {
        return false;
    }

    Agent->JobPriorities.FindOrAdd(JobTag) = FMath::Clamp(Priority, 0, 4);
    return true;
}

int32 UProcessorManager::GetAgentJobPriority(int32 AgentID, FGameplayTag JobTag) const
{
    if (!JobTag.IsValid())
    {
        return 0;
    }

    const FAgentData* Agent = GetAgent(AgentID);
    if (!Agent)
    {
        return 0;
    }

    const int32* Found = Agent->JobPriorities.Find(JobTag);
    return Found ? *Found : 4;
}

TArray<FColonistJobPriority> UProcessorManager::GetAgentJobPriorities(int32 AgentID) const
{
    TArray<FColonistJobPriority> Result;

    const FAgentData* Agent = GetAgent(AgentID);
    if (!Agent)
    {
        return Result;
    }

    for (const FGameplayTag& JobTag : BuildDefaultJobPriorityTags())
    {
        if (!JobTag.IsValid())
        {
            continue;
        }

        FColonistJobPriority Entry;
        Entry.JobTag = JobTag;
        Entry.Priority = Agent->JobPriorities.FindRef(JobTag);
        if (Entry.Priority <= 0 && !Agent->JobPriorities.Contains(JobTag))
        {
            Entry.Priority = 4;
        }
        Result.Add(Entry);
    }

    return Result;
}

void UProcessorManager::ResetAgentJobPrioritiesToDefault(int32 AgentID)
{
    if (FAgentData* Agent = GetMutableAgent(AgentID))
    {
        Agent->JobPriorities.Reset();
        EnsureDefaultJobPriorities(*Agent);
    }
}

TArray<FGameplayTag> UProcessorManager::GetDefaultJobPriorityTags() const
{
    return BuildDefaultJobPriorityTags();
}

void UProcessorManager::RestoreAgents(const TArray<FAgentData>& Saved)
{
    Agents = Saved;
    ActiveSettlementIDs.Reset();

    for (FAgentData& Agent : Agents)
    {
        if (Agent.DisplayName.IsEmpty())
        {
            Agent.DisplayName = FString::Printf(TEXT("Colonist %d"), Agent.AgentID);
        }

        EnsureDefaultJobPriorities(Agent);

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