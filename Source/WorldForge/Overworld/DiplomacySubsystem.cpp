#include "DiplomacySubsystem.h"
#include "WorldEconomySimulation.h"
#include "WorldMapSubsystem.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Engine/World.h"

static constexpr float HighHostilityThreshold = 70.0f;

void UDiplomacySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[DiplomacySubsystem] Initialized."));
}

void UDiplomacySubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UDiplomacySubsystem::OnSimStep(int32 StepNumber, int32 SettlementID,
    TArray<FAgentData>& Agents, UWorld* World)
{
    (void)SettlementID; // ADDED
    (void)Agents;       // ADDED

    CheckAndExpireTreaties(StepNumber, World);
}

// ── Helpers ────────────────────────────────────────────────────────────────
/*static*/ int64 UDiplomacySubsystem::MakePairKey(int32 CivA, int32 CivB)
{
    const int32 Lo = FMath::Min(CivA, CivB);
    const int32 Hi = FMath::Max(CivA, CivB);
    return static_cast<int64>(Lo) * 100000LL + static_cast<int64>(Hi);
}

float UDiplomacySubsystem::GetRelationship(int32 CivA, int32 CivB) const
{
    if (const float* V = RelationshipScores.Find(MakePairKey(CivA, CivB)))
    {
        return *V;
    }

    return 0.0f;
}

void UDiplomacySubsystem::ModifyRelationship(int32 CivA, int32 CivB, float Delta)
{
    float& Score = RelationshipScores.FindOrAdd(MakePairKey(CivA, CivB));
    Score = FMath::Clamp(Score + Delta, -100.0f, 100.0f);
}

bool UDiplomacySubsystem::IsAtWar(int32 CivA, int32 CivB) const
{
    return GetRelationship(CivA, CivB) <= -80.0f;
}

bool UDiplomacySubsystem::IsEmbargoActive(int32 CivA, int32 CivB) const
{
    return EmbargoedPairs.Contains(MakePairKey(CivA, CivB));
}

bool UDiplomacySubsystem::WillAcceptPositiveAction(int32 TargetCivID) const
{
    UWorld* World = GetWorld();
    UWorldMapSubsystem* WMS = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    if (!WMS)
    {
        return true;
    }

    const TArray<FCivilizationData>& Civs = WMS->GetCivilizations();
    for (const FCivilizationData& Civ : Civs)
    {
        if (Civ.CivID != TargetCivID)
        {
            continue;
        }

        // MODIFIED
        // Real project uses FCultureData fields, not a CulturalDNA float array.
        return Civ.CulturalDNA.OutgroupHostility <= HighHostilityThreshold;
    }

    return true;
}

FGameplayTag UDiplomacySubsystem::GetCivCurrencyTag(int32 CivID, UWorld* World) const
{
    UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
    if (!ES)
    {
        return FGameplayTag();
    }

    // MODIFIED
    // Real project GetAllCurrencies() returns TArray<FCurrencyDefinition>, not TMap.
    const TArray<FCurrencyDefinition>& AllCurrencies = ES->GetAllCurrencies();
    for (const FCurrencyDefinition& Currency : AllCurrencies)
    {
        if (Currency.IssuingCivID == CivID)
        {
            return Currency.CurrencyTag;
        }
    }

    return FGameplayTag();
}

void UDiplomacySubsystem::SetEmbargo(int32 CivA, int32 CivB, bool bActive, UWorld* World)
{
    const int64 Key = MakePairKey(CivA, CivB);

    if (bActive)
    {
        EmbargoedPairs.Add(Key);
        SetTradeRouteFlowMod(CivA, CivB, 0.0f, World);
    }
    else
    {
        EmbargoedPairs.Remove(Key);
        SetTradeRouteFlowMod(CivA, CivB, 1.0f, World);
    }
}

void UDiplomacySubsystem::SetTradeRouteFlowMod(int32 CivA, int32 CivB, float Mod, UWorld* World) const
{
    (void)World; // ADDED

    // ADDED
    // Phase 3B stub dependency:
    // this only logs for now. Real mutation of FTradeFlowState.DiplomaticMod
    // is deferred until trade routes are mutable/populated.
    UE_LOG(LogTemp, Log,
        TEXT("[Diplomacy] SetTradeRouteFlowMod stub: %d <-> %d = %.2f"),
        CivA, CivB, Mod);
}

