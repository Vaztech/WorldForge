#include "JobProcessor.h"
#include "ReservationSystem.h"
#include "SettlementManager.h"
#include "NavigationContext.h"
#include "EventBus.h"
#include "ProcessorManager.h"
#include "ProductionProcessor.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"
#include "Traits/WorldForgeTraitTypes.h"
#include "Engine/DataTable.h"

// ====================================================================
// TRAIT SUPPORT
// JobProcessor uses traits for job-selection scoring only.
// Production speed itself is still handled by ProductionProcessor.
// ====================================================================
namespace
{
    UDataTable* GetWorldForgeTraitTable()
    {
        static UDataTable* CachedTraitTable = nullptr;
        static bool bAttemptedLoad = false;

        if (!bAttemptedLoad)
        {
            bAttemptedLoad = true;

            // Future us:
            // Update this path if DT_WorldForgeTraits is moved.
            CachedTraitTable = LoadObject<UDataTable>(
                nullptr,
                TEXT("/Game/Data/DT_WorldForgeTraits.DT_WorldForgeTraits"));

            if (!CachedTraitTable)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[JobProcessor] Could not load DT_WorldForgeTraits at /Game/Data/DT_WorldForgeTraits. Trait job scoring will be skipped."));
            }
        }

        return CachedTraitTable;
    }

    FName GetLeafRowNameFromTraitTag(const FGameplayTag& TraitTag)
    {
        FString TagString = TraitTag.ToString();

        FString Left;
        FString Right;

        if (TagString.Split(TEXT("."), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            return FName(*Right);
        }

        return TraitTag.GetTagName();
    }

    const FWorldForgeTraitDefinition* FindTraitDefinition(
        UDataTable* TraitTable,
        const FGameplayTag& TraitTag)
    {
        if (!TraitTable || !TraitTag.IsValid())
        {
            return nullptr;
        }

        // First try full tag row name:
        // WorldForge.Trait.Lucky
        if (const FWorldForgeTraitDefinition* TraitDef =
            TraitTable->FindRow<FWorldForgeTraitDefinition>(
                TraitTag.GetTagName(),
                TEXT("JobProcessor Trait FullTag Lookup"),
                false))
        {
            return TraitDef;
        }

        // Then try short row name:
        // Lucky
        return TraitTable->FindRow<FWorldForgeTraitDefinition>(
            GetLeafRowNameFromTraitTag(TraitTag),
            TEXT("JobProcessor Trait Leaf Lookup"),
            false);
    }

    FAgentData BuildTraitScoredAgentForJobSelection(const FAgentData& Agent)
    {
        FAgentData ScoredAgent = Agent;

        UDataTable* TraitTable = GetWorldForgeTraitTable();
        if (!TraitTable || Agent.TraitTags.IsEmpty())
        {
            return ScoredAgent;
        }

        float WorkEfficiencyMultiplier = 1.0f;

        for (const FGameplayTag& TraitTag : Agent.TraitTags)
        {
            const FWorldForgeTraitDefinition* TraitDef =
                FindTraitDefinition(TraitTable, TraitTag);

            if (!TraitDef)
            {
                continue;
            }

            // WorkEfficiencyMultiplier affects how attractive this agent is for jobs
            // if JobQueue scoring considers CurrentEfficiency.
            WorkEfficiencyMultiplier *= TraitDef->WorkEfficiencyMultiplier;

            // SkillModifiers are additive temporary scoring bonuses.
            // They do not permanently change the agent's saved skills.
            for (const TPair<FGameplayTag, float>& SkillMod : TraitDef->SkillModifiers)
            {
                if (!SkillMod.Key.IsValid())
                {
                    continue;
                }

                float& SkillValue = ScoredAgent.Skills.FindOrAdd(SkillMod.Key);
                SkillValue = FMath::Clamp(SkillValue + SkillMod.Value, 0.0f, 100.0f);
            }
        }

        ScoredAgent.CurrentEfficiency = FMath::Clamp(
            ScoredAgent.CurrentEfficiency * WorkEfficiencyMultiplier,
            0.0f,
            2.0f);

        return ScoredAgent;
    }
}

