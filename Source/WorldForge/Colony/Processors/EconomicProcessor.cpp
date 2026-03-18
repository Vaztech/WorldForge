#include "EconomicProcessor.h"

#include "WorldEconomySimulation.h"
#include "SettlementManager.h"
#include "SimulationTimeManager.h"
#include "WorldForgeDataTypes.h"
#include "Engine/World.h"

// ====================================================================
// ECONOMIC PROCESSOR
//
// Runs once per simulation step per settlement.
// Responsible for:
// - validating the settlement has a usable currency
// - ticking the world economy simulation
// - printing a periodic debug market report so price movement can be
//   verified during runtime and during save/load tests
//
// NOTE:
// This processor does not own pricing logic directly.
// All economic behavior is delegated to UWorldEconomySimulation.
// ====================================================================
void UEconomicProcessor::ProcessStep(
    int32 StepNumber,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World)
{
    // Agents are unused here, but kept for interface consistency.
    (void)Agents;

    // ------------------------------------------------------------
    // Safety: world must exist
    // ------------------------------------------------------------
    if (!World)
    {
        return;
    }

    // ------------------------------------------------------------
    // Validate the target settlement and ensure it has a currency.
    //
    // If PlayerCurrencyTag is invalid, skip the economic tick because
    // downstream economy systems assume a valid currency exists.
    // ------------------------------------------------------------
    USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
    if (SettlementManager)
    {
        const FSettlementState* Settlement = SettlementManager->GetSettlement(SettlementID);
        if (!Settlement || !Settlement->PlayerCurrencyTag.IsValid())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[EconomicProcessor] Settlement %d has no PlayerCurrencyTag -- skipping"),
                SettlementID);

            return;
        }
    }

    // ------------------------------------------------------------
    // Resolve the main economy simulation subsystem.
    // ------------------------------------------------------------
    UWorldEconomySimulation* EconomySimulation =
        World->GetSubsystem<UWorldEconomySimulation>();
    if (!EconomySimulation)
    {
        return;
    }

    // ------------------------------------------------------------
    // Pull the fixed simulation delta from SimulationTimeManager so
    // economy tick speed tracks the actual sim step rate.
    // ------------------------------------------------------------
    USimulationTimeManager* SimulationTimeManager =
        World->GetSubsystem<USimulationTimeManager>();

    const float FixedDeltaTime =
        SimulationTimeManager ? SimulationTimeManager->GetStepInterval() : 0.2f;

    // ------------------------------------------------------------
    // Core economy update.
    //
    // TickWorldEconomy internally handles:
    // - production pressure
    // - consumption pressure
    // - flow
    // - price discovery
    // - currency trust
    // - black market/policy checks
    // ------------------------------------------------------------
    EconomySimulation->TickWorldEconomy(FixedDeltaTime, SettlementID);

    // ------------------------------------------------------------
    // Debug: emit a readable market report every 10 steps.
    //
    // This helps verify:
    // - prices move away from 1.0
    // - black-market fields activate when applicable
    // - save/load restores economic state correctly
    // ------------------------------------------------------------
    if (StepNumber % 10 == 0)
    {
        const TArray<FMarketPrice> MarketPrices = EconomySimulation->GetMarketReport(SettlementID);

        FString Report;
        Report += FString::Printf(
            TEXT("Settlement=%d Entries=%d\n"),
            SettlementID,
            MarketPrices.Num());

        if (MarketPrices.Num() == 0)
        {
            Report += TEXT("No market price entries.");
        }
        else
        {
            for (const FMarketPrice& Price : MarketPrices)
            {
                Report += FString::Printf(
                    TEXT("Item=%s Official=%.2f Equilibrium=%.2f BlackMarket=%.2f Supply=%.2f Demand=%.2f Fixed=%s\n"),
                    *Price.ItemTag.ToString(),
                    Price.OfficialPrice,
                    Price.EquilibriumPrice,
                    Price.BlackMarketPrice,
                    Price.SupplyPressure,
                    Price.DemandPressure,
                    Price.bPriceFixed ? TEXT("true") : TEXT("false"));
            }
        }

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("\n[EconomySim] ===== MARKET REPORT (Step %d) =====\n%s"),
            StepNumber,
            *Report);
    }

    // ------------------------------------------------------------
    // Event bus flushing is owned by ProcessorManager after all
    // processors complete their step, so nothing else is done here.
    // ------------------------------------------------------------
}