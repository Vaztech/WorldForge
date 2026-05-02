#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "JobQueueManager.h"


#include "JobProcessor.generated.h" // ALWAYS LAST

// ====================================================================
// UJobProcessor
//
// Purpose:
// Handles agent job logic for one simulation step.
// Responsibilities:
// - Move agents toward assigned targets
// - Handle critical need jobs (eat / sleep)
// - Assign queued jobs to idle agents
// - Generate fallback harvest jobs when no queue exists
//
// Architectural rules:
// - Runs only inside the fixed-step processor pipeline
// - Modifies simulation data only
// - Does not spawn actors
// - Uses EventBus for outward communication
// ====================================================================
UCLASS()
class WORLDFORGE_API UJobProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:

    // ====================================================================
    // Main processor entry point
    //
    // Called once per simulation step for each settlement.
    // This is the top-level update function for all job-related behavior.
    // ====================================================================
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    // Returns the processor's display/debug name.
    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Job"));
    }

    // ===============================
    // Public API (Blueprint safe)
    // ===============================
    //
    // These functions are safe to expose to Blueprint because they use
    // Blueprint-compatible parameter and return types.
    // They are intended for debug/UI/control access, not core sim flow.
    // ===============================

    // Cancels all jobs currently assigned to the given agent.
    // Also notifies ReservationSystem so reserved targets can be released.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Simulation|Jobs")
    void CancelJobsForAgent(int32 AgentID, UWorld* World);

    // Returns the number of unassigned jobs currently waiting in queue
    // for this settlement.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Simulation|Jobs")
    int32 GetPendingJobCount(int32 SettlementID) const;

    // Returns the number of currently assigned jobs for this settlement.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Simulation|Jobs")
    int32 GetAssignedJobCount(int32 SettlementID) const;

    FGameplayTag GetSkillTagForJob(FGameplayTag JobTag) const;

    // ===============================
    // Public API (C++ only)
    // ===============================
    //
    // These are intentionally NOT exposed to Blueprint because they return
    // pointers/references or operate on raw sim data in ways not suitable
    // for Blueprint.
    // ===============================

    // Posts a job into the internal queue and returns a pointer to the
    // inserted job entry for immediate C++ use.
    FJobRequest* PostJob(const FJobRequest& Request);

    // Read-only access to all jobs currently in the queue.
    const TArray<FJobRequest>& GetAllJobs() const;

    // Restores the queue from saved data.
    void RestoreJobQueue(const TArray<FJobRequest>& Saved);

    // Returns queue data for save/load serialization.
    const TArray<FJobRequest>& GetJobsForSave() const;

    // ===============================
    // Tunables (Blueprint editable)
    // ===============================
    //
    // These values can be adjusted in editor or Blueprint for balancing,
    // debugging, and testing.
    // ===============================

    // Food need value below which the processor creates an emergency eat job.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Simulation|Needs")
    float FoodCriticalThreshold = 20.f;

    // Sleep need value below which the processor creates an emergency sleep job.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Simulation|Needs")
    float SleepCriticalThreshold = 20.f;

    // Number of grid cells an agent moves per simulation step.
    // Phase 2 default was 1; bumped to 4 during testing so colonist motion is
    // visible at typical PIE camera distances. Phase 3 can extend this with
    // race/terrain/path modifiers.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Simulation|Movement")
    int32 MovementCellsPerStep = 4;

private:

    // ===============================
    // Internal Simulation Logic
    // ===============================
    //
    // These helpers are private because they are only part of the internal
    // deterministic job-processing flow.
    // ===============================

    // Advances an agent toward its CurrentNavTarget.
    // Called when agent is already in MovingToTarget state.
    void TickMovement(
        FAgentData& Agent,
        int32 StepNumber,
        UWorld* World);

    // Called when an agent reaches its CurrentNavTarget.
    // Decides what happens next:
    // - eat
    // - sleep
    // - start work
    void OnAgentArrived(
        FAgentData& Agent,
        int32 StepNumber,
        UWorld* World);

    // Checks whether the agent has urgent hunger/sleep needs and, if so,
    // creates and assigns an emergency job immediately.
    // Returns true if a critical need job was handled.
    bool HandleCriticalNeeds(
        FAgentData& Agent,
        int32 SettlementID,
        int32 StepNumber,
        UWorld* World);

    // Attempts to find the best queued job for this agent and assign it.
    // Returns true if a job was successfully assigned.
    bool TryAssignQueuedJob(
        FAgentData& Agent,
        int32 SettlementID,
        int32 StepNumber,
        UWorld* World);

    
    // Generates fallback harvest jobs when no work is available in queue.
    // This keeps the colony productive during idle periods.
    void GenerateIdleHarvestJobs(
        int32 SettlementID,
        int32 StepNumber,
        UWorld* World);

    // Performs the actual assignment of a job to an agent:
    // - marks queue entry assigned
    // - updates agent state/data
    // - sets nav target
    // - dispatches JobAssigned event
    void AssignJobToAgent(
        FAgentData& Agent,
        FJobRequest& Job,
        int32 ReservationID,
        int32 StepNumber,
        UWorld* World);

    // ===============================
    // Data
    // ===============================

    // Internal job queue manager owned by this processor.
    // Stores pending/assigned jobs and handles job matching logic.
    FJobQueueManager JobQueue;

    // Tracks the last step when idle harvest jobs were generated.
    // Prevents spamming new fallback jobs every single simulation step.
    UPROPERTY()
    int32 LastIdleGenStep = -999;

    
};