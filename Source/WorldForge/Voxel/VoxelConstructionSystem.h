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

    bool PlaceNextBlock(int32 TaskID, UWorld* World);
    bool IsTaskComplete(int32 TaskID) const;
    bool HasActiveTask(int32 SID, FGameplayTag BuildTag) const;

    // Returns mutable task by TaskID.
    // Used internally by construction logic.
    FActiveBuildTask* GetMutableTask(int32 TaskID);

    // Clean mutable accessor by agent ID -- ConstructionProcessor uses this.
    // Avoids unsafe const_cast usage from GetAllTasks().
    FActiveBuildTask* GetMutableTaskForAgent(int32 AgentID);

    // Read-only access for iteration/debug/UI.
    const TArray<FActiveBuildTask>& GetAllTasks() const { return ActiveTasks; }

    void RestoreFromSave(const TArray<FActiveBuildTask>& Saved);

private:
    // Finalizes building placement once construction completes.
    void FinalizeBuilding(const FActiveBuildTask& Task, UWorld* World);

    // Returns required block count for a given building tag.
    int32 GetBlockCount(FGameplayTag BuildTag) const;

    // Active construction tasks (Phase 2 scale: linear scan is acceptable).
    TArray<FActiveBuildTask> ActiveTasks;

    int32 NextTaskID = 0;
};