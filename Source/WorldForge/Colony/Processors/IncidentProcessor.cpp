#include "IncidentProcessor.h"
#include "../Core/WorldForgeRNG.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// ── BF-12 IMPLEMENTATION ────────────────────────────────────────────────
// Random incidents fire once per sim day with a configurable chance.
// Each incident affects agents directly and dispatches an EventBus event
// so other systems (narrative, economy, morale) can react.
//
// Incident types:
//   0 = Disease    — one random agent loses Health, gains thought
//   1 = Injury     — one random agent becomes injured
//   2 = Spoilage   — all agents lose a small amount of FoodNeed
//   3 = GoodOmen   — all agents gain a small morale boost thought
// ─────────────────────────────────────────────────────────────────────────

namespace
{
    // Check for incidents once per sim day (50 steps = 1 day)
    constexpr int32 StepsPerDay = 50;

    // Base chance of an incident per day (5%)
    constexpr float IncidentChancePerDay = 0.05f;
    
        // Disease severity
    constexpr float DiseaseHealthLoss = 15.0f;

    // Food spoilage — per-agent FoodNeed loss
    constexpr float SpoilageFoodLoss = 8.0f;

    // Number of incident types
    constexpr int32 IncidentTypeCount = 4;

    constexpr int32 MaxThoughts = 5;

    // ── Health system incident tuning ──
    // Minor injuries temporarily reduce work speed but should not instantly cripple the colony.
    constexpr int32 MinorInjuryMinSteps = 50;
    constexpr int32 MinorInjuryMaxSteps = 150;
    constexpr int32 MinorInjurySeverity = 1;
    constexpr float MinorInjuryEfficiency = 0.60f;
    constexpr float MinorInjuryHealthLoss = 5.0f;

    // Disease impact is softened by Immunity and worsened by poor Hygiene.
    constexpr float MinDiseaseMultiplier = 0.35f;
    constexpr float MaxDiseaseMultiplier = 2.00f;

    void AddThought(FAgentData& Agent, const FString& Thought)
    {
        Agent.RecentThoughts.Add(Thought);
        if (Agent.RecentThoughts.Num() > MaxThoughts)
        {
            Agent.RecentThoughts.RemoveAt(0);
        }
    }

    float GetDiseaseMultiplier(const FAgentData& Agent)
    {
        const float SafeImmunity = FMath::Max(0.1f, Agent.Immunity);
        const float HygieneRisk = FMath::Clamp(1.25f - Agent.Hygiene, 0.5f, 1.5f);
        return FMath::Clamp(HygieneRisk / SafeImmunity, MinDiseaseMultiplier, MaxDiseaseMultiplier);
    }
}

