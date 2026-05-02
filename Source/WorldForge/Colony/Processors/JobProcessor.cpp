#include "JobProcessor.h"
#include "ReservationSystem.h"
#include "SettlementManager.h"
#include "NavigationContext.h"
#include "EventBus.h"
#include "ProcessorManager.h"
#include "ProductionProcessor.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"


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
    // Safety: simulation must always have a valid world
    if (!World)
    {
        return;
    }

    // Clean up completed or invalid jobs from queue
    JobQueue.PruneCompletedJobs();

    // If no jobs exist, periodically generate basic harvest jobs
    if (!JobQueue.HasPendingJobs(SettlementID) && (StepNumber - LastIdleGenStep) > 10)
    {
        GenerateIdleHarvestJobs(SettlementID, StepNumber, World);
        LastIdleGenStep = StepNumber;
    }

    // Process each agent in this settlement
    for (FAgentData& Agent : Agents)
    {
        // Skip agents not in this settlement
        if (Agent.SettlementID != SettlementID)
        {
            continue;
        }

        // Skip invalid agent states
        if (Agent.State == EAgentState::Dead || Agent.State == EAgentState::Incapacitated)
        {
            continue;
        }

        // ============================================================
        // PHASE 1: Movement
        // If agent is moving, advance toward target and skip job logic
        // ============================================================
        if (Agent.State == EAgentState::MovingToTarget)
        {
            TickMovement(Agent, StepNumber, World);
            continue;
        }

        // ============================================================
        // PHASE 2: Skip agents already busy
        // ============================================================
        if (Agent.State == EAgentState::Working ||
            Agent.State == EAgentState::Carrying ||
            Agent.State == EAgentState::Eating ||
            Agent.State == EAgentState::Sleeping ||
            Agent.State == EAgentState::ReservingTarget)
        {
            continue;
        }

        // ============================================================
        // PHASE 3: Assign work
        // Priority:
        // 1. Critical needs
        // 2. Existing queued jobs
        // 3. Otherwise enter a clean Idle fallback state
        // ============================================================
        if (HandleCriticalNeeds(Agent, SettlementID, StepNumber, World))
        {
            continue;
        }

        if (TryAssignQueuedJob(Agent, SettlementID, StepNumber, World))
        {
            continue;
        }

        
        // ------------------------------------------------------------
        // FALLBACK: no suitable job found
        //
        // This replaces warning spam with stable behavior:
        // - clear any stale task state
        // - ensure the agent is safely idle
        // - allow future steps to re-evaluate new jobs
        // ------------------------------------------------------------
        Agent.State = EAgentState::Idle;
        Agent.CurrentJobTag = FGameplayTag();
        Agent.CurrentJobBuildingTag = FGameplayTag();
        Agent.ActiveReservationID = -1;
    }
}

