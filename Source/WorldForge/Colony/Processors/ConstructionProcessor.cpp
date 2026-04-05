// ConstructionProcessor.cpp

#include "ConstructionProcessor.h"
#include "VoxelConstructionSystem.h"
#include "WorldForgeTags.h"

void UConstructionProcessor::ProcessStep(
    int32 StepNumber,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World)
{
    // StepNumber is currently unused in Phase 2 construction ticking.
    (void)StepNumber;

    if (!World)
    {
        return;
    }

    UVoxelConstructionSystem* VCS = World->GetSubsystem<UVoxelConstructionSystem>();
    if (!VCS)
    {
        return;
    }

    const TArray<FActiveBuildTask>& AllTasks = VCS->GetAllTasks();

    for (FAgentData& Agent : Agents)
    {
        // Only process agents in this settlement.
        if (Agent.SettlementID != SettlementID)
        {
            continue;
        }

        // =========================================
        // Phase 1: Ensure this agent has a build task
        // =========================================

        FActiveBuildTask* Task = VCS->GetMutableTaskForAgent(Agent.AgentID);

        // If this agent does not already own a task, try to claim an unassigned
        // active construction task for this settlement.
        if (!Task)
        {
            for (const FActiveBuildTask& Candidate : AllTasks)
            {
                if (Candidate.bComplete)
                {
                    continue;
                }

                if (Candidate.SettlementID != SettlementID)
                {
                    continue;
                }

                if (Candidate.AssignedAgentID != -1)
                {
                    continue;
                }

                FActiveBuildTask* MutableCandidate = VCS->GetMutableTask(Candidate.TaskID);
                if (!MutableCandidate || MutableCandidate->bComplete || MutableCandidate->AssignedAgentID != -1)
                {
                    continue;
                }

                MutableCandidate->AssignedAgentID = Agent.AgentID;
                Task = MutableCandidate;

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[ConstructionProcessor] Assigned Agent %d to Task %d (%s) Settlement=%d"),
                    Agent.AgentID,
                    MutableCandidate->TaskID,
                    *MutableCandidate->BuildingTag.ToString(),
                    SettlementID
                );

                break;
            }
        }

        // If we still do not have a task, this agent has nothing to build.
        if (!Task)
        {
            continue;
        }

        // =========================================
        // Phase 2: Force agent into build state
        // =========================================

        if (!Task->bComplete)
        {
            Agent.State = EAgentState::Working;
            Agent.CurrentJobTag = TAG_Job_Build;
            Agent.CurrentJobBuildingTag = Task->BuildingTag;
        }

        // Only process working agents in this settlement.
        if (Agent.State != EAgentState::Working)
        {
            continue;
        }

        // ConstructionProcessor only handles build-tagged work.
        if (!Agent.CurrentJobTag.MatchesTag(TAG_Job_Build))
        {
            continue;
        }

        Task = VCS->GetMutableTaskForAgent(Agent.AgentID);
        if (!Task)
        {
            continue;
        }

        // =========================================
        // Phase 3: Place one construction block
        // =========================================

        const bool bCompleted = VCS->PlaceNextBlock(Task->TaskID, World);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ConstructionProcessor] Agent %d advanced Task %d Progress=%d/%d Complete=%s"),
            Agent.AgentID,
            Task->TaskID,
            Task->PlacedBlocks,
            Task->TotalBlocks,
            bCompleted ? TEXT("true") : TEXT("false")
        );

        // If the task completes, reset the agent back to idle.
        if (bCompleted)
        {
            Agent.State = EAgentState::Idle;
            Agent.CurrentJobTag = FGameplayTag();
            Agent.CurrentJobBuildingTag = FGameplayTag();
            Agent.ActiveReservationID = -1;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ConstructionProcessor] Agent %d completed Task %d and returned to Idle."),
                Agent.AgentID,
                Task->TaskID
            );
        }
    }
}