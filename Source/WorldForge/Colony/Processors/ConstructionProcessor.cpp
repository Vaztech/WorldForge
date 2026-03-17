#include "ConstructionProcessor.h"
#include "VoxelConstructionSystem.h"
#include "WorldForgeTags.h" // Required for TAG_Job_Build

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

    for (FAgentData& Agent : Agents)
    {
        // Only process working agents in this settlement.
        if (Agent.SettlementID != SettlementID || Agent.State != EAgentState::Working)
        {
            continue;
        }

        // ConstructionProcessor only handles build-tagged work.
        if (!Agent.CurrentJobTag.MatchesTag(TAG_Job_Build))
        {
            continue;
        }

        // Get the active mutable build task for this agent.
        FActiveBuildTask* Task = VCS->GetMutableTaskForAgent(Agent.AgentID);
        if (!Task)
        {
            continue;
        }

        // Place one construction block this step.
        // If the task completes, reset the agent back to idle.
        if (VCS->PlaceNextBlock(Task->TaskID, World))
        {
            Agent.State = EAgentState::Idle;
            Agent.CurrentJobTag = FGameplayTag();
            Agent.CurrentJobBuildingTag = FGameplayTag();
            Agent.ActiveReservationID = -1;
        }
    }
}