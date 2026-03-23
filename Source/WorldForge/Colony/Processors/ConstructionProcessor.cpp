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
        // Only process agents in this settlement.
        if (Agent.SettlementID != SettlementID)
        {
            continue;
        }

        // ADDED:
        // If this agent already has an active construction task assigned,
        // force the agent into the expected build state/job for V-H automatic
        // placement validation. This is the smallest bridge needed so the
        // existing construction task can actually advance block-by-block.
        FActiveBuildTask* Task = VCS->GetMutableTaskForAgent(Agent.AgentID);
        if (Task && !Task->bComplete)
        {
            // ADDED:
            // Preserve existing architecture: the construction system remains
            // the source of truth for active build tasks. We only ensure the
            // assigned agent is in the build job/state required by the current
            // ConstructionProcessor filters.
            Agent.State = EAgentState::Working;
            Agent.CurrentJobTag = TAG_Job_Build;

            // ADDED:
            // Keep the building tag synchronized for any existing debug/UI use.
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

        // MODIFIED:
        // Reuse the active mutable build task lookup after the state/job bridge.
        Task = VCS->GetMutableTaskForAgent(Agent.AgentID);
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