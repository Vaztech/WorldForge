#include "NeedsProcessor.h"

#include "EventBus.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"
#include "Modifiers/WorldForgeModifierResolver.h"

// ====================================================================
// SHARED MODIFIER SUPPORT
//
// NeedsProcessor no longer reads DT_WorldForgeTraits directly.
// It uses WorldForgeModifierResolver so decay reacts to:
//
// Traits (now)
// Equipment (later)
// Buffs / Debuffs (later)
//
// ====================================================================
namespace
{
    float GetMoodBasedDecayMultiplier(const FAgentData& Agent)
    {
        const FWorldForgeResolvedModifiers Mods =
            UWorldForgeModifierResolver::ResolveAgentModifiers(Agent);

        // Positive MoodModifier = slower decay
        // Negative MoodModifier = faster decay
        const float Multiplier = 1.0f - (Mods.MoodModifier * 0.01f);

        return FMath::Clamp(Multiplier, 0.25f, 2.0f);
    }

    float GetSocialDecayMultiplier(const FAgentData& Agent)
    {
        const FWorldForgeResolvedModifiers Mods =
            UWorldForgeModifierResolver::ResolveAgentModifiers(Agent);

        // Positive SocialModifier = slower decay
        // Negative SocialModifier = faster decay
        const float Multiplier = 1.0f - (Mods.SocialModifier * 0.01f);

        return FMath::Clamp(Multiplier, 0.25f, 2.0f);
    }
}

void UNeedsProcessor::ProcessStep(
    int32 Step,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World
)
{
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;

    for (FAgentData& Agent : Agents)
    {
        if (Agent.SettlementID != SettlementID || Agent.State == EAgentState::Dead)
        {
            continue;
        }

        const float MoodDecayMultiplier = GetMoodBasedDecayMultiplier(Agent);
        const float SocialDecayMultiplier = GetSocialDecayMultiplier(Agent);

        Agent.FoodNeed = FMath::Max(0.0f, Agent.FoodNeed - FoodDecay);
        Agent.SleepNeed = FMath::Max(0.0f, Agent.SleepNeed - (SleepDecay * MoodDecayMultiplier));
        Agent.SocialNeed = FMath::Max(0.0f, Agent.SocialNeed - (SocialDecay * SocialDecayMultiplier));
        Agent.SafetyNeed = FMath::Max(0.0f, Agent.SafetyNeed - (SafetyDecay * MoodDecayMultiplier));
        Agent.PurposeNeed = FMath::Max(0.0f, Agent.PurposeNeed - (PurposeDecay * MoodDecayMultiplier));

        if (!Bus)
        {
            continue;
        }

        if (Agent.FoodNeed < CriticalThreshold)
        {
            FSimulationEvent Event;
            Event.EventType = TAG_Sim_Event_NeedFoodCritical;
            Event.SettlementID = SettlementID;
            Event.AgentID = Agent.AgentID;
            Event.StepNumber = Step;
            Event.FloatPayload = Agent.FoodNeed;

            Bus->Dispatch(Event);
        }
    }
}