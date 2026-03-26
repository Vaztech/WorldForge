#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "NavalSystem.generated.h"

// ─────────────────────────────────────────────────────────────────────────
// UNavalSystem -- Phase 3B
// Manages ships and sea trade routes.
// Ships step toward their destination each sim step, applying wind modifiers.
// Sea routes carry 3x cargo vs. overland.
// Blockade: sets DiplomaticMod = 0.0 on UWorldEconomySimulation.TradeRoutes
// for all routes to/from the blockaded port.
// ─────────────────────────────────────────────────────────────────────────
UCLASS()
class WORLDFORGE_API UNavalSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Called by ProcessorManager each sim step.
    void OnSimStep(int32 StepNumber, int32 SettlementID,
        TArray<FAgentData>& Agents, UWorld* World);

    // ── Ship dispatch ─────────────────────────────────────────────────
    int32 DispatchShip(int32 OwnerSettlementID,
        int32 DestSettlementID,
        EShipType ShipType,
        const TArray<FCargoItem>& Cargo,
        const FVector2D& WindDirection);

    // ── Blockade ──────────────────────────────────────────────────────
    void SetBlockade(int32 PortSettlementID, bool bBlockade, UWorld* World);
    bool IsBlockaded(int32 PortSettlementID) const;

    // ── Queries ────────────────────────────────────────────────────────
    const TArray<FShipData>& GetActiveShips() const { return ActiveShips; }

    // Compute effective speed for a ship given current wind.
    // WindDir: normalized 2D wind direction vector.
    // RouteDir: normalized 2D direction from origin to destination.
    float ComputeShipSpeed(const FShipData& Ship,
        const FVector2D& WindDir,
        const FVector2D& RouteDir) const;

    // Returns cargo multiplier for ship type.
    static int32 GetCargoCap(EShipType Type);

    // Returns base speed for ship type.
    static float GetBaseSpeed(EShipType Type);

    // ── Save / Load ────────────────────────────────────────────────────
    FNavalSaveState GetNavalSaveState() const;
    void RestoreNavalFromSave(const FNavalSaveState& S);

private:
    TArray<FShipData> ActiveShips;
    TSet<int32>       BlockadedPorts; // SettlementIDs
    int32             NextShipID = 0;

    // Current wind direction (updated from SkyScape each step if available).
    FVector2D CurrentWindDirection = FVector2D(1.0f, 0.0f);

    void StepShips(int32 StepNumber, UWorld* World);
    void ProcessShipArrival(const FShipData& Ship, UWorld* World) const;
    FVector2D GetRouteDirection(int32 OriginID, int32 DestID) const;
};