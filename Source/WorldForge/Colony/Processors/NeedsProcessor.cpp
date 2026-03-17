#include "NeedsProcessor.h"

#include "EventBus.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h" // Phase 2 fix: use typed gameplay tag constants

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
        // Only process living agents that belong to this settlement.
        if (Agent.SettlementID != SettlementID || Agent.State == EAgentState::Dead)
        {
            continue;
        }

        // Decay core needs deterministically once per simulation step.
        Agent.FoodNeed = FMath::Max(0.f, Agent.FoodNeed - FoodDecay);
        Agent.SleepNeed = FMath::Max(0.f, Agent.SleepNeed - SleepDecay);
        Agent.SocialNeed = FMath::Max(0.f, Agent.SocialNeed - SocialDecay);
        Agent.SafetyNeed = FMath::Max(0.f, Agent.SafetyNeed - SafetyDecay);
        Agent.PurposeNeed = FMath::Max(0.f, Agent.PurposeNeed - PurposeDecay);

        // If there is no event bus, needs still decay; we just skip event emission.
        if (!Bus)
        {
            continue;
        }

        // Emit critical-food event using typed gameplay tags.
        // This replaces string-based RequestGameplayTag usage.
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