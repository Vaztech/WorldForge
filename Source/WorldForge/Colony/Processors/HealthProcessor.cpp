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
        // Otherwise, agents slowly recover up to max health.
        if (Agent.FoodNeed <= 0.f)
        {
            // CHANGE MADE:
            // Starvation damage was previously far too aggressive at 2.0 health per step,
            // which caused synchronized mass deaths once multiple agents hit zero food need.
            // Reduced to 0.25 so starvation is still dangerous, but agents now have time
            // to recover or reach food before the entire colony collapses at once.
            Agent.Health -= 0.25f;
        }
        else if (Agent.Health < 100.f)
        {
            Agent.Health = FMath::Min(100.f, Agent.Health + 0.5f);
        }

        // Injury applies ongoing health loss and forces recovering state
        // unless the agent is already incapacitated.
        if (Agent.bInjured)
        {
            Agent.Health -= 0.5f;

            if (Agent.State != EAgentState::Recovering &&
                Agent.State != EAgentState::Incapacitated)
            {
                Agent.State = EAgentState::Recovering;
            }
        }

        // Enter incapacitated state at critical health.
        // Release reservations so other agents/jobs are not blocked.
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

        // Death handling.
        // Clears reservations, updates settlement population, and emits event.
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