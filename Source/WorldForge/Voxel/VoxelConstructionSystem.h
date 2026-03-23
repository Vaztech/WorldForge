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

    FActiveBuildTask* GetMutableTask(int32 TaskID);

    FActiveBuildTask* GetMutableTaskForAgent(int32 AgentID);

    const TArray<FActiveBuildTask>& GetAllTasks() const { return ActiveTasks; }

    void RestoreFromSave(const TArray<FActiveBuildTask>& Saved);

private:
    void FinalizeBuilding(const FActiveBuildTask& Task, UWorld* World);

    int32 GetBlockCount(FGameplayTag BuildTag) const;

    TArray<FActiveBuildTask> ActiveTasks;

    int32 NextTaskID = 0;
};