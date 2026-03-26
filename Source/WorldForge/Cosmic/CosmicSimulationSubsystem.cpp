#include "CosmicSimulationSubsystem.h"
#include "WorldEconomySimulation.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Engine/World.h"

void UCosmicSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[CosmicSim] Initialized."));
}

void UCosmicSimulationSubsystem::Deinitialize()
{
    EntityStates.Empty();          // ADDED
    AnchorRecords.Empty();         // ADDED
    NarrativePressure = FNarrativePressureState(); // ADDED
    NextEntityID = 0;              // ADDED
    NextAnchorID = 0;              // ADDED

    Super::Deinitialize();
}

void UCosmicSimulationSubsystem::OnSimStep(int32 StepNumber, int32 SettlementID,
    TArray<FAgentData>& Agents, UWorld* World)
{
    (void)SettlementID; // ADDED
    (void)Agents;       // ADDED

    if (!World) return;

    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    UEventBus* Bus = World->GetSubsystem<UEventBus>();

    // 1. Decay all entity power sources each step
    DecayAllEntities();

    // 2. Add anchor power once per in-game day
    if (SM && StepNumber % StepsPerDay == 0)
    {
        ProcessAnchorPower(StepNumber, SM);
    }

    // 3. Check emergence / fading transitions
    CheckEmergenceTransitions(StepNumber, World);

    // 4. Update narrative pressure from recent events
    if (Bus)
    {
        UpdateNarrativePressure(StepNumber, World, Bus->GetRecentEvents());
    }
}

// ── Entity registration ────────────────────────────────────────────────────
int32 UCosmicSimulationSubsystem::RegisterEntity(
    FGameplayTag EntityTag,
    const TArray<FGameplayTag>& DomainTags,
    float EmergenceThreshold)
{
    // Avoid duplicates
    for (const FCosmicEntityState& E : EntityStates)
    {
        if (E.EntityTag == EntityTag)
        {
            return E.EntityID;
        }
    }

    FCosmicEntityState S;
    S.EntityID = NextEntityID++;
    S.EntityTag = EntityTag;
    S.Status = ECosmicEntityStatus::Dormant;
    S.DomainTags = DomainTags;
    S.EmergenceThreshold = EmergenceThreshold;
    EntityStates.Add(S);
    return S.EntityID;
}

// ── Anchor management ──────────────────────────────────────────────────────
int32 UCosmicSimulationSubsystem::RegisterAnchor(
    int32 SettlementID,
    FIntPoint GridPosition,
    FGameplayTag BoundEntityTag,
    float PowerPerSimDay)
{
    FAnchorRecord A;
    A.AnchorID = NextAnchorID++;
    A.SettlementID = SettlementID;
    A.GridPosition = GridPosition;
    A.BoundEntityTag = BoundEntityTag;
    A.PowerPerSimDay = PowerPerSimDay;
    AnchorRecords.Add(A);

    UE_LOG(LogTemp, Log, TEXT("[CosmicSim] Anchor %d registered for entity %s at settlement %d"),
        A.AnchorID, *BoundEntityTag.ToString(), SettlementID);
    return A.AnchorID;
}

void UCosmicSimulationSubsystem::DestroyAnchor(int32 AnchorID)
{
    AnchorRecords.RemoveAll([AnchorID](const FAnchorRecord& A)
        {
            return A.AnchorID == AnchorID;
        });
}

// ── Power manipulation ─────────────────────────────────────────────────────
void UCosmicSimulationSubsystem::AddFearPower(FGameplayTag Tag, float Amount)
{
    AddPowerToSource(Tag, Amount, &FCosmicEntityState::FearPower);
}

void UCosmicSimulationSubsystem::AddAffinityPower(FGameplayTag Tag, float Amount)
{
    AddPowerToSource(Tag, Amount, &FCosmicEntityState::AffinityPower);
}

void UCosmicSimulationSubsystem::AddCatastrophePower(FGameplayTag Tag, float Amount)
{
    AddPowerToSource(Tag, Amount, &FCosmicEntityState::CatastrophePower);
}