void UDiplomacySubsystem::CheckAndExpireTreaties(int32 StepNumber, UWorld* World)
{
    TArray<FDiplomaticTreaty> RemainingTreaties;

    for (const FDiplomaticTreaty& Treaty : ActiveTreaties)
    {
        if (!Treaty.bActive)
        {
            continue;
        }

        if (Treaty.ExpiryStep >= 0 && StepNumber >= Treaty.ExpiryStep)
        {
            // ADDED
            // Expiry behavior is intentionally minimal and targeted.
            // Only route-modifying treaties restore trade flow here.
            if (Treaty.TreatyType == EDiplomaticActionType::TradeAgreement)
            {
                SetTradeRouteFlowMod(Treaty.CivA, Treaty.CivB, 1.0f, World);
            }

            continue;
        }

        RemainingTreaties.Add(Treaty);
    }

    ActiveTreaties = RemainingTreaties;
}

// ── Diplomatic Actions ─────────────────────────────────────────────────────
void UDiplomacySubsystem::DeclareWar(int32 AttackerCivID, int32 DefenderCivID, UWorld* World)
{
    ModifyRelationship(AttackerCivID, DefenderCivID, -80.0f);
    SetEmbargo(AttackerCivID, DefenderCivID, true, World);

    UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
    if (ES)
    {
        const FGameplayTag AttackerCur = GetCivCurrencyTag(AttackerCivID, World);
        const FGameplayTag DefenderCur = GetCivCurrencyTag(DefenderCivID, World);

        if (AttackerCur.IsValid())
        {
            ES->QueueTrustDelta(AttackerCur, ETrustComponent::TradeReputation, -0.2f);
            ES->QueueTrustDelta(AttackerCur, ETrustComponent::MilitaryStability, +0.1f);
        }

        if (DefenderCur.IsValid())
        {
            ES->QueueTrustDelta(DefenderCur, ETrustComponent::TradeReputation, -0.2f);
            ES->QueueTrustDelta(DefenderCur, ETrustComponent::MilitaryStability, -0.2f);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] War declared: %d -> %d"),
        AttackerCivID, DefenderCivID);
}

void UDiplomacySubsystem::ProposePeace(int32 CivA, int32 CivB, UWorld* World)
{
    ModifyRelationship(CivA, CivB, +30.0f);
    SetEmbargo(CivA, CivB, false, World);

    // Resume trade at 50% capacity
    FDiplomaticTreaty T;
    T.CivA = CivA;
    T.CivB = CivB;
    T.TreatyType = EDiplomaticActionType::ProposePeace;
    T.ExpiryStep = -1;
    T.FlowRateMod = 0.5f;
    T.bActive = true;
    ActiveTreaties.Add(T);

    SetTradeRouteFlowMod(CivA, CivB, 0.5f, World);
}

void UDiplomacySubsystem::TradeAgreement(int32 CivA, int32 CivB, int32 DurationSteps, UWorld* World)
{
    if (!WillAcceptPositiveAction(CivB))
    {
        return;
    }

    ModifyRelationship(CivA, CivB, +20.0f);

    FDiplomaticTreaty T;
    T.CivA = CivA;
    T.CivB = CivB;
    T.TreatyType = EDiplomaticActionType::TradeAgreement;
    T.ExpiryStep = DurationSteps; // caller provides current step + duration
    T.FlowRateMod = 1.3f;
    T.bActive = true;
    ActiveTreaties.Add(T);

    SetTradeRouteFlowMod(CivA, CivB, 1.3f, World);

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Trade agreement: %d <-> %d (x1.3 flow, %d steps)"),
        CivA, CivB, DurationSteps);
}

void UDiplomacySubsystem::CurrencyPegging(int32 CivA, int32 CivB, UWorld* World)
{
    if (!WillAcceptPositiveAction(CivB))
    {
        return;
    }

    ModifyRelationship(CivA, CivB, +15.0f);

    const FGameplayTag CurA = GetCivCurrencyTag(CivA, World);
    const FGameplayTag CurB = GetCivCurrencyTag(CivB, World);
    if (CurA.IsValid() && CurB.IsValid())
    {
        PeggedCurrencyPairs.Add(MakePairKey(CivA, CivB), CurA);
    }

    // Phase 3B: stored only, not yet consumed by economy trust propagation.
    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Currency pegging: Civ %d <-> Civ %d"), CivA, CivB);
}

