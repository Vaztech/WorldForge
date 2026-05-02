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

        // ─────────────────────────────────────────────
        // Race-based health baseline
        // ─────────────────────────────────────────────

        Agent.MaxHealth = 100.f;

        // Tidecallers have stronger pressure adaptation / constitution.
        if (Agent.RaceTag == FGameplayTag::RequestGameplayTag(FName("WorldForge.Race.Tidecaller")))
        {
            Agent.MaxHealth = 150.f;
        }
        else if (Agent.RaceTag == FGameplayTag::RequestGameplayTag(FName("WorldForge.Race.Dwarf")))
        {
            Agent.MaxHealth = 125.f;
        }
        else if (Agent.RaceTag == FGameplayTag::RequestGameplayTag(FName("WorldForge.Race.Oni")))
        {
            Agent.MaxHealth = 135.f;
        }
        else if (Agent.RaceTag == FGameplayTag::RequestGameplayTag(FName("WorldForge.Race.Construct")))
        {
            Agent.MaxHealth = 160.f;
        }

        Agent.Health = FMath::Clamp(Agent.Health, 0.f, Agent.MaxHealth);

        // ─────────────────────────────────────────────
        // Starvation / passive recovery
        // ─────────────────────────────────────────────

        if (Agent.FoodNeed <= 0.f)
        {
            Agent.Health -= 0.25f;
            Agent.CurrentEfficiency = FMath::Min(Agent.CurrentEfficiency, 0.75f);
        }
        else if (!Agent.bInjured && Agent.Health < Agent.MaxHealth)
        {
            Agent.Health = FMath::Min(Agent.MaxHealth, Agent.Health + 0.5f);
        }

        // ─────────────────────────────────────────────
        // Treatment timers
        // ─────────────────────────────────────────────

        if (Agent.TreatmentStepsRemaining > 0)
        {
            Agent.TreatmentStepsRemaining--;

            if (Agent.TreatmentStepsRemaining <= 0)
            {
                Agent.TreatmentStepsRemaining = 0;
                Agent.bReceivingTreatment = false;
            }
        }

        // ─────────────────────────────────────────────
        // Injury processing
        // ─────────────────────────────────────────────

        if (Agent.bInjured)
        {
            if (Agent.InjuryStepsRemaining > 0)
            {
                Agent.InjuryStepsRemaining--;
            }

            float SeverityHealthDrain = 0.0f;
            float InjuryEfficiency = 0.6f;

            switch (Agent.InjurySeverity)
            {
            case 1: // Minor
                SeverityHealthDrain = 0.05f;
                InjuryEfficiency = 0.75f;
                break;

            case 2: // Moderate
                SeverityHealthDrain = 0.15f;
                InjuryEfficiency = 0.55f;
                break;

            case 3: // Severe
                SeverityHealthDrain = 0.35f;
                InjuryEfficiency = 0.30f;
                break;

            default:
                SeverityHealthDrain = 0.05f;
                InjuryEfficiency = 0.75f;
                break;
            }

            Agent.Health -= SeverityHealthDrain;
            Agent.CurrentEfficiency = InjuryEfficiency;

            float HealingAmount = Agent.BaseHealingRate * Agent.MedicalCareMultiplier;

            if (Agent.bReceivingTreatment)
            {
                HealingAmount *= 2.0f;
            }

            // Hygiene and immunity affect recovery quality.
            HealingAmount *= FMath::Clamp(Agent.Hygiene, 0.25f, 1.25f);
            HealingAmount *= FMath::Clamp(Agent.Immunity, 0.25f, 1.5f);

            if (Agent.FoodNeed > 20.f && Agent.SleepNeed > 10.f)
            {
                Agent.Health = FMath::Min(Agent.MaxHealth, Agent.Health + HealingAmount);
            }

            if (Agent.State != EAgentState::Recovering &&
                Agent.State != EAgentState::Incapacitated)
            {
                Agent.State = EAgentState::Recovering;
            }

            // Injury ends when its duration expires and the agent is not critically hurt.
            if (Agent.InjuryStepsRemaining <= 0 && Agent.Health > 40.f)
            {
                Agent.bInjured = false;
                Agent.InjurySeverity = 0;
                Agent.InjuryStepsRemaining = 0;
                Agent.CurrentEfficiency = 1.0f;

                if (Agent.State == EAgentState::Recovering)
                {
                    Agent.State = EAgentState::Idle;
                }

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[HealthProcessor] Agent %d recovered from injury at step %d."),
                    Agent.AgentID,
                    StepNumber);
            }
        }
        else
        {
            Agent.CurrentEfficiency = 1.0f;
            Agent.InjurySeverity = 0;
            Agent.InjuryStepsRemaining = 0;
        }

        // ─────────────────────────────────────────────
        // Incapacitation
        // ─────────────────────────────────────────────

        if (Agent.Health <= 20.f &&
            Agent.State != EAgentState::Incapacitated &&
            Agent.State != EAgentState::Dead)
        {
            Agent.State = EAgentState::Incapacitated;
            Agent.CurrentEfficiency = 0.0f;

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

        // Recover from incapacitated if health improves enough.
        if (Agent.State == EAgentState::Incapacitated && Agent.Health > 35.f)
        {
            Agent.State = Agent.bInjured ? EAgentState::Recovering : EAgentState::Idle;
            Agent.CurrentEfficiency = Agent.bInjured ? 0.5f : 1.0f;
        }

        // ─────────────────────────────────────────────
        // Death handling
        // ─────────────────────────────────────────────

        if (Agent.Health <= 0.f && Agent.State != EAgentState::Dead)
        {
            Agent.State = EAgentState::Dead;
            Agent.Health = 0.f;
            Agent.CurrentEfficiency = 0.0f;
            Agent.bInjured = false;
            Agent.InjurySeverity = 0;
            Agent.InjuryStepsRemaining = 0;

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