void UCosmicSimulationSubsystem::AddBeliefPower(FGameplayTag Tag, float Amount)
{
    AddPowerToSource(Tag, Amount, &FCosmicEntityState::BeliefPower);
}

void UCosmicSimulationSubsystem::AddIrritationPower(FGameplayTag Tag, float Amount)
{
    AddPowerToSource(Tag, Amount, &FCosmicEntityState::IrritationPower);
}

void UCosmicSimulationSubsystem::AddPowerToSource(
    FGameplayTag EntityTag,
    float Amount,
    float FCosmicEntityState::* PowerField)
{
    for (FCosmicEntityState& E : EntityStates)
    {
        if (E.EntityTag == EntityTag)
        {
            E.*PowerField = FMath::Max(0.0f, E.*PowerField + Amount);
            return;
        }
    }
}

// ── Decay ──────────────────────────────────────────────────────────────────
void UCosmicSimulationSubsystem::DecayAllEntities()
{
    for (FCosmicEntityState& E : EntityStates)
    {
        DecayEntityPower(E);
    }
}

/*static*/ void UCosmicSimulationSubsystem::DecayEntityPower(FCosmicEntityState& E)
{
    E.FearPower = FMath::Max(0.0f, E.FearPower - E.FearDecayRate);
    E.AffinityPower = FMath::Max(0.0f, E.AffinityPower - E.AffinityDecayRate);
    E.CatastrophePower = FMath::Max(0.0f, E.CatastrophePower - E.CatastropheDecayRate);
    E.AnchorPower = FMath::Max(0.0f, E.AnchorPower - E.AnchorDecayRate);
    E.BeliefPower = FMath::Max(0.0f, E.BeliefPower - E.BeliefDecayRate);
    E.IrritationPower = FMath::Max(0.0f, E.IrritationPower - E.IrritationDecayRate);

    E.TotalPower = E.FearPower + E.AffinityPower + E.CatastrophePower
        + E.AnchorPower + E.BeliefPower + E.IrritationPower;

    // Decrement active intervention counters
    if (E.FlowBoostStepsRemaining > 0)  E.FlowBoostStepsRemaining--;
    if (E.FlowDrainStepsRemaining > 0)  E.FlowDrainStepsRemaining--;
}

// ── Anchor power ───────────────────────────────────────────────────────────
void UCosmicSimulationSubsystem::ProcessAnchorPower(
    int32 StepNumber,
    USettlementManager* SM)
{
    (void)StepNumber; // ADDED
    (void)SM;         // ADDED

    for (const FAnchorRecord& A : AnchorRecords)
    {
        if (!A.BoundEntityTag.IsValid())
        {
            continue;
        }

        // Power = PowerPerSimDay * (avgPurposeNeed / 100.0f)
        // PurposeNeed is the colonist Purpose need (0-100).
        // Phase 3B: query actual Purpose need average from SettlementManager.
        // Stub: use 0.5 faith factor (50% purpose) until need query is wired.
        const float FaithFactor = 0.5f;
        const float PowerAdded = A.PowerPerSimDay * FaithFactor;

        AddPowerToSource(A.BoundEntityTag, PowerAdded, &FCosmicEntityState::AnchorPower);
    }
}