void UDiplomacySubsystem::EconomicAid(
    int32 DonorCivID, int32 ReceiverCivID, float GoldAmount, UWorld* World)
{
    (void)GoldAmount; // ADDED

    if (!WillAcceptPositiveAction(ReceiverCivID))
    {
        return;
    }

    ModifyRelationship(DonorCivID, ReceiverCivID, +10.0f);

    UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
    if (ES)
    {
        const FGameplayTag RecvCur = GetCivCurrencyTag(ReceiverCivID, World);
        if (RecvCur.IsValid())
        {
            ES->QueueTrustDelta(RecvCur, ETrustComponent::BackingStrength, +0.1f);
        }
    }
}

void UDiplomacySubsystem::CurrencyWar(int32 AttackerCivID, int32 TargetCivID, UWorld* World)
{
    ModifyRelationship(AttackerCivID, TargetCivID, -40.0f);

    UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
    if (ES)
    {
        const FGameplayTag TargetCur = GetCivCurrencyTag(TargetCivID, World);
        if (TargetCur.IsValid())
        {
            ES->QueueTrustDelta(TargetCur, ETrustComponent::CosmicFavor, -0.15f);
            ES->QueueTrustDelta(TargetCur, ETrustComponent::MilitaryStability, -0.10f);
            ES->QueueTrustDelta(TargetCur, ETrustComponent::GovernmentCredibility, -0.05f);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Currency war: %d -> %d"), AttackerCivID, TargetCivID);
}

void UDiplomacySubsystem::TradeEmbargo(int32 IssuingCivID, int32 TargetCivID,
    bool bLift, UWorld* World)
{
    SetEmbargo(IssuingCivID, TargetCivID, !bLift, World);
    ModifyRelationship(IssuingCivID, TargetCivID, bLift ? +5.0f : -20.0f);

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Embargo %s: %d <-> %d"),
        bLift ? TEXT("lifted") : TEXT("declared"),
        IssuingCivID,
        TargetCivID);
}

void UDiplomacySubsystem::SpyMission(int32 SpyingCivID, int32 TargetCivID,
    int32 PlayerSettlementID, UWorld* World)
{
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (Bus)
    {
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.SpyDataRevealed")));
        E.SettlementID = PlayerSettlementID;
        E.AgentID = TargetCivID;      // MODIFIED
        E.StepNumber = 0;             // ADDED
        E.Payload = FGameplayTag();   // ADDED
        E.FloatPayload = 0.0f;        // ADDED

        Bus->Dispatch(E);
    }

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Spy mission: Civ %d spied on Civ %d"),
        SpyingCivID, TargetCivID);
}

void UDiplomacySubsystem::CulturalExchange(int32 CivA, int32 CivB, UWorld* World)
{
    if (!WillAcceptPositiveAction(CivB))
    {
        return;
    }

    ModifyRelationship(CivA, CivB, +10.0f);

    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (Bus)
    {
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.TradeBoom")));
        E.SettlementID = -1;
        E.AgentID = CivB;             // MODIFIED
        E.StepNumber = 0;             // ADDED
        E.Payload = FGameplayTag();   // ADDED
        E.FloatPayload = 0.0f;        // ADDED

        Bus->Dispatch(E);
    }

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Cultural exchange: Civ %d <-> Civ %d"), CivA, CivB);
}

void UDiplomacySubsystem::MilitaryAlliance(int32 CivA, int32 CivB, UWorld* World)
{
    if (!WillAcceptPositiveAction(CivB))
    {
        return;
    }

    ModifyRelationship(CivA, CivB, +25.0f);

    UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
    if (ES)
    {
        for (int32 CivID : { CivA, CivB })
        {
            const FGameplayTag Cur = GetCivCurrencyTag(CivID, World);
            if (Cur.IsValid())
            {
                ES->QueueTrustDelta(Cur, ETrustComponent::MilitaryStability, +0.1f);
            }
        }
    }

    FDiplomaticTreaty T;
    T.CivA = CivA;
    T.CivB = CivB;
    T.TreatyType = EDiplomaticActionType::MilitaryAlliance;
    T.ExpiryStep = -1;
    T.FlowRateMod = 1.0f;
    T.bActive = true;
    ActiveTreaties.Add(T);
}

void UDiplomacySubsystem::Vassalize(int32 OverlordCivID, int32 VassalCivID, UWorld* World)
{
    (void)World; // ADDED

    ModifyRelationship(OverlordCivID, VassalCivID, -30.0f); // resentment

    // Phase 3B partial: royalty redirect is future behavior.
    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Civ %d vassalized by Civ %d"),
        VassalCivID, OverlordCivID);
}

