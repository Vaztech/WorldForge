#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "CivilizationActivitySubsystem.generated.h"

class USettlementManager;
class UProcessorManager;
class UFarmingSubsystem;
class UMiningSubsystem;
class UVoxelConstructionSystem;
class UVoxelWorldManager;
class USimulationTimeManager;

/**
 * Lightweight active simulation bridge for generated civilizations.
 *
 * World generation creates FCivilizationData (history/economy/territory), but
 * those civilizations are not automatically active colony settlements. This
 * subsystem turns generated civs into real SettlementManager/ProcessorManager
 * settlements so they can own agents, buildings, farm zones, stockpiles and
 * future job planners.
 *
 * Keep expansion here instead of cramming NPC civilization logic into
 * WorldGenPipeline, SettlementManager, or the player ColonyManager.
 */
UCLASS(BlueprintType)
class WORLDFORGE_API UCivilizationActivitySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    /** Called after world generation and map initialization. */
    void InitializeFromGeneratedWorld(
        const TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& Tiles,
        int32 GridWidth,
        int32 GridHeight,
        bool bForceRebuild = false);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|CivilizationActivity")
    void TickCivilizationActivity(int32 StepNumber);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|CivilizationActivity")
    void SetActivityEnabled(bool bEnabled) { bEnableActivitySimulation = bEnabled; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|CivilizationActivity")
    bool IsActivityEnabled() const { return bEnableActivitySimulation; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|CivilizationActivity")
    TMap<int32, int32> GetCivToSettlementMap() const { return CivToSettlementID; }

private:
    void CacheSubsystems();
    bool IsValidTileIndex(int32 TileIndex, int32 TileCount) const;
    FIntPoint TileIndexToCell(int32 TileIndex, int32 Width) const;
    FGameplayTag ResolveCivRace(const FCivilizationData& Civ) const;
    FGameplayTag ResolveFallbackRaceByIndex(int32 CivIndex) const;
    void SeedNPCSettlement(const FCivilizationData& Civ, const FIntPoint& StartCell, int32 CivIndex);
    void SeedNPCAgents(int32 SettlementID, const FCivilizationData& Civ, const FIntPoint& WorldOrigin, int32 CivIndex);
    void SeedNPCStockpile(int32 SettlementID);
    void SeedNPCBuildings(int32 SettlementID);
    void SeedNPCFarmZone(int32 SettlementID);
    void EnsureActivityForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber);
    void EnsureBuildingPlanForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber);
    void EnsureMiningPlanForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber);
    void EnsureFarmPlanForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber);
    bool FindMiningCandidateNearSettlement(const FSettlementState& Settlement, int32 SearchSeed, FIntVector& OutVoxel) const;
    void SetAllKnownJobPrioritiesHigh(FAgentData& Agent) const;
    void ApplyRaceSkillDefaults(FAgentData& Agent) const;

private:
    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity")
    bool bEnableActivitySimulation = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity")
    bool bCreateNPCSettlements = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity", meta=(ClampMin="1", UIMin="1"))
    int32 NPCAgentsPerCivilization = 4;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity", meta=(ClampMin="1", UIMin="1"))
    int32 NPCFarmSize = 3;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity", meta=(ClampMin="1", UIMin="1"))
    int32 ActivityIntervalSteps = 25;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity|Mining", meta=(ClampMin="1", UIMin="1"))
    int32 MiningPlanIntervalSteps = 25;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity|Mining", meta=(ClampMin="1", UIMin="1"))
    int32 MiningDesignationsPerPlan = 2;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity|Mining", meta=(ClampMin="1", UIMin="1"))
    int32 MaxOpenMiningDesignationsPerSettlement = 8;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity|Mining", meta=(ClampMin="1", UIMin="1"))
    int32 MiningSearchRadiusCells = 8;

    UPROPERTY(EditAnywhere, Category = "WorldForge|CivilizationActivity")
    FIntPoint SettlementOriginOffset = FIntPoint(-2, -2);

    UPROPERTY(Transient)
    TObjectPtr<USettlementManager> SettlementManager = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UProcessorManager> ProcessorManager = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UFarmingSubsystem> FarmingSubsystem = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UMiningSubsystem> MiningSubsystem = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UVoxelConstructionSystem> ConstructionSystem = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UVoxelWorldManager> VoxelWorldManager = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USimulationTimeManager> SimulationTimeManager = nullptr;

    UPROPERTY(Transient)
    TMap<int32, int32> CivToSettlementID;

    UPROPERTY(Transient)
    TSet<int32> SeededFarmSettlements;

    UPROPERTY(Transient)
    TSet<int32> SeededInitialBuildingSettlements;

    UPROPERTY(Transient)
    TMap<int32, int32> LastMiningPlanStepBySettlement;

    UPROPERTY(Transient)
    TMap<int32, int32> LastBuildPlanStepBySettlement;

    UPROPERTY(Transient)
    TMap<int32, int32> MiningPlanCursorBySettlement;

    bool bInitializedFromWorld = false;
};
