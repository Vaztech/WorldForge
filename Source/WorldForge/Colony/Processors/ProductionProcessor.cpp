#include "ProductionProcessor.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"

// ====================================================================
// MAIN PROCESS STEP
// Runs once per simulation step per settlement.
// Responsible for:
// 1. Advancing active production tasks for working agents
// 2. Consuming required inputs once at task start
// 3. Applying skill-scaled progress
// 4. Completing tasks and depositing outputs
// 5. Pruning finished tasks
// ====================================================================
void UProductionProcessor::ProcessStep(
    int32 StepNumber,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World)
{
    if (!World)
    {
        return;
    }

    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    UEventBus* Bus = World->GetSubsystem<UEventBus>();

    if (!SM)
    {
        return;
    }

    for (FAgentData& Agent : Agents)
    {
        // Only process agents in this settlement who are actively working
        if (Agent.SettlementID != SettlementID)
        {
            continue;
        }

        if (Agent.State != EAgentState::Working)
        {
            continue;
        }

        // Find this agent's active production task
        FActiveProductionTask* Task = nullptr;
        for (FActiveProductionTask& T : ActiveTasks)
        {
            if (T.AssignedAgentID == Agent.AgentID && !T.bComplete)
            {
                Task = &T;
                break;
            }
        }

        if (!Task)
        {
            continue;
        }

        // ------------------------------------------------------------
        // Initialize recipe / consume inputs only once
        // ------------------------------------------------------------
        if (!Task->bInputsReserved && Task->Progress <= 0.f)
        {
            TMap<FGameplayTag, int32> Inputs;
            TMap<FGameplayTag, int32> Outputs;
            float RequiredProgress = 10.f;

            if (!GetRecipe(Task->JobTag, Inputs, Outputs, RequiredProgress))
            {
                // Invalid recipe → cancel task safely
                Agent.State = EAgentState::Idle;
                Task->bComplete = true;

                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[ProductionProcessor] No recipe for %s"),
                    *Task->JobTag.ToString());

                continue;
            }

            Task->RequiredProgress = RequiredProgress;
            Task->OutputItems = Outputs;

            // Check stockpile availability before consuming
            bool bAllAvailable = true;
            for (const TPair<FGameplayTag, int32>& Input : Inputs)
            {
                if (!SM->HasStockpile(SettlementID, Input.Key, Input.Value))
                {
                    bAllAvailable = false;
                    break;
                }
            }

            if (!bAllAvailable)
            {
                // Cannot proceed this step; agent returns idle for now
                Agent.State = EAgentState::Idle;
                continue;
            }

            // Consume all required inputs atomically
            for (const TPair<FGameplayTag, int32>& Input : Inputs)
            {
                SM->ConsumeFromStockpile(SettlementID, Input.Key, Input.Value);
                Task->InputsConsumed.Add(Input.Key, Input.Value);
            }

            Task->bInputsReserved = true;
        }

        // ------------------------------------------------------------
        // Advance task progress using skill modifier
        // ------------------------------------------------------------
        const float SkillMod = GetSkillModifier(Agent, Task->JobTag);
        Task->Progress += BaseProgressPerStep * SkillMod;

        // ------------------------------------------------------------
        // Complete task when enough work has been accumulated
        // ------------------------------------------------------------
        if (Task->Progress >= Task->RequiredProgress)
        {
            CompleteTask(*Task, Agent, StepNumber, World);
        }
    }

    // ------------------------------------------------------------
    // Cleanup finished tasks
    // Phase 2 default: prune immediately
    // ------------------------------------------------------------
    if (CompletedTaskRetentionSteps == 0)
    {
        ActiveTasks.RemoveAll([](const FActiveProductionTask& T)
            {
                return T.bComplete;
            });
    }
}

// ====================================================================
// START PRODUCTION TASK
// Creates a new production task for the given agent/job/building context.
// Returns TaskID on success or -1 if the agent already has an active task.
// ====================================================================
int32 UProductionProcessor::StartProductionTask(
    int32 AgentID,
    int32 SettlementID,
    FGameplayTag JobTag,
    FGameplayTag BuildingTag)
{
    if (HasActiveTask(AgentID))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ProductionProcessor] Agent %d already has active task!"),
            AgentID);

        return -1;
    }

    FActiveProductionTask Task;
    Task.TaskID = NextTaskID++;
    Task.SettlementID = SettlementID;
    Task.JobTag = JobTag;
    Task.BuildingTag = BuildingTag;
    Task.AssignedAgentID = AgentID;
    Task.Progress = 0.f;
    Task.RequiredProgress = 10.f;
    Task.bInputsReserved = false;
    Task.bComplete = false;

    ActiveTasks.Add(Task);
    return Task.TaskID;
}

// ====================================================================
// HAS ACTIVE TASK
// Returns true if the given agent already owns an unfinished task.
// ====================================================================
bool UProductionProcessor::HasActiveTask(int32 AgentID) const
{
    for (const FActiveProductionTask& T : ActiveTasks)
    {
        if (T.AssignedAgentID == AgentID && !T.bComplete)
        {
            return true;
        }
    }

    return false;
}

