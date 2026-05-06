#include "ProductionProcessor.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"
#include "ProcessorManager.h"
#include "JobProcessor.h"
#include "Modifiers/WorldForgeModifierResolver.h"
#include "Farming/FarmingSubsystem.h"
#include "Farming/WildHarvestAdapter.h"
#include "Mining/MiningSubsystem.h"

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

        if (!Task->bInputsReserved && Task->Progress <= 0.0f)
        {
            TMap<FGameplayTag, int32> Inputs;
            TMap<FGameplayTag, int32> Outputs;
            float RequiredProgress = 10.0f;

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

            UE_LOG(
                LogTemp,
                Verbose,
                TEXT("[ProductionProcessor] Task initialized. TaskID=%d Agent=%d Job=%s RequiredProgress=%.2f"),
                Task->TaskID,
                Agent.AgentID,
                *Task->JobTag.ToString(),
                Task->RequiredProgress);
        }

        const float SkillMod = GetSkillModifier(Agent, Task->JobTag);

        const float EfficiencyMod = FMath::Clamp(
            UWorldForgeModifierResolver::ResolveWorkEfficiency(Agent),
            0.0f,
            2.0f);

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
    FGameplayTag BuildingTag,
    int32 TargetID,
    FIntPoint TargetGridPos,
    FGameplayTag TargetItemTag)
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

    // Preserve job metadata from FJobRequest so CompleteTask() can route
    // farm/mining completions back to the correct subsystem record.
    Task.TargetID = TargetID;
    Task.TargetGridPos = TargetGridPos;
    Task.TargetItemTag = TargetItemTag;

    Task.AssignedAgentID = AgentID;
    Task.Progress = 0.0f;
    Task.RequiredProgress = 10.0f;
    Task.bInputsReserved = false;
    Task.bComplete = false;

    ActiveTasks.Add(Task);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ProductionProcessor] Started task ID=%d Agent=%d Job=%s TargetID=%d TargetGrid=(%d,%d)"),
        Task.TaskID,
        AgentID,
        *JobTag.ToString(),
        TargetID,
        TargetGridPos.X,
        TargetGridPos.Y);

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
    OutRequiredProgress = 10.0f;

    if (JobTag == TAG_Job_Cook)
    {
        OutInputs.Add(TAG_Item_Food, 2);
        OutOutputs.Add(TAG_Item_Meal, 3);
        OutRequiredProgress = 8.0f;
        return true;
    }

    if (JobTag == TAG_Job_Craft)
    {
        OutInputs.Add(TAG_Item_Wood, 2);
        OutOutputs.Add(TAG_Item_Plank, 1);
        OutRequiredProgress = 10.0f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Food)
    {
        OutOutputs.Add(TAG_Item_Food, 1);
        OutRequiredProgress = 16.0f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Wood)
    {
        OutOutputs.Add(TAG_Item_Wood, 1);
        OutRequiredProgress = 14.0f;
        return true;
    }

    if (JobTag == TAG_Job_Harvest_Stone)
    {
        OutOutputs.Add(TAG_Item_Stone, 1);
        OutRequiredProgress = 18.0f;
        return true;
    }

    // Mining designations are completed through MiningSubsystem.
    // This recipe only defines work time.
    if (JobTag == TAG_Job_Mine)
    {
        OutRequiredProgress = 18.0f;
        return true;
    }

    if (JobTag == TAG_Job_Build)
    {
        OutRequiredProgress = 10.0f;
        return true;
    }

    // Farming jobs are completed through FarmingSubsystem.
    // These recipes only define work time. Skill/tool modifiers apply in ProcessStep().
    if (JobTag == TAG_Job_Farm_PrepSoil)
    {
        // Soil prep should feel like real labor per tile.
        OutRequiredProgress = 24.0f;
        return true;
    }

    if (JobTag == TAG_Job_Farm_Plant)
    {
        // Planting is shorter than soil prep but still not instant.
        OutRequiredProgress = 14.0f;
        return true;
    }

    if (JobTag == TAG_Job_Farm_Water)
    {
        // Watering is quick but still a work action.
        OutRequiredProgress = 8.0f;
        return true;
    }

    if (JobTag == TAG_Job_Farm_Harvest)
    {
        // Harvesting should take meaningful time per crop tile.
        OutRequiredProgress = 22.0f;
        return true;
    }

    if (JobTag == TAG_Job_Farm_Clear)
    {
        OutRequiredProgress = 12.0f;
        return true;
    }

    if (JobTag == TAG_Job_Farm_Fertilize)
    {
        OutRequiredProgress = 10.0f;
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
    else if (JobTag == TAG_Job_Harvest_Stone ||
        JobTag == TAG_Job_Mine)
    {
        SkillTag = TAG_Skill_Mining;
    }
    else if (JobTag == TAG_Job_Build)
    {
        SkillTag = TAG_Skill_Building;
    }
    else if (JobTag == TAG_Job_Farm_PrepSoil ||
        JobTag == TAG_Job_Farm_Plant ||
        JobTag == TAG_Job_Farm_Water ||
        JobTag == TAG_Job_Farm_Harvest ||
        JobTag == TAG_Job_Farm_Clear ||
        JobTag == TAG_Job_Farm_Fertilize)
    {
        SkillTag = TAG_Skill_Farming;
    }

    if (!SkillTag.IsValid())
    {
        return 1.0f;
    }

    const float SkillValue =
        UWorldForgeModifierResolver::ResolveSkillValue(Agent, SkillTag);

    return 0.5f + (SkillValue / 100.0f) * 1.5f;
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

    const bool bIsFarmJob =
        Task.JobTag == TAG_Job_Farm_PrepSoil ||
        Task.JobTag == TAG_Job_Farm_Plant ||
        Task.JobTag == TAG_Job_Farm_Water ||
        Task.JobTag == TAG_Job_Farm_Harvest ||
        Task.JobTag == TAG_Job_Farm_Clear ||
        Task.JobTag == TAG_Job_Farm_Fertilize;

    const bool bIsMiningJob =
        Task.JobTag == TAG_Job_Mine;

    // =====================================================================
    // FARMING COMPLETION ROUTING
    // =====================================================================
    if (bIsFarmJob)
    {
        UFarmingSubsystem* FarmSub = World ? World->GetSubsystem<UFarmingSubsystem>() : nullptr;
        UWildHarvestAdapter* Adapter = World ? World->GetSubsystem<UWildHarvestAdapter>() : nullptr;

        if (!FarmSub)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[ProductionProcessor] Farm completion failed: FarmingSubsystem missing. Job=%s Agent=%d TargetID=%d"),
                *Task.JobTag.ToString(),
                Agent.AgentID,
                Task.TargetID);

            FailTaskAndReleaseAgent(Task, Agent, World);
            return;
        }

        const int32 ZoneID = Task.TargetID;
        const FIntPoint TilePos = Task.TargetGridPos;

        if (Task.JobTag == TAG_Job_Farm_PrepSoil)
        {
            FarmSub->OnPrepSoilCompleted(ZoneID, TilePos);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ProductionProcessor] Farm prep soil complete. Settlement=%d Zone=%d Tile=(%d,%d)"),
                Task.SettlementID,
                ZoneID,
                TilePos.X,
                TilePos.Y);
        }
        else if (Task.JobTag == TAG_Job_Farm_Plant)
        {
            FarmSub->OnPlantCompleted(ZoneID, TilePos);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ProductionProcessor] Farm plant complete. Settlement=%d Zone=%d Tile=(%d,%d)"),
                Task.SettlementID,
                ZoneID,
                TilePos.X,
                TilePos.Y);
        }
        else if (Task.JobTag == TAG_Job_Farm_Water)
        {
            FarmSub->OnWaterCompleted(ZoneID, TilePos);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ProductionProcessor] Farm water complete. Settlement=%d Zone=%d Tile=(%d,%d)"),
                Task.SettlementID,
                ZoneID,
                TilePos.X,
                TilePos.Y);
        }
        else if (Task.JobTag == TAG_Job_Farm_Harvest)
        {
            int32 Yield = 5;
            FGameplayTag ItemTag = TAG_Item_Food;

            FWorldForgeFarmZone ZoneData;
            if (FarmSub->GetZoneByID(ZoneID, ZoneData))
            {
                if (Adapter)
                {
                    Yield = Adapter->CalculateYield(
                        ZoneData.CropRowName,
                        false,
                        false);

                    const FGameplayTag ResolvedTag =
                        Adapter->GetItemTagForCategory(ZoneData.CropCategory);

                    if (ResolvedTag.IsValid())
                    {
                        ItemTag = ResolvedTag;
                    }
                }

                FarmSub->OnHarvestCompleted(ZoneID, TilePos, Yield);

                if (SM && ItemTag.IsValid())
                {
                    SM->AddToStockpile(Task.SettlementID, ItemTag, Yield);

                    UE_LOG(
                        LogTemp,
                        Log,
                        TEXT("[ProductionProcessor] Farm harvest: +%d %s Settlement=%d Zone=%d Tile=(%d,%d)"),
                        Yield,
                        *ItemTag.ToString(),
                        Task.SettlementID,
                        ZoneID,
                        TilePos.X,
                        TilePos.Y);
                }
            }
            else
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[ProductionProcessor] Farm harvest failed: ZoneID=%d not found."),
                    ZoneID);
            }
        }
        else if (Task.JobTag == TAG_Job_Farm_Clear)
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ProductionProcessor] Farm clear completed. Settlement=%d Zone=%d Tile=(%d,%d)"),
                Task.SettlementID,
                ZoneID,
                TilePos.X,
                TilePos.Y);

            // Clear/dead-crop cleanup is currently handled by FarmingSubsystem.
        }
        else if (Task.JobTag == TAG_Job_Farm_Fertilize)
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ProductionProcessor] Farm fertilize completed. Settlement=%d Zone=%d Tile=(%d,%d)"),
                Task.SettlementID,
                ZoneID,
                TilePos.X,
                TilePos.Y);

            // Fertilize support can be wired once farm inventory/tools exist.
        }
    }

    // =====================================================================
    // MINING COMPLETION ROUTING
    // =====================================================================
    else if (bIsMiningJob)
    {
        UMiningSubsystem* MiningSub = World ? World->GetSubsystem<UMiningSubsystem>() : nullptr;

        if (!MiningSub)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[ProductionProcessor] Mining completion failed: MiningSubsystem missing. Agent=%d TargetID=%d"),
                Agent.AgentID,
                Task.TargetID);

            FailTaskAndReleaseAgent(Task, Agent, World);
            return;
        }

        const bool bCompleted =
            MiningSub->CompleteDesignation(Task.TargetID, Agent.AgentID);

        if (!bCompleted)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[ProductionProcessor] Mining completion failed: DesignationID=%d Agent=%d"),
                Task.TargetID,
                Agent.AgentID);

            FailTaskAndReleaseAgent(Task, Agent, World);
            return;
        }

        if (SM)
        {
            SM->AddToStockpile(Task.SettlementID, TAG_Item_Stone, 1);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ProductionProcessor] Mine complete: +1 %s Settlement=%d DesignationID=%d"),
                *TAG_Item_Stone.GetTag().ToString(),
                Task.SettlementID,
                Task.TargetID);
        }
    }

    // =====================================================================
    // GENERIC PRODUCTION COMPLETION
    // =====================================================================
    else
    {
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

    if (UProcessorManager* PM = World ? World->GetSubsystem<UProcessorManager>() : nullptr)
    {
        if (UJobProcessor* JP = PM->GetTypedProcessor<UJobProcessor>())
        {
            JP->CancelJobsForAgent(Task.AssignedAgentID, World);
        }
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ProductionProcessor] Agent %d ENTERED CARRY STATE"),
        Task.AssignedAgentID);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ProductionProcessor] Complete: %s agent %d TargetID=%d TargetGrid=(%d,%d)"),
        *Task.JobTag.ToString(),
        Task.AssignedAgentID,
        Task.TargetID,
        Task.TargetGridPos.X,
        Task.TargetGridPos.Y);
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