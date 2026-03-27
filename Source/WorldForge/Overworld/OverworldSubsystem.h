#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "OverworldSubsystem.generated.h"

// ─────────────────────────────────────────────────────────────────────────
// UOverworldSubsystem -- Phase 3B
// Manages caravans, outposts, and trade routes.
// Caravans step toward their destination each sim step.
// On arrival: calls UWorldEconomySimulation::ProcessCaravanTransaction().
// Outpost supply lines: tracked via FOutpostData.StepsWithoutSupply.
//   150 steps = degrade (FlowRateMod *= 0.5), 300 steps = abandon.
// ─────────────────────────────────────────────────────────────────────────
UCLASS()
class WORLDFORGE_API UOverworldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Called by simulation-step bridge each sim step.
    void OnSimStep(int32 StepNumber, int32 SettlementID,
        TArray<FAgentData>& Agents, UWorld* World);

    // ADDED -- BF-06 runtime hook
    // Bridge for USimulationTimeManager::OnSimulationStep dynamic multicast.
    UFUNCTION()
    void HandleSimulationStep(int32 StepNumber);

    // ── Caravan management ─────────────────────────────────────────────
    // Dispatch a new outbound caravan from OriginSettlementID.
    // TravelSteps = how many sim steps to reach destination.
    // KEPT EXISTING -- not BlueprintCallable because TArray<FCargoItem>
    // is not currently a Blueprint-supported parameter type in this project.
    int32 DispatchCaravan(int32 OriginSettlementID,
        int32 DestSettlementID,
        const TArray<FCargoItem>& Cargo,
        float PaymentAmount,
        FGameplayTag PaymentCurrencyTag,
        int32 TravelSteps);

    // ── Outpost management ─────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Overworld")
    int32 BuildOutpost(int32 OwnerSettlementID,
        FIntPoint GridCell,
        EOutpostType OutpostType);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Overworld")
    void AbandonOutpost(int32 OutpostID);

    const TArray<FOutpostData>& GetActiveOutposts() const { return ActiveOutposts; }
    const TArray<FCaravanData>& GetActiveCaravans() const { return ActiveCaravans; }

    // ── Trade route modifiers (used by UpdateFlow in Phase 3B) ─────────
    // Returns combined InfrastructureMod for routes touching this settlement.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Overworld")
    float GetInfrastructureMod(int32 OriginID, int32 DestID) const;

    // ── Save / Load ────────────────────────────────────────────────────
    FOverworldSaveState GetOverworldSaveState() const;
    void RestoreOverworldFromSave(const FOverworldSaveState& S);

private:
    TArray<FCaravanData> ActiveCaravans;
    TArray<FOutpostData> ActiveOutposts;
    int32 NextCaravanID = 0;
    int32 NextOutpostID = 0;

    static constexpr int32 OutpostDegradesAt = 150;
    static constexpr int32 OutpostAbandonsAt = 300;

    void StepCaravans(int32 StepNumber, UWorld* World);
    void ProcessCaravanArrival(const FCaravanData& Caravan, UWorld* World) const;
    void StepOutpostSupplyLines(int32 StepNumber, UWorld* World);
    bool IsOutpostSupplied(const FOutpostData& Outpost, int32 StepNumber) const;
};