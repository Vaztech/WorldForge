#include "HealthProcessor.h"
#include "ReservationSystem.h"
#include "EventBus.h"
#include "SettlementManager.h"
#include "WorldForgeTags.h"

void UHealthProcessor::ProcessStep(
    int32 StepNumber,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World)
{
    if (!World)
    {
        return;
    }

    UReservationSystem* RS = World->GetSubsystem<UReservationSystem>();
    UEventBus* Bus = World->GetSubsystem<UEventBus>();
    USettlementManager* SM = World->GetSubsystem<USettlementManager>();

    for (FAgentData& Agent : Agents)
    {
        // Only process living agents in this settlement.
        if (Agent.SettlementID != SettlementID || Agent.State == EAgentState::Dead)
        {
            continue;
        }

        // Starvation damages health over time.
        if (Agent.FoodNeed <= 0.f)
        {
            Agent.Health -= 0.25f;
        }
        else if (Agent.Health < 100.f)
        {
            Agent.Health = FMath::Min(100.f, Agent.Health + 0.5f);
        }

        // ADDED
        // Section G2: PressureLimit mechanic
        // If agent has this trait, allow higher max health (150 instead of 100)
        if (Agent.RaceTag == FGameplayTag::RequestGameplayTag(FName("WorldForge.Race.Tidecaller")))
        {
            // NOTE:
            // Minimal implementation per doc.
            // Full trait system not introduced yet.

            if (Agent.Health > 150.f)
            {
                Agent.Health = 150.f;
            }
        }

        // Injury applies ongoing health loss
        if (Agent.bInjured)
        {
            Agent.Health -= 0.5f;

            if (Agent.State != EAgentState::Recovering &&
                Agent.State != EAgentState::Incapacitated)
            {
                Agent.State = EAgentState::Recovering;
            }
        }

        // Enter incapacitated state
        if (Agent.Health <= 20.f &&
            Agent.State != EAgentState::Incapacitated &&
            Agent.State != EAgentState::Dead)
        {
            Agent.State = EAgentState::Incapacitated;

            if (RS)
            {
                RS->ReleaseAllForAgent(Agent.AgentID);
            }

            if (Bus)
            {
                FSimulationEvent E;
                E.EventType = TAG_Sim_Event_ColonistInjured;
                E.SettlementID = SettlementID;
                E.AgentID = Agent.AgentID;
                E.StepNumber = StepNumber;
                Bus->Dispatch(E);
            }
        }

        // Death handling
        if (Agent.Health <= 0.f && Agent.State != EAgentState::Dead)
        {
            Agent.State = EAgentState::Dead;
            Agent.Health = 0.f;

            if (RS)
            {
                RS->NotifyAgentDied(Agent.AgentID);
            }

            if (SM)
            {
                SM->DecrementPopulation(SettlementID);
            }

            if (Bus)
            {
                FSimulationEvent E;
                E.EventType = TAG_Sim_Event_ColonistDied;
                E.SettlementID = SettlementID;
                E.AgentID = Agent.AgentID;
                E.StepNumber = StepNumber;
                Bus->Dispatch(E);
            }

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[HealthProcessor] Agent %d died step %d."),
                Agent.AgentID,
                StepNumber);
        }
    }
}