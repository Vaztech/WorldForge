#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldEconomySimulation.generated.h"

class UWorldForgeRNG;
class USettlementManager;
class UProcessorManager;

/**
 * World economy simulation subsystem.
 *
 * Phase 1 responsibilities are preserved:
 * - world generation economy seeding
 * - prehistory epoch economy simulation
 * - currency / government generation helpers
 *
 * Phase 3A additions:
 * - fixed-step economy tick
 * - runtime market queries
 * - trust delta queue
 * - black market / narrative support
 * - economy save / load state
 */
UCLASS()
class WORLDFORGE_API UWorldEconomySimulation : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Phase 1 (preserved) ──────────────────────────────────────────
    // Called during world generation Step 11.
    // Preserved signature to avoid breaking current callers.
    void GenerateWorldEconomy(
        TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& Tiles,
        TArray<FCurrencyDefinition>& OutCurrencies,
        UWorldForgeRNG* RNG);

    // Called during prehistory simulation.
    void SimulateEconomicEpoch(
        EPreHistoryEpoch Epoch,
        int32 EpochYears,
        TArray<FCivilizationData>& Civs,
        TArray<FHistoryEventData>& OutHistory);

    // Existing Phase 1 / Phase 2 accessors preserved.
    const TArray<FCurrencyDefinition>& GetAllCurrencies() const { return AllCurrenciesArray; }
    const TArray<FGovernmentEconomicProfile>& GetAllGovernments() const { return AllGovernmentsArray; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Economy")
    float GetExchangeRate(
        FGameplayTag FromCurrencyTag,
        FGameplayTag ToCurrencyTag) const;

    // ── Phase 3A: main tick entry point ──────────────────────────────
    // Called by UEconomicProcessor (Processor 9) each fixed step.
    void TickWorldEconomy(float FixedDeltaTime, int32 PlayerSettlementID);

    // Queue a trust delta. NEVER applies immediately.
    // Applied internally at the end of TickWorldEconomy().
    void QueueTrustDelta(
        FGameplayTag CurrencyTag,
        ETrustComponent Component,
        float Delta);

    // ── Phase 3A: runtime queries ────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Economy")
    float GetItemPrice(
        FGameplayTag ItemTag,
        int32 SettlementID,
        bool bIncludeBlackMarket = false) const;

    // C++ only: cannot expose pointer-to-USTRUCT via UFUNCTION
    const FGovernmentEconomicProfile* GetGovernment(int32 CivID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Economy")
    TArray<FMarketPrice> GetMarketReport(int32 SettlementID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Economy")
    bool HasBlackMarket(FGameplayTag ItemTag, int32 SettlementID) const;

    // ADDED
    // Section C hard prerequisite.
    // Called on caravan arrival after stockpile delivery has already occurred.
    // Recomputes destination supply pressure deterministically and logs currency exchange.
    // Does NOT modify prices immediately.
    void ProcessCaravanTransaction(
        int32 BuyerSettlementID,
        int32 SellerSettlementID,
        int32 CaravanID);

    // ── Phase 3B event notifications (stubs in 3A) ──────────────────
    void NotifyWarStarted(int32 AttackerCivID, int32 DefenderCivID);
    void NotifyWarEnded(int32 WinnerCivID, int32 LoserCivID);
    void NotifyPlagueEvent(int32 AffectedSettlementID, float Severity);
    void NotifyDisasterEvent(int32 TileID, FGameplayTag DisasterType);
    void NotifyGovernmentChange(int32 CivID, FGameplayTag NewGovernmentType);
    void NotifyRouteOpened(int32 RouteID);
    void NotifyRouteClosed(int32 RouteID, const FString& Reason);
    void NotifyCivCollapse(int32 CollapsingCivID);

    // ── Narrative reader drain (called by future Commander's Desk) ──
    TArray<FString> DrainNarrativeReports();

    // ── Save / Load ──────────────────────────────────────────────────
    FEconomySaveState GetEconomySaveState() const;
    void RestoreEconomyFromSave(const FEconomySaveState& SaveState);

    int32 SetDiplomaticModForSettlementPairs(
        const TSet<int32>& FromSettlementIDs,
        const TSet<int32>& ToSettlementIDs,
        float DiplomaticMod
    );

private:
    // ── Existing Phase 1 helpers (preserved) ─────────────────────────
    FGameplayTag DeriveGovernmentType(
        const FCultureData& DNA,
        FGameplayTag RaceTag,
        UWorldForgeRNG* RNG) const;
    FCurrencyDefinition GenerateCurrencyForCiv(
        const FCivilizationData& Civ,
        const FGameplayTag& GovernmentType,
        UWorldForgeRNG* RNG) const;
    FGovernmentEconomicProfile CreateGovernmentProfile(
        const FGameplayTag& GovernmentType,
        UWorldForgeRNG* RNG) const;
    float GetBaseAnchorValue(const FCivilizationData& Civ, const TArray<FWorldTileData>& Tiles) const;

    // ── Phase 3A economic force update steps ─────────────────────────
    void UpdateProduction(
        float DeltaTime,
        int32 SettlementID,
        USettlementManager* SettlementManager,
        UProcessorManager* ProcessorManager);

    void UpdateConsumption(
        float DeltaTime,
        int32 SettlementID,
        USettlementManager* SettlementManager);

    void UpdateFlow(float DeltaTime, int32 SettlementID);
    void UpdatePriceDiscovery(float DeltaTime, int32 SettlementID);
    void UpdateCurrencyTrust(float DeltaTime);

    // ── Phase 3A price fixing / black market ─────────────────────────
    void EvaluatePriceFixPolicies(int32 SettlementID, int32 OwnerCivID);
    void UpdateBlackMarkets(int32 SettlementID);
    float ComputeBlackMarketGap(
        const FMarketPrice& Price,
        const FPriceFixPolicy& Policy) const;

    // ── Phase 3A collapse / narrative ────────────────────────────────
    void CheckCurrencyCollapse();
    void ScanNarrativePatterns(int32 SettlementID, int32 CurrentStep);
    bool IsNarrativeCooledDown(FGameplayTag PatternTag, int32 CurrentStep) const;
    void TryAddNarrativeReport(
        FGameplayTag PatternTag,
        const FString& Report,
        int32 CurrentStep);

    // ── Internal helpers ─────────────────────────────────────────────
    FMarketPrice& GetOrCreatePrice(int32 SettlementID, FGameplayTag ItemTag);
    void ApplyPendingTrustDeltas();
    void RegisterDefaultCommodities();

    void RebuildLookupMapsFromArrays();
    void RebuildArraysFromLookupMaps();

private:
    // ── Preserved legacy arrays for back-compat with existing callers ─
    TArray<FCurrencyDefinition> AllCurrenciesArray;
    TArray<FGovernmentEconomicProfile> AllGovernmentsArray;

    // ── Phase 3A runtime lookup state ────────────────────────────────
    TMap<FGameplayTag, FCurrencyDefinition> AllCurrencies;
    TMap<int32, FGovernmentEconomicProfile> CivGovernments;
    TMap<int32, TArray<FMarketPrice>> SettlementPrices;
    TMap<int32, FTradeFlowState> TradeRoutes;
    TMap<FGameplayTag, FCurrencyTrustState> CurrencyTrust;

    // Key: SettlementID -> ItemTag -> last N prices
    TMap<int32, TMap<FGameplayTag, TArray<float>>> PriceHistory;

    // Pending trust deltas queued during current tick
    TArray<FCurrencyTrustDelta> PendingTrustDeltas;

    // Narrative report state
    TArray<FString> PendingNarrativeReports;
    TMap<FGameplayTag, int32> LastNarrativeReportStep;

    static constexpr int32 NarrativeCooldownSteps = 500;
    static constexpr int32 MaxNarrativeReports = 5;

    // Default tracked commodity tags for Phase 3A
    TArray<FGameplayTag> TrackedItems;
};