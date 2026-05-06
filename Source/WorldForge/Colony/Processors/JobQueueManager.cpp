#include "JobQueueManager.h"

// ====================================================================
// JobQueueManager
//
// Owns queued job storage and job selection scoring.
//
// Trait integration note:
// Trait effects are applied before this file receives AgentData.
// JobProcessor builds a temporary "scored agent" copy with:
// - trait WorkEfficiencyMultiplier applied to CurrentEfficiency
// - trait SkillModifiers applied to Skills
//
// This file then uses:
// - Priority
// - Skill
// - CurrentEfficiency
// - Distance
//
// to choose the best job.
// ====================================================================

namespace
{
    // Priority is intentionally very strong.
    // Emergency/High jobs should usually beat normal jobs.
    constexpr float PriorityWeight = 1000.0f;

    // Skill makes a colonist prefer jobs they are better suited for.
    // Trait SkillModifiers feed into this through JobProcessor.
    constexpr float SkillWeight = 4.0f;

    // Efficiency makes HardWorker/Fast/Slow/Lazy-style traits matter.
    // JobProcessor applies trait WorkEfficiencyMultiplier before scoring.
    constexpr float EfficiencyWeight = 250.0f;

    // Distance prevents colonists from always picking far-away jobs.
    constexpr float DistancePenaltyWeight = 1.5f;
}

FJobRequest* FJobQueueManager::PostJob(const FJobRequest& Request)
{
    FJobRequest NewJob = Request;
    NewJob.JobID = NextJobID++;
    NewJob.bAssigned = false;
    NewJob.AssignedAgentID = -1;

    Jobs.Add(NewJob);
    return &Jobs.Last();
}

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

void FJobQueueManager::CancelJob(int32 JobID)
{
    Jobs.RemoveAll([JobID](const FJobRequest& J)
        {
            return J.JobID == JobID;
        });
}

void FJobQueueManager::CancelAllJobsForAgent(int32 AgentID)
{
    Jobs.RemoveAll([AgentID](const FJobRequest& J)
        {
            return J.bAssigned && J.AssignedAgentID == AgentID;
        });
}

FJobRequest* FJobQueueManager::FindBestJobForAgent(
    int32 AgentID,
    const FAgentData& AgentData,
    int32 SettlementID)
{
    FJobRequest* Best = nullptr;
    float BestScore = -FLT_MAX;

    for (FJobRequest& J : Jobs)
    {
        if (J.bAssigned || J.SettlementID != SettlementID)
        {
            continue;
        }

        const int32 Pri = static_cast<int32>(J.Priority);

        float Skill = 0.0f;

        if (J.RequiredSkill.IsValid())
        {
            Skill = GetAgentSkill(AgentData, J.RequiredSkill);

            // If a job requires a skill and the agent has none,
            // skip it completely.
            if (Skill <= 0.0f)
            {
                continue;
            }
        }

        const int32 Distance =
            FMath::Abs(AgentData.CurrentGridPos.X - J.TargetGridPos.X) +
            FMath::Abs(AgentData.CurrentGridPos.Y - J.TargetGridPos.Y);

        const float Efficiency = FMath::Clamp(
            AgentData.CurrentEfficiency,
            0.0f,
            2.0f);

        // Final weighted score.
        // Higher score wins.
        const float Score =
            (Pri * PriorityWeight) +
            (Skill * SkillWeight) +
            (Efficiency * EfficiencyWeight) -
            (Distance * DistancePenaltyWeight);

        if (Score > BestScore)
        {
            BestScore = Score;
            Best = &J;
        }
    }

    return Best;
}

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

bool FJobQueueManager::HasPendingJobs(int32 SID) const
{
    return GetPendingCount(SID) > 0;
}

void FJobQueueManager::PruneCompletedJobs()
{
    Jobs.RemoveAll([](const FJobRequest& J)
        {
            return J.bAssigned && J.AssignedAgentID < 0;
        });
}

void FJobQueueManager::RestoreFromSave(const TArray<FJobRequest>& Saved)
{
    Jobs = Saved;
    NextJobID = 0;

    for (const FJobRequest& J : Jobs)
    {
        NextJobID = FMath::Max(NextJobID, J.JobID + 1);
    }
}

float FJobQueueManager::GetAgentSkill(const FAgentData& A, FGameplayTag Tag) const
{
    const float* V = A.Skills.Find(Tag);
    return V ? *V : 0.0f;
}