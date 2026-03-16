#include "NeedsProcessor.h"

#include "EventBus.h"
#include "WorldForgeDataTypes.h"

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

        Agent.FoodNeed = FMath::Max(0.f, Agent.FoodNeed - FoodDecay);
        Agent.SleepNeed = FMath::Max(0.f, Agent.SleepNeed - SleepDecay);
        Agent.SocialNeed = FMath::Max(0.f, Agent.SocialNeed - SocialDecay);
        Agent.SafetyNeed = FMath::Max(0.f, Agent.SafetyNeed - SafetyDecay);
        Agent.PurposeNeed = FMath::Max(0.f, Agent.PurposeNeed - PurposeDecay);

        if (!Bus)
        {
            continue;
        }

        if (Agent.FoodNeed < CriticalThreshold)
        {
            FSimulationEvent Event;
            Event.EventType = FGameplayTag::RequestGameplayTag(
                FName(TEXT("WorldForge.Sim.Event.NeedFoodCritical"))
            );
            Event.SettlementID = SettlementID;
            Event.AgentID = Agent.AgentID;
            Event.StepNumber = Step;
            Event.FloatPayload = Agent.FoodNeed;

            Bus->Dispatch(Event);
        }
    }
}