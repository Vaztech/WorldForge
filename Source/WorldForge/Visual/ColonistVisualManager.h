#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/TimerHandle.h"
#include "ColonistVisualManager.generated.h"

class AWorldForgeColonist;
class UProcessorManager;
class USimulationTimeManager;

UCLASS(BlueprintType)
class WORLDFORGE_API UColonistVisualManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SpawnColonistActors(int32 InSettlementID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SyncActorsToAgents(int32 InSettlementID);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Visual")
    AWorldForgeColonist* GetActorForAgent(int32 AgentID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void DestroyAllColonistActors();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|Visual")
    TSubclassOf<AWorldForgeColonist> ColonistActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual")
    float InitialSpawnDelaySeconds = 0.20f;

private:
    UFUNCTION()
    void HandleSimulationStep(int32 StepNumber);

    void HandleDeferredSpawn();
    void InternalSpawnColonistActors(int32 InSettlementID);

    AWorldForgeColonist* SpawnActorFromData(const struct FAgentData& AgentData, float CellSize);
    void DestroyActorForAgent(int32 AgentID);

    // Returns the voxel-space cell size (UVoxelWorldManager::VoxelSize) used by
    // agent grid coordinates. Chunk-indexed terrain cache queries happen downstream
    // via UVoxelWorldManager::VoxelCellToChunkCell, not through this value.
    float ResolveCellSize() const;

private:
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<AWorldForgeColonist>> ColonistActors;

    UPROPERTY(Transient)
    int32 ActiveSettlementID = INDEX_NONE;

    UPROPERTY(Transient)
    int32 PendingSpawnSettlementID = INDEX_NONE;

    UPROPERTY(Transient)
    TObjectPtr<UProcessorManager> CachedProcessorManager = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USimulationTimeManager> CachedSimulationTimeManager = nullptr;

    FTimerHandle DeferredSpawnTimerHandle;
    bool bSpawnDeferredScheduled = false;
    bool bIsSyncInProgress = false;
};