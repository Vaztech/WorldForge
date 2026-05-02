#include "MoraleProcessor.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Engine/World.h"

// ── BF-11 IMPLEMENTATION ────────────────────────────────────────────────
// Morale is a smoothed blend of weighted colonist needs, with penalties
// for injury and incapacitation.  MoodScore tracks morale over time so
// it doesn't jitter frame-to-frame.  Thoughts are added at extreme values.
//
// Weight rationale (must sum to 1.0):
//   Food   30% — hunger is the most immediate stressor
//   Safety 25% — feeling unsafe dominates mood
//   Sleep  20% — fatigue is a strong mood driver
//   Social 15% — loneliness hurts but isn't survival-critical
//   Purpose 10% — existential need, slow-burn effect
// ─────────────────────────────────────────────────────────────────────────

namespace
{
    constexpr float MoraleBlendRate = 0.15f;

    constexpr float WeightFood    = 0.30f;
    constexpr float WeightSafety  = 0.25f;
    constexpr float WeightSleep   = 0.20f;
    constexpr float WeightSocial  = 0.15f;
    constexpr float WeightPurpose = 0.10f;

    constexpr float InjuryPenalty        = -15.0f;
    constexpr float IncapacitatedPenalty = -30.0f;

    constexpr float CriticalMoraleThreshold = 20.0f;
    constexpr float HighMoraleThreshold     = 80.0f;

    constexpr int32 MaxThoughts = 5;
}

void UMoraleProcessor::ProcessStep(
    int32 Step,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World
)
{
    for (FAgentData& Agent : Agents)
    {
        if (Agent.State == EAgentState::Dead)
        {
            continue;
        }

        // ── 1. Compute target morale from weighted needs ──
        const float TargetBase =
            Agent.FoodNeed    * WeightFood +
            Agent.SleepNeed   * WeightSleep +
            Agent.SocialNeed  * WeightSocial +
            Agent.SafetyNeed  * WeightSafety +
            Agent.PurposeNeed * WeightPurpose;

        // ── 2. Apply state-based penalties ──
        float Penalty = 0.0f;

        if (Agent.State == EAgentState::Incapacitated)
        {
            Penalty += IncapacitatedPenalty;
        }
        else if (Agent.bInjured)
        {
            Penalty += InjuryPenalty;
        }

        const float TargetMorale = FMath::Clamp(TargetBase + Penalty, 0.0f, 100.0f);

        // ── 3. Smooth blend MoodScore toward target ──
        const float PreviousMood = Agent.MoodScore;
        Agent.MoodScore = FMath::Lerp(Agent.MoodScore, TargetMorale, MoraleBlendRate);
        Agent.MoodScore = FMath::Clamp(Agent.MoodScore, 0.0f, 100.0f);

        // ── 4. Add thoughts at extreme morale thresholds ──
        if (Agent.MoodScore < CriticalMoraleThreshold && PreviousMood >= CriticalMoraleThreshold)
        {
            Agent.RecentThoughts.Add(TEXT("I can't take much more of this..."));
            if (Agent.RecentThoughts.Num() > MaxThoughts)
            {
                Agent.RecentThoughts.RemoveAt(0);
            }
        }
        else if (Agent.MoodScore > HighMoraleThreshold && PreviousMood <= HighMoraleThreshold)
        {
            Agent.RecentThoughts.Add(TEXT("Life is good here."));
            if (Agent.RecentThoughts.Num() > MaxThoughts)
            {
                Agent.RecentThoughts.RemoveAt(0);
            }
        }
    }

    // ── 5. Fire event if any agent crossed the critical threshold ──
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (Bus)
    {
        int32 CriticalCount = 0;
        for (const FAgentData& Agent : Agents)
        {
            if (Agent.State != EAgentState::Dead && Agent.MoodScore < CriticalMoraleThreshold)
            {
                ++CriticalCount;
            }
        }

        if (CriticalCount > 0)
        {
            FSimulationEvent E;
            E.EventType = FGameplayTag::RequestGameplayTag(
                FName(TEXT("WorldForge.Sim.Event.MoraleCritical")), false);
            E.SettlementID = SettlementID;
            E.AgentID = -1;
            E.StepNumber = Step;
            E.FloatPayload = static_cast<float>(CriticalCount);

            if (E.EventType.IsValid())
            {
                Bus->Dispatch(E);
            }
        }
    }
}