void UDiplomacySubsystem::OpenBorders(int32 CivA, int32 CivB, UWorld* World)
{
    if (!WillAcceptPositiveAction(CivB))
    {
        return;
    }

    ModifyRelationship(CivA, CivB, +10.0f);

    FDiplomaticTreaty T;
    T.CivA = CivA;
    T.CivB = CivB;
    T.TreatyType = EDiplomaticActionType::OpenBorders;
    T.ExpiryStep = -1;
    T.FlowRateMod = 1.0f;
    T.bActive = true;
    ActiveTreaties.Add(T);

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Open borders: Civ %d <-> Civ %d"), CivA, CivB);
}

void UDiplomacySubsystem::RequestTribute(int32 RequesterCivID, int32 TargetCivID,
    float Amount, UWorld* World)
{
    (void)World; // ADDED

    const bool bAccepted = GetRelationship(RequesterCivID, TargetCivID) >= 20.0f;
    if (!bAccepted)
    {
        ModifyRelationship(RequesterCivID, TargetCivID, -30.0f);
        UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Tribute refused by Civ %d"), TargetCivID);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Tribute %.0f paid by Civ %d to Civ %d"),
            Amount, TargetCivID, RequesterCivID);
    }
}

void UDiplomacySubsystem::HonorDebt(int32 DebtorCivID, int32 CreditorCivID,
    bool bHonored, UWorld* World)
{
    if (bHonored)
    {
        ModifyRelationship(DebtorCivID, CreditorCivID, +20.0f);

        UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
        if (ES)
        {
            const FGameplayTag Cur = GetCivCurrencyTag(DebtorCivID, World);
            if (Cur.IsValid())
            {
                ES->QueueTrustDelta(Cur, ETrustComponent::GovernmentCredibility, +0.1f);
            }
        }
    }
    else
    {
        // Oni-specific permanent relationship damage in guide
        ModifyRelationship(DebtorCivID, CreditorCivID, -50.0f);
        UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Debt broken by Civ %d -- permanent damage"),
            DebtorCivID);
    }
}

void UDiplomacySubsystem::MarriageAlliance(int32 CivA, int32 CivB,
    int32 DurationSteps, UWorld* World)
{
    (void)World; // ADDED

    if (!WillAcceptPositiveAction(CivB))
    {
        return;
    }

    ModifyRelationship(CivA, CivB, +20.0f);

    FDiplomaticTreaty T;
    T.CivA = CivA;
    T.CivB = CivB;
    T.TreatyType = EDiplomaticActionType::MarriageAlliance;
    T.ExpiryStep = DurationSteps;
    T.FlowRateMod = 1.0f;
    T.bActive = true;
    ActiveTreaties.Add(T);
}

void UDiplomacySubsystem::JointExpedition(int32 CivA, int32 CivB,
    const TArray<FCargoItem>& SharedCargo, UWorld* World)
{
    (void)SharedCargo; // ADDED
    (void)World;       // ADDED

    if (!WillAcceptPositiveAction(CivB))
    {
        return;
    }

    ModifyRelationship(CivA, CivB, +10.0f);

    // Phase 3B partial: cargo split is future behavior in Overworld.
    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] Joint expedition: Civ %d + Civ %d"), CivA, CivB);
}

// ── Save / Load ───────────────────────────────────────────────────────────
FDiplomacySaveState UDiplomacySubsystem::GetDiplomacySaveState() const
{
    FDiplomacySaveState S;
    S.RelationshipScores = RelationshipScores;
    S.ActiveTreaties = ActiveTreaties;

    // MODIFIED
    // Current project still uses EmbargoedCivIDs (TArray<int32>) in save state.
    // Keep simplified serialization for now to avoid a large data-type edit here.
    for (int64 K : EmbargoedPairs)
    {
        S.EmbargoedCivIDs.Add(static_cast<int32>(K));
    }

    S.PeggedCurrencyPairs = PeggedCurrencyPairs;
    return S;
}

void UDiplomacySubsystem::RestoreDiplomacyFromSave(const FDiplomacySaveState& S)
{
    RelationshipScores = S.RelationshipScores;
    ActiveTreaties = S.ActiveTreaties;

    EmbargoedPairs.Empty();
    for (int32 SavedKey : S.EmbargoedCivIDs)
    {
        EmbargoedPairs.Add(static_cast<int64>(SavedKey));
    }

    PeggedCurrencyPairs = S.PeggedCurrencyPairs;

    UE_LOG(LogTemp, Log, TEXT("[Diplomacy] State restored. %d treaties, %d relationships."),
        ActiveTreaties.Num(), RelationshipScores.Num());
}