// ====================================================================
// MAIN PROCESS STEP
// Runs once per simulation step per settlement.
// Responsible for:
// 1. Generating jobs if needed
// 2. Updating agent movement
// 3. Assigning jobs to idle agents
// 4. Falling back to a clean Idle state when no valid job exists
// ====================================================================
void UJobProcessor::ProcessStep(
    int32 StepNumber,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World)
{
    if (!World)
    {
        return;
    }

    JobQueue.PruneCompletedJobs();

    if (!JobQueue.HasPendingJobs(SettlementID) && (StepNumber - LastIdleGenStep) > 10)
    {
        GenerateIdleHarvestJobs(SettlementID, StepNumber, World);
        LastIdleGenStep = StepNumber;
    }

    for (FAgentData& Agent : Agents)
    {
        if (Agent.SettlementID != SettlementID)
        {
            continue;
        }

        if (Agent.State == EAgentState::Dead || Agent.State == EAgentState::Incapacitated)
        {
            continue;
        }

        if (Agent.State == EAgentState::MovingToTarget)
        {
            TickMovement(Agent, StepNumber, World);
            continue;
        }

        if (Agent.State == EAgentState::Working ||
            Agent.State == EAgentState::Carrying ||
            Agent.State == EAgentState::Eating ||
            Agent.State == EAgentState::Sleeping ||
            Agent.State == EAgentState::ReservingTarget)
        {
            continue;
        }

        if (HandleCriticalNeeds(Agent, SettlementID, StepNumber, World))
        {
            continue;
        }

        if (TryAssignQueuedJob(Agent, SettlementID, StepNumber, World))
        {
            continue;
        }

        Agent.State = EAgentState::Idle;
        Agent.CurrentJobTag = FGameplayTag();
        Agent.CurrentJobBuildingTag = FGameplayTag();
        Agent.ActiveReservationID = -1;
    }
}

// ====================================================================
// MOVEMENT SYSTEM
// Moves agent one grid cell per simulation step toward target.
// ====================================================================
void UJobProcessor::TickMovement(
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    const FIntPoint Target = Agent.CurrentNavTarget;

    if (Agent.CurrentGridPos == Target)
    {
        OnAgentArrived(Agent, StepNumber, World);
        return;
    }

    FIntPoint Pos = Agent.CurrentGridPos;

    for (int32 i = 0; i < MovementCellsPerStep; ++i)
    {
        if (Pos == Target)
        {
            break;
        }

        if (Pos.X != Target.X)
        {
            Pos.X += (Target.X > Pos.X) ? 1 : -1;
        }
        else if (Pos.Y != Target.Y)
        {
            Pos.Y += (Target.Y > Pos.Y) ? 1 : -1;
        }
    }

    Agent.CurrentGridPos = Pos;

    if (Agent.CurrentGridPos == Target)
    {
        OnAgentArrived(Agent, StepNumber, World);
    }
}

// ====================================================================
// ARRIVAL HANDLER
// ====================================================================
void UJobProcessor::OnAgentArrived(
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    const FGameplayTag JobTag = Agent.CurrentJobTag;

    if (JobTag == TAG_Job_Eat)
    {
        Agent.State = EAgentState::Eating;
        Agent.FoodNeed = 100.f;

        if (USettlementManager* SM = World->GetSubsystem<USettlementManager>())
        {
            SM->ConsumeFromStockpile(Agent.SettlementID, TAG_Item_Food, 1);
        }

        Agent.State = EAgentState::Idle;
        Agent.CurrentJobTag = FGameplayTag();
        Agent.CurrentJobBuildingTag = FGameplayTag();
        Agent.ActiveReservationID = -1;
        return;
    }

    if (JobTag == TAG_Job_Sleep)
    {
        Agent.State = EAgentState::Sleeping;
        Agent.SleepNeed = 100.f;

        Agent.State = EAgentState::Idle;
        Agent.CurrentJobTag = FGameplayTag();
        Agent.CurrentJobBuildingTag = FGameplayTag();
        Agent.ActiveReservationID = -1;
        return;
    }

    if (JobTag == TAG_Job_Cook ||
        JobTag == TAG_Job_Craft ||
        JobTag == TAG_Job_Build ||
        JobTag.MatchesTag(TAG_Job_Harvest))
    {
        Agent.State = EAgentState::Working;

        if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
        {
            if (UProductionProcessor* PP = PM->GetTypedProcessor<UProductionProcessor>())
            {
                PP->StartProductionTask(
                    Agent.AgentID,
                    Agent.SettlementID,
                    JobTag,
                    Agent.CurrentJobBuildingTag);
            }
        }
    }
}