// ====================================================================
// GET TASK
// Returns the agent's current unfinished task, or nullptr if none exists.
// ====================================================================
const FActiveProductionTask* UProductionProcessor::GetTask(int32 AgentID) const
{
    for (const FActiveProductionTask& T : ActiveTasks)
    {
        if (T.AssignedAgentID == AgentID && !T.bComplete)
        {
            return &T;
        }
    }

    return nullptr;
}

// ====================================================================
// GET RECIPE
// Maps job tags to required inputs, outputs, and work duration.
// Phase 2 uses a simple hardcoded recipe table.
// ====================================================================
bool UProductionProcessor::GetRecipe(
    FGameplayTag JobTag,
    TMap<FGameplayTag, int32>& OutInputs,
    TMap<FGameplayTag, int32>& OutOutputs,
    float& OutRequiredProgress) const
{
    OutInputs.Empty();
    OutOutputs.Empty();
    OutRequiredProgress = 10.f;

    if (JobTag == TAG_Job_Cook)
    {
        OutInputs.Add(TAG_Item_Food, 2);
        OutOutputs.Add(TAG_Item_Meal, 3);
        OutRequiredProgress = 8.f;
        return true;
    }

    if (JobTag == TAG_Job_Craft)
    {
        OutInputs.Add(TAG_Item_Wood, 2);
        OutOutputs.Add(TAG_Item_Plank, 1);
        OutRequiredProgress = 10.f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Food)
    {
        OutOutputs.Add(TAG_Item_Food, 5);
        OutRequiredProgress = 12.f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Wood)
    {
        OutOutputs.Add(TAG_Item_Wood, 4);
        OutRequiredProgress = 10.f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Stone)
    {
        OutOutputs.Add(TAG_Item_Stone, 4);
        OutRequiredProgress = 15.f;
        return true;
    }

    if (JobTag == TAG_Job_Build)
    {
        // Build is included for pipeline completeness.
        // ConstructionProcessor may own the deeper building logic later.
        OutRequiredProgress = 10.f;
        return true;
    }

    return false;
}

// ====================================================================
// GET SKILL MODIFIER
// Converts the agent's relevant skill into a production speed multiplier.
// Default range:
// - no skill tag: 1.0x
// - known skill: 0.5x to 2.0x
// ====================================================================
float UProductionProcessor::GetSkillModifier(
    const FAgentData& Agent,
    FGameplayTag JobTag) const
{
    FGameplayTag SkillTag;

    if (JobTag == TAG_Job_Cook || JobTag == TAG_Job_Harvest_Food)
    {
        SkillTag = TAG_Skill_Cooking;
    }
    else if (JobTag == TAG_Job_Craft)
    {
        SkillTag = TAG_Skill_Crafting;
    }
    else if (JobTag == TAG_Job_Harvest_Wood)
    {
        SkillTag = TAG_Skill_Hauling;
    }
    else if (JobTag == TAG_Job_Harvest_Stone)
    {
        SkillTag = TAG_Skill_Mining;
    }
    else if (JobTag == TAG_Job_Build)
    {
        SkillTag = TAG_Skill_Building;
    }

    if (!SkillTag.IsValid())
    {
        return 1.0f;
    }

    const float* FoundSkill = Agent.Skills.Find(SkillTag);
    const float SkillValue = FoundSkill ? *FoundSkill : 1.f;

    return 0.5f + (SkillValue / 100.f) * 1.5f;
}

// ====================================================================
// COMPLETE TASK
// Finalizes a production task:
// - deposits outputs to stockpile
// - dispatches completion event
// - resets agent work state
// ====================================================================
void UProductionProcessor::CompleteTask(
    FActiveProductionTask& Task,
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    USettlementManager* SM = World ? World->GetSubsystem<USettlementManager>() : nullptr;
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;

    // Deposit outputs into settlement stockpile
    if (SM)
    {
        for (const TPair<FGameplayTag, int32>& Output : Task.OutputItems)
        {
            SM->AddToStockpile(Task.SettlementID, Output.Key, Output.Value);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ProductionProcessor] +%d %s in settlement %d"),
                Output.Value,
                *Output.Key.ToString(),
                Task.SettlementID);
        }
    }

    // Dispatch production complete event
    if (Bus)
    {
        FSimulationEvent Event;
        Event.EventType = TAG_Sim_Event_ProductionComplete;
        Event.SettlementID = Task.SettlementID;
        Event.AgentID = Task.AssignedAgentID;
        Event.StepNumber = StepNumber;
        Event.Payload = Task.JobTag;
        Bus->Dispatch(Event);
    }

    // Mark task complete and reset agent
    Task.bComplete = true;

    Agent.State = EAgentState::Idle;
    Agent.CurrentJobTag = FGameplayTag();
    Agent.CurrentJobBuildingTag = FGameplayTag();
    Agent.ActiveReservationID = -1;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ProductionProcessor] Complete: %s agent %d"),
        *Task.JobTag.ToString(),
        Task.AssignedAgentID);
}

// ====================================================================
// RESTORE FROM SAVE
// Restores the task list and rebuilds NextTaskID so future tasks get
// unique IDs after load.
// ====================================================================
void UProductionProcessor::RestoreFromSave(const TArray<FActiveProductionTask>& Saved)
{
    ActiveTasks = Saved;
    NextTaskID = 0;

    for (const FActiveProductionTask& T : ActiveTasks)
    {
        NextTaskID = FMath::Max(NextTaskID, T.TaskID + 1);
    }
}