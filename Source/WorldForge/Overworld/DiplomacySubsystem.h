#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "DiplomacySubsystem.generated.h"

// ─────────────────────────────────────────────────────────────────────────
// UDiplomacySubsystem -- Phase 3B
// 16 diplomatic actions. Relationship scores -100 to +100 per faction pair.
// All trust effects queue via UWorldEconomySimulation::QueueTrustDelta().
// Flow effects are stubbed through SetTradeRouteFlowMod() logging only.
// EventBus events are dispatched (queued) -- never applied mid-step.
// ─────────────────────────────────────────────────────────────────────────
UCLASS()
class WORLDFORGE_API UDiplomacySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Called by ProcessorManager each step.
    // Diplomacy is mostly action-driven; current tick use is treaty expiry.
    void OnSimStep(int32 StepNumber, int32 SettlementID,
        TArray<FAgentData>& Agents, UWorld* World);

    // ── Relationship queries ───────────────────────────────────────────
    float GetRelationship(int32 CivA, int32 CivB) const;
    void  ModifyRelationship(int32 CivA, int32 CivB, float Delta);
    bool  IsAtWar(int32 CivA, int32 CivB) const;
    bool  IsEmbargoActive(int32 CivA, int32 CivB) const;

    // ── High Outgroup Hostility check ─────────────────────────────────
    // Returns false if the target civ's OutgroupHostility > 70.
    // Positive diplomatic actions should fail if this returns false.
    bool WillAcceptPositiveAction(int32 TargetCivID) const;

    // ── 16 Diplomatic actions ─────────────────────────────────────────
    void DeclareWar(int32 AttackerCivID, int32 DefenderCivID, UWorld* World);
    void ProposePeace(int32 CivA, int32 CivB, UWorld* World);
    void TradeAgreement(int32 CivA, int32 CivB, int32 DurationSteps, UWorld* World);
    void CurrencyPegging(int32 CivA, int32 CivB, UWorld* World);
    void EconomicAid(int32 DonorCivID, int32 ReceiverCivID, float GoldAmount, UWorld* World);
    void CurrencyWar(int32 AttackerCivID, int32 TargetCivID, UWorld* World);
    void TradeEmbargo(int32 IssuingCivID, int32 TargetCivID, bool bLift, UWorld* World);
    void SpyMission(int32 SpyingCivID, int32 TargetCivID, int32 PlayerSettlementID, UWorld* World);
    void CulturalExchange(int32 CivA, int32 CivB, UWorld* World);
    void MilitaryAlliance(int32 CivA, int32 CivB, UWorld* World);
    void Vassalize(int32 OverlordCivID, int32 VassalCivID, UWorld* World);
    void OpenBorders(int32 CivA, int32 CivB, UWorld* World);
    void RequestTribute(int32 RequesterCivID, int32 TargetCivID, float Amount, UWorld* World);
    void HonorDebt(int32 DebtorCivID, int32 CreditorCivID, bool bHonored, UWorld* World);
    void MarriageAlliance(int32 CivA, int32 CivB, int32 DurationSteps, UWorld* World);
    void JointExpedition(int32 CivA, int32 CivB, const TArray<FCargoItem>& SharedCargo, UWorld* World);

    // ── Save / Load ────────────────────────────────────────────────────
    FDiplomacySaveState GetDiplomacySaveState() const;
    void RestoreDiplomacyFromSave(const FDiplomacySaveState& S);

private:
    // Key = CivA * 100000 + CivB (always store with lower CivID as CivA)
    TMap<int64, float> RelationshipScores;
    TArray<FDiplomaticTreaty> ActiveTreaties;
    TSet<int64> EmbargoedPairs;  // same key scheme
    TMap<int64, FGameplayTag> PeggedCurrencyPairs;

    static int64 MakePairKey(int32 CivA, int32 CivB);
    void SetEmbargo(int32 CivA, int32 CivB, bool bActive, UWorld* World);
    void SetTradeRouteFlowMod(int32 CivA, int32 CivB, float Mod, UWorld* World);
    FGameplayTag GetCivCurrencyTag(int32 CivID, UWorld* World) const;
    void CheckAndExpireTreaties(int32 StepNumber, UWorld* World);
};