void UIncidentProcessor::ProcessStep(
    int32 Step,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World
)
{
    // Only check once per sim day
    if (Step % StepsPerDay != 0)
    {
        return;
    }

    if (Agents.Num() == 0 || !World)
    {
        return;
    }

    // Get RNG from GameInstance
    UGameInstance* GI = World->GetGameInstance();
    UWorldForgeRNG* RNG = GI ? GI->GetSubsystem<UWorldForgeRNG>() : nullptr;
    if (!RNG)
    {
        return;
    }

    // Roll for incident
    const float Roll = RNG->NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY);
    if (Roll > IncidentChancePerDay)
    {
        return;
    }

    // Pick incident type
    const int32 IncidentType = RNG->NextInt(
      UWorldForgeRNG::BRANCH_PREHISTORY, 0, IncidentTypeCount - 1);
    


    // Count living agents for random target selection
    TArray<int32> LivingIndices;
    for (int32 i = 0; i < Agents.Num(); ++i)
    {
        if (Agents[i].SettlementID == SettlementID &&
            Agents[i].State != EAgentState::Dead)
        {
            LivingIndices.Add(i);
        }
    }

    if (LivingIndices.Num() == 0)
    {
        return;
    }

    UEventBus* Bus = World->GetSubsystem<UEventBus>();
    FSimulationEvent Event;
    Event.SettlementID = SettlementID;
    Event.StepNumber = Step;
    Event.AgentID = -1;

    switch (IncidentType)
    {
    case 0: // ── Disease ──
    {
        const int32 TargetIdx = LivingIndices[
            RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, LivingIndices.Num() - 1)];
        FAgentData& Target = Agents[TargetIdx];

        const float DiseaseMultiplier = GetDiseaseMultiplier(Target);
        const float FinalHealthLoss = DiseaseHealthLoss * DiseaseMultiplier;

        Target.Health = FMath::Max(0.0f, Target.Health - FinalHealthLoss);

        // Poor hygiene makes future disease more likely/severe until restored by later systems.
        Target.Hygiene = FMath::Clamp(Target.Hygiene - 0.05f, 0.0f, 1.0f);

        AddThought(Target, TEXT("I feel ill..."));

        Event.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.Disease")), false);
        Event.AgentID = Target.AgentID;
        Event.FloatPayload = FinalHealthLoss;

        UE_LOG(LogTemp, Log,
            TEXT("[IncidentProcessor] Disease: Agent %d lost %.1f health. Immunity=%.2f Hygiene=%.2f Settlement=%d Step=%d"),
            Target.AgentID,
            FinalHealthLoss,
            Target.Immunity,
            Target.Hygiene,
            SettlementID,
            Step);
        break;
    }

    case 1: // ── Injury ──
    {
        const int32 TargetIdx = LivingIndices[
            RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, LivingIndices.Num() - 1)];
        FAgentData& Target = Agents[TargetIdx];

        if (!Target.bInjured)
        {
            const int32 InjurySteps = RNG->NextInt(
                UWorldForgeRNG::BRANCH_PREHISTORY,
                MinorInjuryMinSteps,
                MinorInjuryMaxSteps);

            Target.bInjured = true;
            Target.InjurySeverity = MinorInjurySeverity;
            Target.InjuryStepsRemaining = InjurySteps;
            Target.CurrentEfficiency = FMath::Min(Target.CurrentEfficiency, MinorInjuryEfficiency);
            Target.Health = FMath::Max(0.0f, Target.Health - MinorInjuryHealthLoss);

            // Injury creates a reason for the HealthProcessor to move the colonist into Recovering.
            AddThought(Target, TEXT("I hurt myself working."));

            Event.EventType = FGameplayTag::RequestGameplayTag(
                FName(TEXT("WorldForge.Sim.Event.Injury")), false);
            Event.AgentID = Target.AgentID;
            Event.FloatPayload = MinorInjuryHealthLoss;

            UE_LOG(LogTemp, Log,
                TEXT("[IncidentProcessor] Injury: Agent %d injured. Severity=%d Efficiency=%.2f Steps=%d Settlement=%d Step=%d"),
                Target.AgentID,
                Target.InjurySeverity,
                Target.CurrentEfficiency,
                Target.InjuryStepsRemaining,
                SettlementID,
                Step);
        }
        break;
    }

    case 2: // ── Food Spoilage ──
    {
        for (int32 Idx : LivingIndices)
        {
            Agents[Idx].FoodNeed = FMath::Max(
                0.0f, Agents[Idx].FoodNeed - SpoilageFoodLoss);
        }

        Event.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.FoodSpoilage")), false);
        Event.FloatPayload = SpoilageFoodLoss;

        UE_LOG(LogTemp, Log,
            TEXT("[IncidentProcessor] Food spoilage: %d agents affected. Settlement=%d Step=%d"),
            LivingIndices.Num(), SettlementID, Step);
        break;
    }

    case 3: // ── Good Omen (positive event) ──
    {
        for (int32 Idx : LivingIndices)
        {
            // Small direct mood boost
            Agents[Idx].MoodScore = FMath::Min(
                100.0f, Agents[Idx].MoodScore + 5.0f);
            AddThought(Agents[Idx], TEXT("Something good happened today."));
        }

        Event.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.GoodOmen")), false);
        Event.FloatPayload = 5.0f;

        UE_LOG(LogTemp, Log,
            TEXT("[IncidentProcessor] Good omen: %d agents boosted. Settlement=%d Step=%d"),
            LivingIndices.Num(), SettlementID, Step);
        break;
    }

    default:
        break;
    }

    // Dispatch event
    if (Bus && Event.EventType.IsValid())
    {
        Bus->Dispatch(Event);
    }
}