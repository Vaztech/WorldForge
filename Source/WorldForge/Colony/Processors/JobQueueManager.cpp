#include "JobQueueManager.h"

// ====================================================================
// PostJob
//
// Creates a new queued job entry from the incoming request.
// Assigns a unique JobID, clears assignment state, appends to Jobs,
// and returns a pointer to the newly inserted entry.
//
// WARNING:
// The returned pointer is only valid until the next Add/Remove on Jobs.
// ====================================================================
FJobRequest* FJobQueueManager::PostJob(const FJobRequest& Request)
{
    FJobRequest NewJob = Request;
    NewJob.JobID = NextJobID++;
    NewJob.bAssigned = false;
    NewJob.AssignedAgentID = -1;

    Jobs.Add(NewJob);
    return &Jobs.Last();
}

// ====================================================================
// FindJobByIDMutable
//
// Searches the queue for a job with the given JobID and returns
// mutable access if found.
// Returns nullptr if no matching job exists.
// ====================================================================
FJobRequest* FJobQueueManager::FindJobByIDMutable(int32 JobID)
{
    for (FJobRequest& J : Jobs)
    {
        if (J.JobID == JobID)
        {
            return &J;
        }
    }

    return nullptr;
}

// ====================================================================
// CancelJob
//
// Removes a specific job from the queue by JobID.
// If no such job exists, nothing happens.
// ====================================================================
void FJobQueueManager::CancelJob(int32 JobID)
{
    Jobs.RemoveAll([JobID](const FJobRequest& J)
        {
            return J.JobID == JobID;
        });
}

// ====================================================================
// CancelAllJobsForAgent
//
// Removes all jobs currently assigned to the given agent.
// Used when an agent dies, becomes invalid, or has jobs cancelled.
// ====================================================================
void FJobQueueManager::CancelAllJobsForAgent(int32 AgentID)
{
    Jobs.RemoveAll([AgentID](const FJobRequest& J)
        {
            return J.bAssigned && J.AssignedAgentID == AgentID;
        });
}

// ====================================================================
// FindBestJobForAgent
//
// Chooses the best unassigned job for a specific agent.
//
// Rules:
// - Job must belong to the same settlement
// - Job must not already be assigned
// - If the job requires a skill, the agent must have that skill > 0
// - Highest priority wins
// - If priority ties, higher skill value wins
//
// Returns nullptr if no valid job exists.
// ====================================================================
FJobRequest* FJobQueueManager::FindBestJobForAgent(
    int32 AgentID,
    const FAgentData& AgentData,
    int32 SettlementID)
{
    FJobRequest* Best = nullptr;
    int32 BestPri = -1;
    float BestSkill = -1.f;

    for (FJobRequest& J : Jobs)
    {
        if (J.bAssigned || J.SettlementID != SettlementID)
        {
            continue;
        }

        const int32 Pri = static_cast<int32>(J.Priority);
        float Skill = 0.f;

        // If job requires a skill, agent must have it
        if (J.RequiredSkill.IsValid())
        {
            Skill = GetAgentSkill(AgentData, J.RequiredSkill);
            if (Skill <= 0.f)
            {
                continue;
            }
        }

        // Higher priority wins; if tied, stronger skill wins
        if (Pri > BestPri || (Pri == BestPri && Skill > BestSkill))
        {
            BestPri = Pri;
            BestSkill = Skill;
            Best = &J;
        }
    }

    return Best;
}

// ====================================================================
// AssignJob
//
// Marks an existing job as assigned to the given agent.
// If JobID is not found, nothing happens.
// ====================================================================
void FJobQueueManager::AssignJob(int32 JobID, int32 AgentID)
{
    for (FJobRequest& J : Jobs)
    {
        if (J.JobID == JobID)
        {
            J.bAssigned = true;
            J.AssignedAgentID = AgentID;
            return;
        }
    }
}

// ====================================================================
// GetPendingCount
//
// Counts the number of unassigned jobs for a settlement.
// ====================================================================
int32 FJobQueueManager::GetPendingCount(int32 SID) const
{
    int32 Count = 0;

    for (const FJobRequest& J : Jobs)
    {
        if (J.SettlementID == SID && !J.bAssigned)
        {
            ++Count;
        }
    }

    return Count;
}

// ====================================================================
// GetAssignedCount
//
// Counts the number of assigned jobs for a settlement.
// ====================================================================
int32 FJobQueueManager::GetAssignedCount(int32 SID) const
{
    int32 Count = 0;

    for (const FJobRequest& J : Jobs)
    {
        if (J.SettlementID == SID && J.bAssigned)
        {
            ++Count;
        }
    }

    return Count;
}

// ====================================================================
// HasPendingJobs
//
// Convenience helper: returns true if settlement has any pending jobs.
// ====================================================================
bool FJobQueueManager::HasPendingJobs(int32 SID) const
{
    return GetPendingCount(SID) > 0;
}

// ====================================================================
// PruneCompletedJobs
//
// Removes orphaned assigned jobs whose AssignedAgentID is invalid.
// This is a lightweight cleanup pass for queue hygiene.
//
// Note:
// Current rule assumes an assigned job with AgentID < 0 is no longer valid.
// ====================================================================
void FJobQueueManager::PruneCompletedJobs()
{
    Jobs.RemoveAll([](const FJobRequest& J)
        {
            return J.bAssigned && J.AssignedAgentID < 0;
        });
}

// ====================================================================
// RestoreFromSave
//
// Restores the queue from serialized save data.
// Rebuilds NextJobID so newly posted jobs continue with unique IDs.
// ====================================================================
void FJobQueueManager::RestoreFromSave(const TArray<FJobRequest>& Saved)
{
    Jobs = Saved;
    NextJobID = 0;

    for (const FJobRequest& J : Jobs)
    {
        NextJobID = FMath::Max(NextJobID, J.JobID + 1);
    }
}

// ====================================================================
// GetAgentSkill
//
// Looks up the agent's numeric skill value for the given tag.
// Returns 0 if the skill is not present.
// ====================================================================
float FJobQueueManager::GetAgentSkill(const FAgentData& A, FGameplayTag Tag) const
{
    const float* V = A.Skills.Find(Tag);
    return V ? *V : 0.f;
}