#include "ProductionProcessor.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"
#include "ProcessorManager.h"
#include "JobProcessor.h"

// ====================================================================
// INTERNAL FAILURE HANDLER
// ====================================================================
void UProductionProcessor::FailTaskAndReleaseAgent(
    FActiveProductionTask& Task,
    FAgentData& Agent,
    UWorld* World)
{
    Task.bComplete = true;

    Agent.State = EAgentState::Idle;
    Agent.CurrentJobTag = FGameplayTag();
    Agent.CurrentJobBuildingTag = FGameplayTag();
    Agent.ActiveReservationID = -1;

    if (UProcessorManager* PM = World ? World->GetSubsystem<UProcessorManager>() : nullptr)
    {
        if (UJobProcessor* JP = PM->GetTypedProcessor<UJobProcessor>())
        {
            JP->CancelJobsForAgent(Agent.AgentID, World);
        }
    }
}

// ====================================================================
// MAIN PROCESS STEP
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
        if (Agent.SettlementID != SettlementID)
        {
            continue;
        }

        if (Agent.State == EAgentState::Dead || Agent.State == EAgentState::Incapacitated)
        {
            continue;
        }

        if (Agent.State == EAgentState::Carrying)
        {
            Agent.State = EAgentState::Idle;

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[ProductionProcessor] Agent %d EXITED CARRY STATE"),
                Agent.AgentID);

            continue;
        }

        if (Agent.State != EAgentState::Working)
        {
            continue;
        }

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

        if (!Task->bInputsReserved && Task->Progress <= 0.f)
        {
            TMap<FGameplayTag, int32> Inputs;
            TMap<FGameplayTag, int32> Outputs;
            float RequiredProgress = 10.f;

            if (!GetRecipe(Task->JobTag, Inputs, Outputs, RequiredProgress))
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[ProductionProcessor] No recipe for %s"),
                    *Task->JobTag.ToString());

                FailTaskAndReleaseAgent(*Task, Agent, World);
                continue;
            }

            Task->RequiredProgress = RequiredProgress;
            Task->OutputItems = Outputs;

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
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[ProductionProcessor] Missing inputs for %s, releasing agent %d"),
                    *Task->JobTag.ToString(),
                    Agent.AgentID);

                FailTaskAndReleaseAgent(*Task, Agent, World);
                continue;
            }

            for (const TPair<FGameplayTag, int32>& Input : Inputs)
            {
                SM->ConsumeFromStockpile(SettlementID, Input.Key, Input.Value);
                Task->InputsConsumed.Add(Input.Key, Input.Value);
            }

            Task->bInputsReserved = true;
        }

        // Health/injury now directly affects work speed.
        const float SkillMod = GetSkillModifier(Agent, Task->JobTag);
        const float EfficiencyMod = FMath::Clamp(Agent.CurrentEfficiency, 0.0f, 1.5f);
        const float FinalWorkMod = SkillMod * EfficiencyMod;

        if (FinalWorkMod <= 0.0f)
        {
            continue;
        }

        Task->Progress += BaseProgressPerStep * FinalWorkMod;

        if (Task->Progress >= Task->RequiredProgress)
        {
            CompleteTask(*Task, Agent, StepNumber, World);
        }
    }

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
        OutOutputs.Add(TAG_Item_Food, 1);
        OutRequiredProgress = 16.f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Wood)
    {
        OutOutputs.Add(TAG_Item_Wood, 1);
        OutRequiredProgress = 14.f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Stone)
    {
        OutOutputs.Add(TAG_Item_Stone, 1);
        OutRequiredProgress = 18.f;
        return true;
    }

    if (JobTag == TAG_Job_Build)
    {
        OutRequiredProgress = 10.f;
        return true;
    }

    return false;
}

// ====================================================================
// GET SKILL MODIFIER
// ====================================================================
float UProductionProcessor::GetSkillModifier(
    const FAgentData& Agent,
    FGameplayTag JobTag) const
{
    FGameplayTag SkillTag;

    if (JobTag == TAG_Job_Cook)
    {
        SkillTag = TAG_Skill_Cooking;
    }
    else if (JobTag == TAG_Job_Harvest_Food)
    {
        SkillTag = TAG_Skill_Farming;
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
// ====================================================================
void UProductionProcessor::CompleteTask(
    FActiveProductionTask& Task,
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    USettlementManager* SM = World ? World->GetSubsystem<USettlementManager>() : nullptr;
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;

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

    Task.bComplete = true;

    Agent.State = EAgentState::Carrying;
    Agent.CurrentJobTag = FGameplayTag();
    Agent.CurrentJobBuildingTag = FGameplayTag();
    Agent.ActiveReservationID = -1;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ProductionProcessor] Agent %d ENTERED CARRY STATE"),
        Task.AssignedAgentID);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ProductionProcessor] Complete: %s agent %d"),
        *Task.JobTag.ToString(),
        Task.AssignedAgentID);
}

// ====================================================================
// RESTORE FROM SAVE
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