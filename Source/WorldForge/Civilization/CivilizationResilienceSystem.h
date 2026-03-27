#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "CivilizationResilienceSystem.generated.h"

class UWorldEconomySimulation;

UCLASS()
class WORLDFORGE_API UCivilizationResilienceSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resilience")
    void OnSimStep(int32 StepNumber, int32 SettlementID, TArray<FAgentData>& Agents, UWorld* World);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resilience")
    float ComputeCivHealth(const FCivilizationData& Civ) const;

    FResilienceSaveState GetResilienceSaveState() const;
    void RestoreResilienceFromSave(const FResilienceSaveState& S);

    void ProcessOneCiv(FCivilizationData& Civ, int32 StepNumber, UWorld* World);

    FResilienceRecord& FindOrAddRecord(int32 CivID);

    void BuildScoreBundleFromWorld(
        const FCivilizationData& Civ,
        UWorld* World,
        float& OutTerritoryScore,
        float& OutEconomyScore,
        float& OutPopulationScore,
        float& OutMilitaryScore,
        float& OutHealth) const;

    float ComputeCivHealthFromWorld(const FCivilizationData& Civ, UWorld* World) const;

    void LogCivHealth(
        const FCivilizationData& Civ,
        int32 StepNumber,
        UWorld* World,
        float Health) const;

    void LogCivHealthFromScores(
        const FCivilizationData& Civ,
        int32 StepNumber,
        float TerritoryScore,
        float EconomyScore,
        float PopulationScore,
        float MilitaryScore,
        float Health) const;

    void ApplyHealthOutcome(
        FCivilizationData& Civ,
        FResilienceRecord& Record,
        int32 StepNumber,
        UWorld* World,
        float Health);

    float GetCollapseThreshold() const;
    int32 GetConsecutiveCollapseTicksRequired() const;

    void ExecuteCollapse(
        FCivilizationData& Civ,
        FResilienceRecord& Record,
        int32 StepNumber,
        UWorld* World);

private:
    float DeriveTerritoryScore(const FCivilizationData& Civ) const;
    float DeriveEconomyScore(const FCivilizationData& Civ, UWorldEconomySimulation* EconSim) const;
    float DerivePopulationScore(const FCivilizationData& Civ) const;
    float DeriveMilitaryScore(const FCivilizationData& Civ) const;

    ECivilizationCollapseType DetermineCollapseType(const FCivilizationData& Civ) const;
    void TriggerCollapse(FCivilizationData& Civ, int32 StepNumber, UWorld* World);
    void DispatchCollapseEvent(const FCivilizationData& Civ, int32 StepNumber, UWorld* World) const;
    void SpawnRefugeeEvent(const FCivilizationData& Civ, int32 StepNumber, UWorld* World) const;

private:
    UPROPERTY()
    TMap<int32, FResilienceRecord> Records;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Resilience")
    float CollapseThreshold = 0.30f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Resilience")
    int32 ConsecutiveCollapseTicksRequired = 3;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Resilience")
    int32 LivingWorldTickInterval = 50;
};