// ── Emergence transitions ──────────────────────────────────────────────────
void UCosmicSimulationSubsystem::CheckEmergenceTransitions(
    int32 StepNumber,
    UWorld* World)
{
    for (FCosmicEntityState& E : EntityStates)
    {
        if (E.TotalPower >= E.EmergenceThreshold &&
            E.Status == ECosmicEntityStatus::Dormant)
        {
            E.Status = ECosmicEntityStatus::Emerging;
            UE_LOG(LogTemp, Log,
                TEXT("[CosmicSim] Entity %s EMERGING (power=%.1f)"),
                *E.EntityTag.ToString(), E.TotalPower);
        }
        else if (E.TotalPower >= E.EmergenceThreshold &&
            E.Status == ECosmicEntityStatus::Emerging)
        {
            E.Status = ECosmicEntityStatus::Active;
            UE_LOG(LogTemp, Log,
                TEXT("[CosmicSim] Entity %s now ACTIVE -- dispatching interventions."),
                *E.EntityTag.ToString());
        }
        else if (E.Status == ECosmicEntityStatus::Active &&
            E.TotalPower < E.FadeThreshold)
        {
            E.Status = ECosmicEntityStatus::Fading;
            UE_LOG(LogTemp, Log,
                TEXT("[CosmicSim] Entity %s FADING (power=%.1f)"),
                *E.EntityTag.ToString(), E.TotalPower);
        }
        else if (E.Status == ECosmicEntityStatus::Fading &&
            E.TotalPower <= 0.0f)
        {
            E.Status = ECosmicEntityStatus::Gone;
        }

        // Active entities dispatch economic interventions periodically
        if (E.Status == ECosmicEntityStatus::Active)
        {
            DispatchEconomicIntervention(E, StepNumber, World);
        }
    }
}

void UCosmicSimulationSubsystem::DispatchEconomicIntervention(
    const FCosmicEntityState& Entity,
    int32 StepNumber,
    UWorld* World)
{
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
    if (!Bus)
    {
        return;
    }

    // Determine entity domain type and queue appropriate intervention.
    // Commerce domain entities: boost flow and trust.
    // War/fear domain entities: drain flow and trust.
    const bool bCommerceEntity = Entity.DomainTags.ContainsByPredicate(
        [](const FGameplayTag& T)
        {
            // ADDED
            // String fallback retained as allowed stub behavior until full tag tree is finalized.
            return T.ToString().Contains(TEXT("Commerce"));
        });

    const bool bWarEntity = Entity.DomainTags.ContainsByPredicate(
        [](const FGameplayTag& T)
        {
            return T.ToString().Contains(TEXT("War"));
        });

    // Only dispatch interventions every 250 steps to avoid flooding
    if (StepNumber % 250 != 0)
    {
        return;
    }

    if (bCommerceEntity && ES)
    {
        // MODIFIED
        // Real project GetAllCurrencies() returns TArray<FCurrencyDefinition>.
        for (const FCurrencyDefinition& Currency : ES->GetAllCurrencies())
        {
            if (Currency.CurrencyTag.IsValid())
            {
                ES->QueueTrustDelta(Currency.CurrencyTag, ETrustComponent::CosmicFavor, +0.2f);
            }
        }

        // Queue flow boost via EventBus
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.CosmicIntervention")));
        E.SettlementID = -1;
        E.AgentID = 50;              // MODIFIED
        E.StepNumber = StepNumber;      // ADDED
        E.Payload = FGameplayTag();  // ADDED
        E.FloatPayload = 1.5f;            // flow multiplier
        Bus->Dispatch(E);
    }
    else if (bWarEntity && ES)
    {
        // MODIFIED
        // Real project GetAllCurrencies() returns TArray<FCurrencyDefinition>.
        for (const FCurrencyDefinition& Currency : ES->GetAllCurrencies())
        {
            if (Currency.CurrencyTag.IsValid())
            {
                ES->QueueTrustDelta(Currency.CurrencyTag, ETrustComponent::GovernmentCredibility, -0.3f);
            }
        }

        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.CosmicIntervention")));
        E.SettlementID = -1;
        E.AgentID = 50;              // MODIFIED
        E.StepNumber = StepNumber;      // ADDED
        E.Payload = FGameplayTag();  // ADDED
        E.FloatPayload = 0.3f;            // flow drain multiplier
        Bus->Dispatch(E);
    }
}

