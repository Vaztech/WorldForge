#include "MoraleProcessor.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Modifiers/WorldForgeModifierResolver.h"
#include "Engine/World.h"

// ── BF-11 IMPLEMENTATION ────────────────────────────────────────────────
// Morale is a smoothed blend of weighted colonist needs, with penalties
// for injury and incapacitation.
//
// MoraleProcessor no longer loads DT_WorldForgeTraits directly.
// It asks WorldForgeModifierResolver for resolved values.
//
// This means morale now supports:
// Traits now + equipment/buffs/debuffs later.
// ─────────────────────────────────────────────────────────────────────────

namespace
{
    constexpr float MoraleBlendRate = 0.15f;

    constexpr float WeightFood = 0.30f;
    constexpr float WeightSafety = 0.25f;
    constexpr float WeightSleep = 0.20f;
    constexpr float WeightSocial = 0.15f;
    constexpr float WeightPurpose = 0.10f;

    constexpr float InjuryPenalty = -15.0f;
    constexpr float IncapacitatedPenalty = -30.0f;

    constexpr float CriticalMoraleThreshold = 20.0f;
    constexpr float HighMoraleThreshold = 80.0f;

    constexpr int32 MoraleMaxThoughts = 5;

    void AddMoraleThought(FAgentData& Agent, const FString& Thought)
    {
        Agent.RecentThoughts.Add(Thought);

        if (Agent.RecentThoughts.Num() > MoraleMaxThoughts)
        {
            Agent.RecentThoughts.RemoveAt(0);
        }
    }
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
        if (Agent.SettlementID != SettlementID || Agent.State == EAgentState::Dead)
        {
            continue;
        }

        const float PreviousMood = Agent.MoodScore;

        // Pull all current modifiers from one shared resolver.
        // Today: traits.
        // Later: traits + equipment + buffs + debuffs.
        const FWorldForgeResolvedModifiers Mods =
            UWorldForgeModifierResolver::ResolveAgentModifiers(Agent);

        const float TargetBase =
            Agent.FoodNeed * WeightFood +
            Agent.SleepNeed * WeightSleep +
            Agent.SocialNeed * WeightSocial +
            Agent.SafetyNeed * WeightSafety +
            Agent.PurposeNeed * WeightPurpose;

        float Penalty = 0.0f;

        if (Agent.State == EAgentState::Incapacitated)
        {
            Penalty += IncapacitatedPenalty;
        }
        else if (Agent.bInjured)
        {
            Penalty += InjuryPenalty;
        }

        // MoodModifier is a stable bias to target morale.
        const float TargetMorale = FMath::Clamp(
            TargetBase + Penalty + Mods.MoodModifier,
            0.0f,
            100.0f);

        // Smooth toward target so morale does not jitter.
        Agent.MoodScore = FMath::Lerp(
            Agent.MoodScore,
            TargetMorale,
            MoraleBlendRate);

        // MoraleDeltaPerStep is slow drift after smoothing.
        Agent.MoodScore = FMath::Clamp(
            Agent.MoodScore + Mods.MoraleDeltaPerStep,
            0.0f,
            100.0f);

        if (Agent.MoodScore < CriticalMoraleThreshold &&
            PreviousMood >= CriticalMoraleThreshold)
        {
            AddMoraleThought(Agent, TEXT("I can't take much more of this..."));
        }
        else if (Agent.MoodScore > HighMoraleThreshold &&
            PreviousMood <= HighMoraleThreshold)
        {
            AddMoraleThought(Agent, TEXT("Life is good here."));
        }
    }

    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (!Bus)
    {
        return;
    }

    int32 CriticalCount = 0;

    for (const FAgentData& Agent : Agents)
    {
        if (Agent.SettlementID == SettlementID &&
            Agent.State != EAgentState::Dead &&
            Agent.MoodScore < CriticalMoraleThreshold)
        {
            ++CriticalCount;
        }
    }

    if (CriticalCount > 0)
    {
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.MoraleCritical")),
            false);
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