#include "Processors/MiningProcessor.h"

#include "Mining/MiningSubsystem.h"
#include "WorldForgeTags.h"

void UMiningProcessor::ProcessStep(
    int32 StepNumber,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World)
{
    if (!World)
    {
        return;
    }

    for (FAgentData& Agent : Agents)
    {
        if (Agent.SettlementID != SettlementID)
        {
            continue;
        }

        if (Agent.State == EAgentState::Dead ||
            Agent.State == EAgentState::Incapacitated)
        {
            continue;
        }

        // Injured / sick agents with no usable efficiency cannot mine.
        if (Agent.CurrentEfficiency <= 0.0f)
        {
            continue;
        }

        // Mining job reached destination / immediate test target.
        if (Agent.CurrentJobTag == TAG_Job_Mine &&
            AgentMiningDesignationIDs.Contains(Agent.AgentID))
        {
            if (Agent.State == EAgentState::MovingToTarget &&
                Agent.CurrentGridPos == Agent.CurrentNavTarget)
            {
                CompleteMiningDesignation(Agent, StepNumber, World);
            }

            continue;
        }

        // Only idle agents can accept mining designations.
        if (Agent.State != EAgentState::Idle)
        {
            continue;
        }

        TryAssignMiningDesignation(Agent, SettlementID, StepNumber, World);
    }
}

bool UMiningProcessor::TryAssignMiningDesignation(
    FAgentData& Agent,
    int32 SettlementID,
    int32 StepNumber,
    UWorld* World)
{
    UMiningSubsystem* Mining = World ? World->GetSubsystem<UMiningSubsystem>() : nullptr;
    if (!Mining)
    {
        return false;
    }

    // Health/injury efficiency affects whether this agent can take mining work.
    const float EfficiencyMod = FMath::Clamp(Agent.CurrentEfficiency, 0.0f, 1.5f);
    if (EfficiencyMod <= 0.0f)
    {
        return false;
    }

    int32 DesignationID = INDEX_NONE;
    FIntVector MineGrid = FIntVector::ZeroValue;

    if (!Mining->TryReserveDesignation(
        SettlementID,
        Agent.AgentID,
        DesignationID,
        MineGrid))
    {
        return false;
    }

    AgentMiningDesignationIDs.Add(Agent.AgentID, DesignationID);

    Agent.CurrentJobTag = TAG_Job_Mine;
    Agent.CurrentJobBuildingTag = FGameplayTag();
    Agent.ActiveReservationID = -1;

    // Current temporary behavior:
    // mining completes immediately instead of walking to far voxel-space coords.
    Agent.CurrentNavTarget = Agent.CurrentGridPos;
    Agent.State = EAgentState::MovingToTarget;

    UE_LOG(LogTemp, Warning,
        TEXT("[MiningProcessor] Agent %d assigned mining designation ID=%d -> Grid=(%d,%d,%d) Efficiency=%.2f Step=%d"),
        Agent.AgentID,
        DesignationID,
        MineGrid.X,
        MineGrid.Y,
        MineGrid.Z,
        EfficiencyMod,
        StepNumber);

    CompleteMiningDesignation(Agent, StepNumber, World);
    return true;
}

void UMiningProcessor::CompleteMiningDesignation(
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    int32* MiningDesignationID = AgentMiningDesignationIDs.Find(Agent.AgentID);
    if (!MiningDesignationID)
    {
        return;
    }

    bool bCompleted = false;

    if (UMiningSubsystem* Mining = World ? World->GetSubsystem<UMiningSubsystem>() : nullptr)
    {
        bCompleted = Mining->CompleteDesignation(*MiningDesignationID, Agent.AgentID);
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[MiningProcessor] Mining designation complete attempt. Agent=%d ID=%d Success=%s Efficiency=%.2f Step=%d"),
        Agent.AgentID,
        *MiningDesignationID,
        bCompleted ? TEXT("true") : TEXT("false"),
        Agent.CurrentEfficiency,
        StepNumber);

    AgentMiningDesignationIDs.Remove(Agent.AgentID);

    Agent.State = EAgentState::Idle;
    Agent.CurrentJobTag = FGameplayTag();
    Agent.CurrentJobBuildingTag = FGameplayTag();
    Agent.ActiveReservationID = -1;
}