// ====================================================================
// CRITICAL NEEDS HANDLER
// ====================================================================
bool UJobProcessor::HandleCriticalNeeds(
    FAgentData& Agent,
    int32 SettlementID,
    int32 StepNumber,
    UWorld* World)
{
    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    UReservationSystem* RS = World->GetSubsystem<UReservationSystem>();

    if (!SM || !RS)
    {
        return false;
    }

    if (Agent.FoodNeed < FoodCriticalThreshold)
    {
        if (!SM->HasStockpile(SettlementID, TAG_Item_Food, 1))
        {
            return false;
        }

        const int32 ReservationID = RS->TryReserve(
            Agent.AgentID,
            0,
            TAG_Item_Food,
            EReservationType::Item,
            SettlementID,
            StepNumber,
            30);

        if (ReservationID < 0)
        {
            return false;
        }

        FJobRequest EatReq;
        EatReq.SettlementID = SettlementID;
        EatReq.JobTag = TAG_Job_Eat;
        EatReq.TargetItemTag = TAG_Item_Food;
        EatReq.Priority = EJobPriority::Emergency;
        EatReq.PostedOnStep = StepNumber;
        EatReq.TargetGridPos = Agent.CurrentGridPos;

        if (FJobRequest* Job = PostJob(EatReq))
        {
            AssignJobToAgent(Agent, *Job, ReservationID, StepNumber, World);
        }

        return true;
    }

    if (Agent.SleepNeed < SleepCriticalThreshold)
    {
        const int32 ReservationID = RS->TryReserve(
            Agent.AgentID,
            Agent.AgentID,
            FGameplayTag(),
            EReservationType::Bed,
            SettlementID,
            StepNumber,
            60);

        if (ReservationID < 0)
        {
            return false;
        }

        FJobRequest SleepReq;
        SleepReq.SettlementID = SettlementID;
        SleepReq.JobTag = TAG_Job_Sleep;
        SleepReq.Priority = EJobPriority::Emergency;
        SleepReq.PostedOnStep = StepNumber;
        SleepReq.TargetGridPos = Agent.CurrentGridPos;

        if (FJobRequest* Job = PostJob(SleepReq))
        {
            AssignJobToAgent(Agent, *Job, ReservationID, StepNumber, World);
        }

        return true;
    }

    return false;
}

// ====================================================================
// JOB ASSIGNMENT
// Finds best job from queue and assigns it to agent.
//
// Trait hook:
// We create a temporary scoring copy of the agent with trait skill bonuses
// and work-efficiency modifiers. The real Agent is not permanently changed.
// ====================================================================
bool UJobProcessor::TryAssignQueuedJob(
    FAgentData& Agent,
    int32 SettlementID,
    int32 StepNumber,
    UWorld* World)
{
    UReservationSystem* RS = World->GetSubsystem<UReservationSystem>();
    if (!RS)
    {
        return false;
    }

    const FAgentData ScoredAgent = BuildTraitScoredAgentForJobSelection(Agent);

    FJobRequest* Job = JobQueue.FindBestJobForAgent(
        Agent.AgentID,
        ScoredAgent,
        SettlementID);

    if (!Job)
    {
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[JobProcessor] Agent %d evaluating job %s"),
        Agent.AgentID,
        *Job->JobTag.ToString());

    int32 ReservationID = -1;

    if (Job->TargetID >= 0)
    {
        ReservationID = RS->TryReserve(
            Agent.AgentID,
            Job->TargetID,
            Job->TargetItemTag,
            EReservationType::Job,
            SettlementID,
            StepNumber,
            50);

        if (ReservationID < 0)
        {
            return false;
        }
    }

    AssignJobToAgent(Agent, *Job, ReservationID, StepNumber, World);
    return true;
}

