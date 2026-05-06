#include "Processors/MiningProcessor.h"

#include "Mining/MiningSubsystem.h"
#include "SettlementManager.h"
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

        if (Agent.CurrentEfficiency <= 0.0f)
        {
            continue;
        }

        if (Agent.CurrentJobTag == TAG_Job_Mine &&
            AgentMiningDesignationIDs.Contains(Agent.AgentID))
        {
            if (Agent.State == EAgentState::MovingToTarget)
            {
                if (Agent.CurrentGridPos == Agent.CurrentNavTarget)
                {
                    Agent.State = EAgentState::Working;

                    UE_LOG(LogTemp, Warning,
                        TEXT("[MiningProcessor] Agent %d reached mining target. Starting work. Target=(%d,%d) Step=%d"),
                        Agent.AgentID,
                        Agent.CurrentNavTarget.X,
                        Agent.CurrentNavTarget.Y,
                        StepNumber);
                }

                continue;
            }

            if (Agent.State == EAgentState::Working)
            {
                TickMiningWork(Agent, StepNumber, World);
                continue;
            }

            continue;
        }

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

    FIntPoint LocalTarget = FIntPoint::ZeroValue;
    if (!ConvertMineGridToLocalTarget(SettlementID, MineGrid, World, LocalTarget))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MiningProcessor] Failed to convert mine grid to local target. Agent=%d Designation=%d Grid=(%d,%d,%d)"),
            Agent.AgentID,
            DesignationID,
            MineGrid.X,
            MineGrid.Y,
            MineGrid.Z);

        Mining->FailDesignation(DesignationID, Agent.AgentID);
        return false;
    }

    AgentMiningDesignationIDs.Add(Agent.AgentID, DesignationID);
    AgentMiningProgress.Add(Agent.AgentID, 0.0f);

    Agent.CurrentJobTag = TAG_Job_Mine;
    Agent.CurrentJobBuildingTag = FGameplayTag();
    Agent.ActiveReservationID = -1;
    Agent.CurrentNavTarget = LocalTarget;
    Agent.State = EAgentState::MovingToTarget;

    UE_LOG(LogTemp, Warning,
        TEXT("[MiningProcessor] Agent %d assigned mining designation ID=%d WorldGrid=(%d,%d,%d) LocalTarget=(%d,%d) Efficiency=%.2f Step=%d"),
        Agent.AgentID,
        DesignationID,
        MineGrid.X,
        MineGrid.Y,
        MineGrid.Z,
        LocalTarget.X,
        LocalTarget.Y,
        EfficiencyMod,
        StepNumber);

    return true;
}

void UMiningProcessor::TickMiningWork(
    FAgentData& Agent,
    int32 StepNumber,
    UWorld* World)
{
    float& Progress = AgentMiningProgress.FindOrAdd(Agent.AgentID);

    const float EfficiencyMod = FMath::Clamp(Agent.CurrentEfficiency, 0.1f, 2.0f);
    Progress += BaseMiningProgressPerStep * EfficiencyMod;

    UE_LOG(LogTemp, Verbose,
        TEXT("[MiningProcessor] Agent %d mining progress %.2f / %.2f Step=%d"),
        Agent.AgentID,
        Progress,
        RequiredMiningProgress,
        StepNumber);

    if (Progress >= RequiredMiningProgress)
    {
        CompleteMiningDesignation(Agent, StepNumber, World);
    }
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
    AgentMiningProgress.Remove(Agent.AgentID);

    Agent.State = EAgentState::Idle;
    Agent.CurrentJobTag = FGameplayTag();
    Agent.CurrentJobBuildingTag = FGameplayTag();
    Agent.ActiveReservationID = -1;
}

bool UMiningProcessor::ConvertMineGridToLocalTarget(
    int32 SettlementID,
    const FIntVector& MineGrid,
    UWorld* World,
    FIntPoint& OutLocalTarget) const
{
    const USettlementManager* SettlementManager = World
        ? World->GetSubsystem<USettlementManager>()
        : nullptr;

    const FSettlementState* Settlement = SettlementManager
        ? SettlementManager->GetSettlement(SettlementID)
        : nullptr;

    if (!Settlement)
    {
        return false;
    }

    const FIntPoint WorldCell(MineGrid.X, MineGrid.Y);
    OutLocalTarget = WorldCell - Settlement->WorldOrigin;
    return true;
}