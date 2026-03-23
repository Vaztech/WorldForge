#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "CivilizationResilienceSystem.generated.h"

// ─────────────────────────────────────────────────────────────────────────
// UCivilizationResilienceSystem -- Phase 3B
// Runs every LivingWorldTickInterval sim steps (same as CultureDriftSystem).
// Checks CivHealthScore each tick. If < CollapseThreshold for
// ConsecutiveCollapseTicksRequired consecutive ticks, triggers collapse.
// Collapse is dispatched via EventBus::Dispatch() -- NEVER applied immediately.
// ─────────────────────────────────────────────────────────────────────────
UCLASS()
class WORLDFORGE_API UCivilizationResilienceSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void OnSimStep(int32 StepNumber, int32 SettlementID,
        TArray<FAgentData>& Agents, UWorld* World); // ADDED

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 LivingWorldTickInterval = 100; // ADDED

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CollapseThreshold = 0.2f; // ADDED

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ConsecutiveCollapseTicksRequired = 3; // ADDED

    float ComputeCivHealth(const FCivilizationData& Civ) const; // ADDED

    FResilienceSaveState GetResilienceSaveState() const; // ADDED
    void RestoreResilienceFromSave(const FResilienceSaveState& S); // ADDED

private:
    TMap<int32, FResilienceRecord> Records; // ADDED

    void ProcessOneCiv(FCivilizationData& Civ,
        int32 StepNumber, UWorld* World); // ADDED

    void TriggerCollapse(FCivilizationData& Civ,
        int32 StepNumber, UWorld* World); // ADDED

    ECivilizationCollapseType DetermineCollapseType(
        const FCivilizationData& Civ) const; // ADDED

    void DispatchCollapseEvent(const FCivilizationData& Civ,
        int32 StepNumber, UWorld* World) const; // ADDED

    void SpawnRefugeeEvent(const FCivilizationData& Civ,
        int32 StepNumber, UWorld* World) const; // ADDED

    float DeriveTerritoryScore(const FCivilizationData& Civ) const; // ADDED
    float DeriveEconomyScore(const FCivilizationData& Civ,
        class UWorldEconomySimulation* EconSim) const; // ADDED
    float DerivePopulationScore(const FCivilizationData& Civ) const; // ADDED
    float DeriveMilitaryScore(const FCivilizationData& Civ) const; // ADDED
};