// ====================================================================
// JOB ASSIGNMENT IMPLEMENTATION
// ====================================================================
void UJobProcessor::AssignJobToAgent(
    FAgentData& Agent,
    FJobRequest& Job,
    int32 ReservationID,
    int32 StepNumber,
    UWorld* World)
{
    JobQueue.AssignJob(Job.JobID, Agent.AgentID);

    Agent.CurrentJobTag = Job.JobTag;
    Agent.ActiveReservationID = ReservationID;
    Agent.CurrentNavTarget = Job.TargetGridPos;
    Agent.CurrentJobBuildingTag = Job.TargetBuildingTag;

    Agent.State = EAgentState::MovingToTarget;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[JobProcessor] Agent %d assigned job %s -> Target (%d,%d) Step=%d"),
        Agent.AgentID,
        *Job.JobTag.ToString(),
        Job.TargetGridPos.X,
        Job.TargetGridPos.Y,
        StepNumber);

    if (Job.TargetGridPos == Agent.CurrentGridPos)
    {
        OnAgentArrived(Agent, StepNumber, World);
    }

    if (UEventBus* Bus = World->GetSubsystem<UEventBus>())
    {
        FSimulationEvent Event;
        Event.EventType = TAG_Sim_Event_JobAssigned;
        Event.SettlementID = Agent.SettlementID;
        Event.AgentID = Agent.AgentID;
        Event.StepNumber = StepNumber;
        Event.Payload = Job.JobTag;
        Bus->Dispatch(Event);
    }
}

