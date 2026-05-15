#include "JobProcessor.h"
#include "ReservationSystem.h"
#include "SettlementManager.h"
#include "NavigationContext.h"
#include "EventBus.h"
#include "ProcessorManager.h"
#include "ProductionProcessor.h"
#include "Mining/MiningSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"
#include "Modifiers/WorldForgeModifierResolver.h"

// ====================================================================
// SHARED MODIFIER SUPPORT
//
// JobProcessor no longer loads DT_WorldForgeTraits directly.
// It asks WorldForgeModifierResolver for the agent's effective values.
//
// This matters because later equipment can be added to the resolver and
// JobProcessor will automatically score:
// Base skills + trait mods + equipment mods + buff/debuff mods
//
// The real Agent is not permanently mutated here.
// We build a temporary scoring copy for JobQueueManager.
// ====================================================================
namespace
{
    // Turn this on only when debugging score math.
    // Leaving it false prevents log spam every simulation step.
    constexpr bool bLogJobModifierScoring = false;

    FAgentData BuildModifierScoredAgentForJobSelection(const FAgentData& Agent)
    {
        FAgentData ScoredAgent = Agent;

        // Resolve all additive/multiplicative modifiers in one place.
        // Today this can include traits.
        // Later this can include equipment, buffs, wounds, curses, etc.
        const FWorldForgeResolvedModifiers Mods =
            UWorldForgeModifierResolver::ResolveAgentModifiers(Agent);

        const float OldEfficiency = ScoredAgent.CurrentEfficiency;

        // Use the resolver's final work-efficiency value.
        // This keeps JobProcessor independent from where modifiers came from.
        ScoredAgent.CurrentEfficiency =
            UWorldForgeModifierResolver::ResolveWorkEfficiency(Agent);

        // Apply additive skill modifiers to the temporary scoring copy only.
        // This does NOT permanently change Agent.Skills.
        for (const TPair<FGameplayTag, float>& SkillMod : Mods.SkillModifiers)
        {
            if (!SkillMod.Key.IsValid())
            {
                continue;
            }

            const float OldSkillValue = ScoredAgent.Skills.FindRef(SkillMod.Key);

            float& SkillValue = ScoredAgent.Skills.FindOrAdd(SkillMod.Key);
            SkillValue = FMath::Clamp(
                OldSkillValue + SkillMod.Value,
                0.0f,
                100.0f);
        }

        if (bLogJobModifierScoring)
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("[JobModifierScoring] Agent=%d Efficiency %.2f -> %.2f SkillMods=%d"),
                Agent.AgentID,
                OldEfficiency,
                ScoredAgent.CurrentEfficiency,
                Mods.SkillModifiers.Num());
        }

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

    // Find the assigned queue entry for this agent.
    //
    // This lets us preserve FJobRequest metadata when handing the work off
    // to ProductionProcessor:
    // - TargetID
    // - TargetGridPos
    // - TargetItemTag
    //
    // Without this, farming/mining completion cannot know which farm zone,
    // farm tile, or mining designation was completed.
    const FJobRequest* AssignedJob = nullptr;
    const FJobRequest* FallbackAssignedJob = nullptr;

    for (const FJobRequest& Job : JobQueue.GetAllJobs())
    {
        if (!Job.bAssigned || Job.AssignedAgentID != Agent.AgentID)
        {
            continue;
        }

        // Keep a fallback in case old/stale jobs exist, but prefer exact match.
        if (!FallbackAssignedJob)
        {
            FallbackAssignedJob = &Job;
        }

        const bool bTagMatches = Job.JobTag == JobTag;
        const bool bTargetMatches = Job.TargetGridPos == Agent.CurrentNavTarget;

        if (bTagMatches && bTargetMatches)
        {
            AssignedJob = &Job;
            break;
        }

        // Farm jobs must preserve TargetID/TargetGridPos, so at least require tag match.
        if (!AssignedJob && bTagMatches)
        {
            AssignedJob = &Job;
        }
    }

    if (!AssignedJob)
    {
        AssignedJob = FallbackAssignedJob;
    }

    if (!AssignedJob)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[JobProcessor] Agent %d arrived for %s but no assigned job metadata was found. NavTarget=(%d,%d) Step=%d"),
            Agent.AgentID,
            *JobTag.ToString(),
            Agent.CurrentNavTarget.X,
            Agent.CurrentNavTarget.Y,
            StepNumber);
    }

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

    const bool bIsFarmJob =
        JobTag == TAG_Job_Farm_PrepSoil ||
        JobTag == TAG_Job_Farm_Plant ||
        JobTag == TAG_Job_Farm_Water ||
        JobTag == TAG_Job_Farm_Harvest ||
        JobTag == TAG_Job_Farm_Clear ||
        JobTag == TAG_Job_Farm_Fertilize;

    const bool bIsProductionJob =
        JobTag == TAG_Job_Cook ||
        JobTag == TAG_Job_Craft ||
        JobTag == TAG_Job_Build ||
        JobTag == TAG_Job_Mine ||
        JobTag.MatchesTag(TAG_Job_Harvest) ||
        bIsFarmJob;

    if (bIsProductionJob)
    {
        Agent.State = EAgentState::Working;

        const int32 TargetID = AssignedJob ? AssignedJob->TargetID : INDEX_NONE;
        const FIntPoint TargetGridPos = AssignedJob ? AssignedJob->TargetGridPos : Agent.CurrentGridPos;
        const FGameplayTag TargetItemTag = AssignedJob ? AssignedJob->TargetItemTag : FGameplayTag();
        const FGameplayTag BuildingTag = AssignedJob ? AssignedJob->TargetBuildingTag : Agent.CurrentJobBuildingTag;

        if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
        {
            if (UProductionProcessor* PP = PM->GetTypedProcessor<UProductionProcessor>())
            {
                PP->StartProductionTask(
                    Agent.AgentID,
                    Agent.SettlementID,
                    JobTag,
                    BuildingTag,
                    TargetID,
                    TargetGridPos,
                    TargetItemTag);
            }
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[JobProcessor] Agent %d arrived for %s TargetID=%d TargetGrid=(%d,%d) Step=%d"),
            Agent.AgentID,
            *JobTag.ToString(),
            TargetID,
            TargetGridPos.X,
            TargetGridPos.Y,
            StepNumber);
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
// Modifier hook:
// We create a temporary scoring copy of the agent with effective skill
// and efficiency modifiers. The real Agent is not permanently changed.
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

    const FAgentData ScoredAgent = BuildModifierScoredAgentForJobSelection(Agent);

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
    // Intentionally do NOT generate generic default food/wood/stone harvest jobs here.
    // Player-directed/system-directed work should come from real designations and systems:
    // - MiningSubsystem posts mining jobs for marked mining designations.
    // - FarmingSubsystem posts farm jobs from farm zones.
    // - Build/production systems post build/craft/cook jobs from player orders.
    //
    // This keeps idle colonists from inventing work before the player tells them what to do.
    if (!World)
    {
        return;
    }

    if (UMiningSubsystem* MiningSubsystem = World->GetSubsystem<UMiningSubsystem>())
    {
        MiningSubsystem->PostMiningJobs(SettlementID, StepNumber);
    }

    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[JobProcessor] Idle/default harvest generation disabled. Checked explicit mining jobs only. Settlement=%d Step=%d"),
        SettlementID,
        StepNumber);
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
        return TAG_Skill_Cooking;
    }

    if (JobTag == TAG_Job_Craft)
    {
        return TAG_Skill_Crafting;
    }

    if (JobTag == TAG_Job_Build)
    {
        return TAG_Skill_Building;
    }

    // ════ Wild Harvest Integration ════
    // All farming jobs use the Farming skill.
    if (JobTag == TAG_Job_Farm_PrepSoil ||
        JobTag == TAG_Job_Farm_Plant ||
        JobTag == TAG_Job_Farm_Water ||
        JobTag == TAG_Job_Farm_Harvest ||
        JobTag == TAG_Job_Farm_Clear ||
        JobTag == TAG_Job_Farm_Fertilize)
    {
        return TAG_Skill_Farming;
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