// ====================================================================
// MOVEMENT SYSTEM (PHASE 2 IMPLEMENTATION)
// Moves agent one grid cell per simulation step toward target.
// Deterministic and grid-based (no interpolation here).
// ====================================================================
void UJobProcessor::TickMovement(
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    const FIntPoint Target = Agent.CurrentNavTarget;

    // If already at target → trigger arrival logic
    if (Agent.CurrentGridPos == Target)
    {
        OnAgentArrived(Agent, StepNumber, World);
        return;
    }

    FIntPoint Pos = Agent.CurrentGridPos;

    // Move step-by-step (can scale with MovementCellsPerStep)
    for (int32 i = 0; i < MovementCellsPerStep; ++i)
    {
        if (Pos == Target)
        {
            break;
        }

        // Simple Manhattan movement (Phase 2)
        // Phase 3: replace with path following
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

    // If reached target → trigger arrival
    if (Agent.CurrentGridPos == Target)
    {
        OnAgentArrived(Agent, StepNumber, World);
    }
}

// ====================================================================
// ARRIVAL HANDLER
// Called when agent reaches its navigation target.
// Responsible for transitioning into work or completing instant jobs.
// ====================================================================
void UJobProcessor::OnAgentArrived(
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    const FGameplayTag JobTag = Agent.CurrentJobTag;

   
    // ------------------------------------------------------------
    // Eat Job
    // ------------------------------------------------------------
    if (JobTag == TAG_Job_Eat)
    {
        Agent.State = EAgentState::Eating;

        // Instantly restore food (Phase 2 simplification)
        Agent.FoodNeed = 100.f;

        // Consume resource from stockpile
        if (USettlementManager* SM = World->GetSubsystem<USettlementManager>())
        {
            SM->ConsumeFromStockpile(Agent.SettlementID, TAG_Item_Food, 1);
        }

        // Reset agent state
        Agent.State = EAgentState::Idle;
        Agent.CurrentJobTag = FGameplayTag();
        Agent.CurrentJobBuildingTag = FGameplayTag();
        Agent.ActiveReservationID = -1;
        return;
    }

    // ------------------------------------------------------------
    // Sleep Job
    // ------------------------------------------------------------
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

    // ------------------------------------------------------------
    // Production / Harvest / Build Jobs
    // These require starting a production task
    // ------------------------------------------------------------
    if (JobTag == TAG_Job_Cook ||
        JobTag == TAG_Job_Craft ||
        JobTag == TAG_Job_Build ||
        JobTag.MatchesTag(TAG_Job_Harvest))
    {
        Agent.State = EAgentState::Working;

        // Forward task to ProductionProcessor
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
// Creates emergency jobs for hunger or sleep.
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

    // ------------------------------------------------------------
    // FOOD NEED
    // ------------------------------------------------------------
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

        // Create eat job
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

    // ------------------------------------------------------------
    // SLEEP NEED
    // ------------------------------------------------------------
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
// Finds best job from queue and assigns it to agent
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

    FJobRequest* Job = JobQueue.FindBestJobForAgent(
        Agent.AgentID,
        Agent,
        SettlementID);

    // ------------------------------------------------------------
    // No valid job available:
    // - do not warn every tick
    // - let caller place the agent into fallback Idle behavior
    // ------------------------------------------------------------
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
// Sets agent state and navigation target
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

    // Broadcast event for UI / systems
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
// Creates fallback harvest jobs when the settlement has no pending jobs.
// Keeps idle colonists productive and stockpiles replenishing.
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

    // Diagnostic log: show current stockpile levels whenever fallback job generation runs.
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[JobProcessor] Idle job check Settlement=%d Step=%d Food=%d Wood=%d Stone=%d"),
        SettlementID,
        StepNumber,
        FoodAmt,
        WoodAmt,
        StoneAmt);

    // CHANGE MADE:
    // The old fallback targets used either fixed coordinates or a very small random
    // offset range around the same anchor cells. That still produced visible vertical
    // stacks and tight clustering because many agents were effectively being sent into
    // the same narrow area.
    //
    // This helper now:
    // 1. uses a wider spread radius,
    // 2. randomizes X and Y independently,
    // 3. tries multiple times to find a walkable cell,
    // 4. falls back safely to the original base cell if no candidate succeeds.
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

            // Safe fallback if NavigationContext is missing or no candidate succeeded.
            return FIntPoint(BaseX, BaseY);
        };

    if (FoodAmt < 50)
    {
        // CHANGE MADE:
        // Post multiple fallback food jobs so more colonists can work at once and
        // the group does not bottleneck around a single posted target.
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
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[JobProcessor] Skipping food harvest job: Food stockpile is sufficient."));
    }

    if (WoodAmt < 20)
    {
        // CHANGE MADE:
        // Post multiple fallback wood jobs with wider-spread targets.
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
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[JobProcessor] Skipping wood harvest job: Wood stockpile is sufficient."));
    }

    if (StoneAmt < 20)
    {
        // CHANGE MADE:
        // Post multiple fallback stone jobs with wider-spread targets.
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
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[JobProcessor] Skipping stone harvest job: Stone stockpile is sufficient."));
    }
}

// ====================================================================
// PUBLIC API
// Thin wrappers around JobQueue for processor-facing access.
// ====================================================================

// Posts a new job into the queue and returns a pointer to the inserted entry.
FJobRequest* UJobProcessor::PostJob(const FJobRequest& Request)
{
    return JobQueue.PostJob(Request);
}

// Cancels all jobs owned by an agent and notifies ReservationSystem
// so reserved targets can be released.
void UJobProcessor::CancelJobsForAgent(int32 AgentID, UWorld* World)
{
    JobQueue.CancelAllJobsForAgent(AgentID);

    if (UReservationSystem* RS = World ? World->GetSubsystem<UReservationSystem>() : nullptr)
    {
        RS->NotifyJobCancelled(AgentID);
    }
}

// Returns number of currently pending (unassigned) jobs for a settlement.
int32 UJobProcessor::GetPendingJobCount(int32 SettlementID) const
{
    return JobQueue.GetPendingCount(SettlementID);
}

// Returns number of currently assigned jobs for a settlement.
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

// Returns all jobs in the internal queue.
const TArray<FJobRequest>& UJobProcessor::GetAllJobs() const
{
    return JobQueue.GetAllJobs();
}

// Restores the internal queue from saved job data.
void UJobProcessor::RestoreJobQueue(const TArray<FJobRequest>& Saved)
{
    JobQueue.RestoreFromSave(Saved);
}

// Returns job queue contents for save serialization.
const TArray<FJobRequest>& UJobProcessor::GetJobsForSave() const
{
    return JobQueue.GetJobsForSave();
}