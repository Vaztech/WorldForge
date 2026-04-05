#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "VoxelSpawnSubsystem.generated.h"

class USettlementManager;
class USettlementVisualManager;
class UDA_VoxelVisualRuleSet;

/**
 * UVoxelSpawnSubsystem
 *
 * Phase 3 VSS foundation:
 * - read-only observer
 * - polls SettlementManager visual dirty flags
 * - owns per-settlement visual managers
 * - rebuilds settlement visuals after ProcessorManager flushes EventBus
 */
UCLASS()
class WORLDFORGE_API UVoxelSpawnSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Called by ProcessorManager after FlushPendingEvents(). */
    void OnSimStep(int32 StepNumber);

    /** Manual rebuild hook for debugging / Blueprint testing. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|VSS")
    void ForceRebuildSettlement(int32 SettlementID);

    /** Optional override from Blueprint or test setup. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|VSS")
    void SetDefaultRuleSet(UDA_VoxelVisualRuleSet* InRuleSet);

    /** Stub for save support. */
    FVSSSaveState GetVSSSaveState() const;

    /** Stub for save restore. */
    void RestoreVSSFromSave(const FVSSSaveState& SaveState);

private:
    void RebuildSettlementVisuals(int32 SettlementID);
    USettlementVisualManager* GetOrCreateManager(int32 SettlementID);
    UDA_VoxelVisualRuleSet* TryLoadDefaultRuleSet();

private:
    UPROPERTY()
    TObjectPtr<USettlementManager> SettlementManager = nullptr;

    UPROPERTY()
    TObjectPtr<UDA_VoxelVisualRuleSet> DefaultRuleSet = nullptr;

    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<USettlementVisualManager>> SettlementManagers;

    UPROPERTY(Transient)
    TArray<FVoxelSpawnRecord> SpawnRecords;
};