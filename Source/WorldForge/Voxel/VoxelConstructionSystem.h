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

    // Starts a custom construction task using an explicit list of grid cells
    // instead of the legacy prefab footprint logic.
    // This is the minimal Section I entry point for player-authored shapes.
    int32 StartCustomBuildTask(
        int32 SID,
        const TArray<FIntPoint>& Blocks,
        FGameplayTag MaterialTag,
        int32 AgentID
    );

    bool PlaceNextBlock(int32 TaskID, UWorld* World);
    bool IsTaskComplete(int32 TaskID) const;

    // Legacy query kept for any callers that still want to know if a given
    // building tag is currently under construction anywhere in the settlement.
    bool HasActiveTask(int32 SID, FGameplayTag BuildTag) const;

    FActiveBuildTask* GetMutableTask(int32 TaskID);

    FActiveBuildTask* GetMutableTaskForAgent(int32 AgentID);

    const TArray<FActiveBuildTask>& GetAllTasks() const { return ActiveTasks; }

    void RestoreFromSave(const TArray<FActiveBuildTask>& Saved);

private:
    void FinalizeBuilding(const FActiveBuildTask& Task, UWorld* World);

    int32 GetBlockCount(FGameplayTag BuildTag) const;

    // Reject only truly conflicting prefab tasks at the same placement cell,
    // instead of blocking every active task with the same building tag.
    bool HasConflictingTaskAtOrigin(int32 SettlementID, FIntPoint Origin) const;

    // Resolve which stockpile item should gate this build.
    // For now this is intentionally minimal and deterministic:
    // voxel wood -> Item.Wood
    // everything else defaults to Item.Stone
    FGameplayTag ResolveRequiredStockpileItem(FGameplayTag MaterialTag) const;

    // Returns true only if settlement has enough material in stockpile
    // to create the task up front.
    bool HasEnoughStockpileForTask(
        int32 SettlementID,
        FGameplayTag MaterialTag,
        int32 RequiredUnits,
        UWorld* World) const;

    TArray<FActiveBuildTask> ActiveTasks;

    int32 NextTaskID = 0;
};