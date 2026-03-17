#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "ProductionProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UProductionProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    // ====================================================================
    // Main processor entry point
    //
    // Called once per simulation step per settlement.
    // Responsibilities:
    // - Advance active production tasks
    // - Apply skill-modified progress
    // - Complete outputs when tasks finish
    // - Prune completed tasks at the end of the step (Phase 2 Fix 4)
    // ====================================================================
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Production"));
    }

    // ====================================================================
    // Public API
    // ====================================================================

    // Starts a production task for the given agent/job/building context.
    // Returns the new TaskID, or -1 if task creation failed.
    int32 StartProductionTask(
        int32 AgentID,
        int32 SettlementID,
        FGameplayTag JobTag,
        FGameplayTag BuildingTag);

    // Returns true if this agent already has an active production task.
    bool HasActiveTask(int32 AgentID) const;

    // Returns the current active task for this agent, or nullptr if none exists.
    const FActiveProductionTask* GetTask(int32 AgentID) const;

    // Save/load support
    const TArray<FActiveProductionTask>& GetAllTasksForSave() const { return ActiveTasks; }
    void RestoreFromSave(const TArray<FActiveProductionTask>& Saved);

    // ====================================================================
    // Tunables
    // ====================================================================

    // Base amount of work added each simulation step before skill modifiers.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Simulation|Production")
    float BaseProgressPerStep = 1.0f;

    // Number of completed steps to retain finished tasks before pruning.
    // Phase 2 fix path typically uses 0 so completed tasks are removed
    // immediately at the end of ProcessStep.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Simulation|Production")
    int32 CompletedTaskRetentionSteps = 0;

private:
    // ====================================================================
    // Internal helpers
    // ====================================================================

    // Returns recipe data for a given job tag.
    //
    // Phase 2 Fix 1:
    // Recipe selection must use typed gameplay tag constants
    // (for example TAG_Job_Cook / TAG_Job_Craft / TAG_Job_Harvest_Food),
    // never string-based comparisons like Tag.ToString().Contains(...).
    //
    // OutInputs: required consumed items
    // OutOutputs: produced items
    // OutRequiredProgress: work required to complete task
    bool GetRecipe(
        FGameplayTag JobTag,
        TMap<FGameplayTag, int32>& OutInputs,
        TMap<FGameplayTag, int32>& OutOutputs,
        float& OutRequiredProgress) const;

    // Returns a work speed modifier based on the agent's relevant skill.
    float GetSkillModifier(const FAgentData& Agent, FGameplayTag JobTag) const;

    // Completes a task:
    // - deposits outputs
    // - dispatches completion event
    // - resets agent state
    //
    // Note: completion does not imply immediate removal from ActiveTasks
    // unless ProcessStep prunes it afterward.
    void CompleteTask(
        FActiveProductionTask& Task,
        FAgentData& Agent,
        int32 StepNumber,
        UWorld* World);

    // ====================================================================
    // Data
    // ====================================================================

    // Active production tasks for all agents.
    // Completed entries must be pruned by ProcessStep to avoid unbounded growth
    // (Phase 2 Fix 4).
    TArray<FActiveProductionTask> ActiveTasks;

    // Monotonic TaskID source for new production tasks.
    int32 NextTaskID = 0;
};