// ====================================================================
// GENERATE IDLE HARVEST JOBS
// ====================================================================
void UJobProcessor::GenerateIdleHarvestJobs(
    int32 SettlementID,
    int32 StepNumber,
    UWorld* World)
{
    USettlementManager* SM = World ? World->GetSubsystem<USettlementManager>() : nullptr;
    UNavigationContext* Nav = World ? World->GetSubsystem<UNavigationContext>() : nullptr;

    if (!SM)
    {
        return;
    }

    const int32 FoodAmt = SM->GetStockpileAmount(SettlementID, TAG_Item_Food);
    const int32 WoodAmt = SM->GetStockpileAmount(SettlementID, TAG_Item_Wood);
    const int32 StoneAmt = SM->GetStockpileAmount(SettlementID, TAG_Item_Stone);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[JobProcessor] Idle job check Settlement=%d Step=%d Food=%d Wood=%d Stone=%d"),
        SettlementID,
        StepNumber,
        FoodAmt,
        WoodAmt,
        StoneAmt);

    auto GetSpreadTarget = [&](int32 BaseX, int32 BaseY) -> FIntPoint
        {
            const int32 MaxAttempts = 6;

            for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
            {
                const int32 OffsetX = FMath::RandRange(-6, 6);
                const int32 OffsetY = FMath::RandRange(-6, 6);

                const FIntPoint Candidate(BaseX + OffsetX, BaseY + OffsetY);

                if (Nav && Nav->IsCellWalkable(SettlementID, Candidate))
                {
                    return Candidate;
                }
            }

            return FIntPoint(BaseX, BaseY);
        };

    if (FoodAmt < 50)
    {
        for (int32 i = 0; i < 3; ++i)
        {
            FJobRequest Job;
            Job.SettlementID = SettlementID;
            Job.PostedOnStep = StepNumber;
            Job.JobTag = TAG_Job_Harvest_Food;
            Job.RequiredSkill = TAG_Skill_Farming;
            Job.Priority = (FoodAmt < 20) ? EJobPriority::High : EJobPriority::Normal;
            Job.TargetGridPos = GetSpreadTarget(10, 10);

            if (FJobRequest* J = PostJob(Job))
            {
                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[JobProcessor] Posted job %s at (%d,%d) Step=%d"),
                    *J->JobTag.ToString(),
                    J->TargetGridPos.X,
                    J->TargetGridPos.Y,
                    StepNumber);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[JobProcessor] Skipping food harvest job: Food stockpile is sufficient."));
    }

    if (WoodAmt < 20)
    {
        for (int32 i = 0; i < 2; ++i)
        {
            FJobRequest Job;
            Job.SettlementID = SettlementID;
            Job.PostedOnStep = StepNumber;
            Job.JobTag = TAG_Job_Harvest_Wood;
            Job.RequiredSkill = TAG_Skill_Hauling;
            Job.Priority = EJobPriority::Normal;
            Job.TargetGridPos = GetSpreadTarget(15, 10);

            if (FJobRequest* J = PostJob(Job))
            {
                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[JobProcessor] Posted job %s at (%d,%d) Step=%d"),
                    *J->JobTag.ToString(),
                    J->TargetGridPos.X,
                    J->TargetGridPos.Y,
                    StepNumber);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[JobProcessor] Skipping wood harvest job: Wood stockpile is sufficient."));
    }

    if (StoneAmt < 20)
    {
        for (int32 i = 0; i < 2; ++i)
        {
            FJobRequest Job;
            Job.SettlementID = SettlementID;
            Job.PostedOnStep = StepNumber;
            Job.JobTag = TAG_Job_Harvest_Stone;
            Job.RequiredSkill = TAG_Skill_Mining;
            Job.Priority = EJobPriority::Normal;
            Job.TargetGridPos = GetSpreadTarget(20, 10);

            if (FJobRequest* J = PostJob(Job))
            {
                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[JobProcessor] Posted job %s at (%d,%d) Step=%d"),
                    *J->JobTag.ToString(),
                    J->TargetGridPos.X,
                    J->TargetGridPos.Y,
                    StepNumber);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[JobProcessor] Skipping stone harvest job: Stone stockpile is sufficient."));
    }
}

// ====================================================================
// PUBLIC API
// ====================================================================
FJobRequest* UJobProcessor::PostJob(const FJobRequest& Request)
{
    return JobQueue.PostJob(Request);
}

void UJobProcessor::CancelJobsForAgent(int32 AgentID, UWorld* World)
{
    JobQueue.CancelAllJobsForAgent(AgentID);

    if (UReservationSystem* RS = World ? World->GetSubsystem<UReservationSystem>() : nullptr)
    {
        RS->NotifyJobCancelled(AgentID);
    }
}

int32 UJobProcessor::GetPendingJobCount(int32 SettlementID) const
{
    return JobQueue.GetPendingCount(SettlementID);
}

int32 UJobProcessor::GetAssignedJobCount(int32 SettlementID) const
{
    return JobQueue.GetAssignedCount(SettlementID);
}

FGameplayTag UJobProcessor::GetSkillTagForJob(FGameplayTag JobTag) const
{
    if (JobTag == TAG_Job_Harvest_Food)
    {
        return TAG_Skill_Farming;
    }

    if (JobTag == TAG_Job_Harvest_Wood)
    {
        return TAG_Skill_Hauling;
    }

    if (JobTag == TAG_Job_Harvest_Stone)
    {
        return TAG_Skill_Mining;
    }

    if (JobTag == TAG_Job_Cook)
    {
        return TAG_Skill_Farming;
    }

    if (JobTag == TAG_Job_Craft)
    {
        return TAG_Skill_Hauling;
    }

    if (JobTag == TAG_Job_Build)
    {
        return TAG_Skill_Hauling;
    }

    return FGameplayTag();
}

const TArray<FJobRequest>& UJobProcessor::GetAllJobs() const
{
    return JobQueue.GetAllJobs();
}

void UJobProcessor::RestoreJobQueue(const TArray<FJobRequest>& Saved)
{
    JobQueue.RestoreFromSave(Saved);
}

const TArray<FJobRequest>& UJobProcessor::GetJobsForSave() const
{
    return JobQueue.GetJobsForSave();
}