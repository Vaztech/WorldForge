#include "MoraleProcessor.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Traits/WorldForgeTraitTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

// ── BF-11 IMPLEMENTATION ────────────────────────────────────────────────
// Morale is a smoothed blend of weighted colonist needs, with penalties
// for injury and incapacitation. MoodScore tracks morale over time so
// it doesn't jitter frame-to-frame. Thoughts are added at extreme values.
//
// Trait hook:
// This processor now reads colonist TraitTags and applies MoodModifier from
// DT_WorldForgeTraits. This keeps morale behavior data-driven instead of
// hardcoding Lucky, Clumsy, NEET, Hikikomori, Chuiboy, etc. directly here.
//
// Future note:
// This file owns morale-specific thought limits. Keep constants uniquely
// named so Unity builds do not collide with other processors.
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

    // Future us:
    // Update this path if DT_WorldForgeTraits is moved.
    // Current expected asset path:
    // Content/Data/DT_WorldForgeTraits
    UDataTable* GetWorldForgeTraitTable()
    {
        static UDataTable* CachedTraitTable = nullptr;
        static bool bAttemptedLoad = false;

        if (!bAttemptedLoad)
        {
            bAttemptedLoad = true;

            CachedTraitTable = LoadObject<UDataTable>(
                nullptr,
                TEXT("/Game/Data/DT_WorldForgeTraits.DT_WorldForgeTraits"));

            if (!CachedTraitTable)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[MoraleProcessor] Could not load DT_WorldForgeTraits at /Game/Data/DT_WorldForgeTraits. Trait mood modifiers will be skipped."));
            }
        }

        return CachedTraitTable;
    }

    float GetTraitMoodModifier(const FAgentData& Agent)
    {
        UDataTable* TraitTable = GetWorldForgeTraitTable();
        if (!TraitTable || Agent.TraitTags.IsEmpty())
        {
            return 0.0f;
        }

        float TotalMoodModifier = 0.0f;

        for (const FGameplayTag& TraitTag : Agent.TraitTags)
        {
            if (!TraitTag.IsValid())
            {
                continue;
            }

            const FName RowName = TraitTag.GetTagName();

            const FWorldForgeTraitDefinition* TraitDef =
                TraitTable->FindRow<FWorldForgeTraitDefinition>(
                    RowName,
                    TEXT("MoraleProcessor Trait Lookup"),
                    false);

            if (!TraitDef)
            {
                continue;
            }

            TotalMoodModifier += TraitDef->MoodModifier;
        }

        return TotalMoodModifier;
    }

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
        if (Agent.State == EAgentState::Dead)
        {
            continue;
        }

        // ── 1. Compute target morale from weighted needs ──
        const float TargetBase =
            Agent.FoodNeed * WeightFood +
            Agent.SleepNeed * WeightSleep +
            Agent.SocialNeed * WeightSocial +
            Agent.SafetyNeed * WeightSafety +
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

        // ── 3. Apply trait-based mood modifiers ──
        // Example:
        // Optimistic/Lucky traits can push morale up.
        // Pessimistic/NEET/Hikikomori-style traits can push morale down.
        const float TraitMoodModifier = GetTraitMoodModifier(Agent);

        const float TargetMorale = FMath::Clamp(
            TargetBase + Penalty + TraitMoodModifier,
            0.0f,
            100.0f);

        // ── 4. Smooth blend MoodScore toward target ──
        const float PreviousMood = Agent.MoodScore;

        Agent.MoodScore = FMath::Lerp(
            Agent.MoodScore,
            TargetMorale,
            MoraleBlendRate);

        Agent.MoodScore = FMath::Clamp(Agent.MoodScore, 0.0f, 100.0f);

        // ── 5. Add thoughts at extreme morale thresholds ──
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

    // ── 6. Fire event if any agent crossed the critical threshold ──
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (Bus)
    {
        int32 CriticalCount = 0;

        for (const FAgentData& Agent : Agents)
        {
            if (Agent.State != EAgentState::Dead &&
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
}