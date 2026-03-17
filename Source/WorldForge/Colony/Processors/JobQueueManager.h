#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"

// ====================================================================
// FJobQueueManager
//
// Plain C++ helper owned by UJobProcessor.
// This is NOT a UObject and is intentionally not Blueprint-exposed.
//
// Responsibilities:
// - Store posted jobs
// - Track assigned/unassigned state
// - Find the best job for an agent based on priority + skill
// - Support save/load restoration
//
// Design notes:
// - Pointer returned by PostJob() is valid only until the next Add/Remove
// - This stays internal to simulation code
// - No actor references, no UObject ownership, deterministic-safe
// ====================================================================
struct FJobQueueManager
{
public:

    // ====================================================================
    // Job Posting / Removal
    // ====================================================================

    // Adds a new job to the queue.
    // Returns a pointer to the inserted job entry for immediate C++ use.
    // WARNING: pointer is only valid until the next Add/Remove on Jobs.
    FJobRequest* PostJob(const FJobRequest& Request);

    // Removes a specific job from the queue by JobID.
    void CancelJob(int32 JobID);

    // Removes all jobs currently assigned to the specified agent.
    void CancelAllJobsForAgent(int32 AgentID);

    // ====================================================================
    // Job Lookup / Matching
    // ====================================================================

    // Returns the best currently unassigned job for the given agent.
    //
    // Selection rules:
    // - Must belong to the same settlement
    // - Must not already be assigned
    // - If a required skill exists, the agent must have that skill
    // - Highest priority wins
    // - If priorities tie, higher agent skill wins
    //
    // Returns nullptr if no suitable job exists.
    FJobRequest* FindBestJobForAgent(
        int32 AgentID,
        const FAgentData& AgentData,
        int32 SettlementID);

    // Finds a specific job by JobID and returns mutable access.
    // Returns nullptr if not found.
    FJobRequest* FindJobByIDMutable(int32 JobID);

    // Marks an existing job as assigned to the specified agent.
    void AssignJob(int32 JobID, int32 AgentID);

    // ====================================================================
    // Queue Statistics
    // ====================================================================

    // Number of unassigned jobs for this settlement.
    int32 GetPendingCount(int32 SettlementID) const;

    // Number of assigned jobs for this settlement.
    int32 GetAssignedCount(int32 SettlementID) const;

    // True if this settlement currently has any pending jobs.
    bool HasPendingJobs(int32 SettlementID) const;

    // Read-only access to all jobs in the queue.
    const TArray<FJobRequest>& GetAllJobs() const
    {
        return Jobs;
    }

    // ====================================================================
    // Save / Load
    // ====================================================================

    // Restores the queue from saved job data.
    // Also rebuilds NextJobID so future posted jobs continue correctly.
    void RestoreFromSave(const TArray<FJobRequest>& Saved);

    // Returns current queue contents for save serialization.
    const TArray<FJobRequest>& GetJobsForSave() const
    {
        return Jobs;
    }

    // ====================================================================
    // Maintenance
    // ====================================================================

    // Removes completed/orphaned jobs from the queue.
    // Exact pruning rules are defined in the .cpp implementation.
    void PruneCompletedJobs();

private:

    // Returns the agent's value for a given skill tag.
    // Used during job matching to prefer better-qualified agents.
    float GetAgentSkill(const FAgentData& A, FGameplayTag SkillTag) const;

    // Internal storage of all queued jobs.
    TArray<FJobRequest> Jobs;

    // Monotonic ID source for newly posted jobs.
    int32 NextJobID = 0;
};