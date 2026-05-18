// VoxelConstructionSystem.h

#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "VoxelConstructionSystem.generated.h"

class UWorld;

// FActiveBuildTask is defined in WorldForgeDataTypes.h

UCLASS()
class WORLDFORGE_API UVoxelConstructionSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable)
    int32 StartBuildTask(int32 SID, FGameplayTag BuildTag, FIntPoint Origin, int32 AgentID);

    // Legacy flat custom-build entry point kept for compatibility.
    // Internally converted into a 3D block list at Z=0.
    int32 StartCustomBuildTask(
        int32 SID,
        const TArray<FIntPoint>& Blocks,
        FGameplayTag MaterialTag,
        int32 AgentID
    );

    // New uncapped 3D custom-build entry point.
    int32 StartCustomBuildTask3D(
        int32 SID,
        const TArray<FIntVector>& Blocks,
        FGameplayTag MaterialTag,
        int32 AgentID
    );

    // New explicit prefab/build entry point using a full 3D block list.
    int32 StartBuildTaskFromBlocks(
        int32 SID,
        FGameplayTag BuildTag,
        const TArray<FIntVector>& Blocks,
        FGameplayTag MaterialTag,
        int32 AgentID
    );

    bool PlaceNextBlock(int32 TaskID, UWorld* World);
    bool IsTaskComplete(int32 TaskID) const;

    bool HasActiveTask(int32 SID, FGameplayTag BuildTag) const;

    FActiveBuildTask* GetMutableTask(int32 TaskID);
    FActiveBuildTask* GetMutableTaskForAgent(int32 AgentID);

    const TArray<FActiveBuildTask>& GetAllTasks() const { return ActiveTasks; }

    void RestoreFromSave(const TArray<FActiveBuildTask>& Saved);

    int32 GetRequiredUnitsForPrefab(FGameplayTag BuildTag) const;
    int32 GetRequiredUnitsForCustom(int32 CellCount) const;

    FGameplayTag GetRequiredItemForPrefab(FGameplayTag BuildTag) const;
    FGameplayTag GetRequiredItemForMaterial(FGameplayTag MaterialTag) const;

    int32 GetAvailableUnitsForPrefab(int32 SettlementID, FGameplayTag BuildTag, UWorld* World) const;
    int32 GetAvailableUnitsForMaterial(int32 SettlementID, FGameplayTag MaterialTag, UWorld* World) const;

    bool CanAffordPrefab(int32 SettlementID, FGameplayTag BuildTag, UWorld* World) const;
    bool CanAffordCustom(int32 SettlementID, FGameplayTag MaterialTag, int32 CellCount, UWorld* World) const;

private:
    void FinalizeBuilding(const FActiveBuildTask& Task, UWorld* World);

    int32 GetBlockCount(FGameplayTag BuildTag) const;

    // Builds an explicit 3D block list for legacy prefab placement.
    TArray<FIntVector> BuildPrefabBlockList(FGameplayTag BuildTag, FIntPoint Origin) const;

    // Convert any task into its effective 3D block list.
    void GetEffectiveBlockList3D(const FActiveBuildTask& Task, TArray<FIntVector>& OutBlocks) const;

    bool TaskOccupiesBlock(const FActiveBuildTask& Task, const FIntVector& Block) const;

    bool HasConflictingTaskAtOrigin(int32 SettlementID, FIntPoint Origin) const;
    bool HasConflictingTaskInBlockList(int32 SettlementID, const TArray<FIntVector>& Blocks) const;
    bool AreBlocksInsidePlayerClaimIfNeeded(int32 SettlementID, const TArray<FIntVector>& Blocks) const;

    FGameplayTag ResolveRequiredStockpileItem(FGameplayTag MaterialTag) const;

    bool HasEnoughStockpileForTask(
        int32 SettlementID,
        FGameplayTag MaterialTag,
        int32 RequiredUnits,
        UWorld* World) const;

    TArray<FActiveBuildTask> ActiveTasks;

    int32 NextTaskID = 0;
};