// ── Narrative pressure ─────────────────────────────────────────────────────
void UCosmicSimulationSubsystem::UpdateNarrativePressure(
    int32 StepNumber,
    UWorld* World,
    const TArray<FSimulationEvent>& RecentEvents)
{
    (void)StepNumber; // ADDED

    for (const FSimulationEvent& E : RecentEvents)
    {
        const FString Tag = E.EventType.ToString();
        if (Tag.Contains(TEXT("War")))            NarrativePressure.War += 2.0f;
        if (Tag.Contains(TEXT("CaravanArrived"))) NarrativePressure.Commerce += 1.0f;
        if (Tag.Contains(TEXT("Cosmic")))         NarrativePressure.CosmicUnstable += 3.0f;
        if (Tag.Contains(TEXT("Refugee")))        NarrativePressure.Fear += 2.0f;
    }

    // Natural decay of pressure each step
    auto Decay = [](float& V)
        {
            V = FMath::Max(0.0f, V - 0.5f);
        };

    Decay(NarrativePressure.War);
    Decay(NarrativePressure.Commerce);
    Decay(NarrativePressure.CosmicUnstable);
    Decay(NarrativePressure.Fear);

    // Fire beats when threshold crossed
    TryFireNarrativeBeat(TEXT("Age of Commerce stirring"), NarrativePressure.Commerce, World);
    TryFireNarrativeBeat(TEXT("Age of War descends"), NarrativePressure.War, World);
    TryFireNarrativeBeat(TEXT("Cosmic instability rising"), NarrativePressure.CosmicUnstable, World);
    TryFireNarrativeBeat(TEXT("Fear spreads across the land"), NarrativePressure.Fear, World);
}

void UCosmicSimulationSubsystem::TryFireNarrativeBeat(
    const FString& BeatDescription,
    float& Accumulator,
    UWorld* World) const
{
    if (Accumulator < FNarrativePressureState::Threshold)
    {
        return;
    }

    Accumulator -= FNarrativePressureState::Threshold;

    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (Bus)
    {
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.NarrativeBeat")));
        E.SettlementID = -1;
        E.AgentID = -1;              // ADDED
        E.StepNumber = 0;               // ADDED
        E.Payload = FGameplayTag();  // ADDED
        E.FloatPayload = 0.0f;            // ADDED
        Bus->Dispatch(E);
    }

    UE_LOG(LogTemp, Log, TEXT("[CosmicSim] Narrative beat fired: %s"), *BeatDescription);
}

// ── Queries ────────────────────────────────────────────────────────────────
bool UCosmicSimulationSubsystem::IsEntityActive(FGameplayTag EntityTag) const
{
    for (const FCosmicEntityState& E : EntityStates)
    {
        if (E.EntityTag == EntityTag)
        {
            return E.Status == ECosmicEntityStatus::Active;
        }
    }

    return false;
}

float UCosmicSimulationSubsystem::GetEntityTotalPower(FGameplayTag EntityTag) const
{
    for (const FCosmicEntityState& E : EntityStates)
    {
        if (E.EntityTag == EntityTag)
        {
            return E.TotalPower;
        }
    }

    return 0.0f;
}

const FCosmicEntityState* UCosmicSimulationSubsystem::GetEntityState(
    FGameplayTag EntityTag) const
{
    for (const FCosmicEntityState& E : EntityStates)
    {
        if (E.EntityTag == EntityTag)
        {
            return &E;
        }
    }

    return nullptr;
}

// ── Save / Load ───────────────────────────────────────────────────────────
FCosmicSaveState UCosmicSimulationSubsystem::GetCosmicSaveState() const
{
    FCosmicSaveState S;
    S.EntityStates = EntityStates;
    S.AnchorRecords = AnchorRecords;
    S.NarrativePressure = NarrativePressure;
    S.NextEntityID = NextEntityID;
    S.NextAnchorID = NextAnchorID;
    return S;
}

void UCosmicSimulationSubsystem::RestoreCosmicFromSave(const FCosmicSaveState& S)
{
    EntityStates = S.EntityStates;
    AnchorRecords = S.AnchorRecords;
    NarrativePressure = S.NarrativePressure;
    NextEntityID = S.NextEntityID;
    NextAnchorID = S.NextAnchorID;

    UE_LOG(LogTemp, Log, TEXT("[CosmicSim] Restored %d entities, %d anchors."),
        EntityStates.Num(), AnchorRecords.Num());
}