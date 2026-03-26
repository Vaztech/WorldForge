#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "VoxelWorldManager.generated.h"

UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UVoxelWorldManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Phase V-C: finds the placed VoxelWorld actor in the current level and caches it.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool FindAndCacheVoxelWorld();

    // Lazy readiness check: if not cached yet, attempts to find the VoxelWorld now.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool IsReady();

    // Converts sim grid coordinates into UE world coordinates.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FVector GridToWorld(FIntPoint GridCell, int32 ZLevel = 0) const;

    // ADDED:
    // Converts UE world coordinates back into sim grid coordinates.
    // This is the BP-safe inverse helper for click-to-build workflows.
    // Uses voxel-world origin offset when available and center-based snapping.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntPoint WorldToGrid(FVector WorldPosition) const;

    // ADDED:
    // 3D helper for future vertical build workflows.
    // Current player flow is XY-only, so this is additive and does not break existing BP.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntVector WorldToGrid3D(FVector WorldPosition) const;

    // Existing system: biome/chunk application pipeline
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void ApplyBiomeProfileToChunk(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile);

    // MODIFIED:
    // Kept existing API exactly the same.
    // This now also writes a visible log line in the .cpp for V-H manual validation.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void PlaceVoxelBlock(FVector WorldPosition, FGameplayTag MaterialTag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void NotifyVoxelDig(FVector WorldPosition, int32 AgentID);

    // Existing validation path
    bool ValidateSmoothSDF(const FVoxelBiomeProfile& Profile) const;

    // Existing chunk sizing
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    float ChunkWorldSize = 3200.0f;

    // Added for GridToWorld helper
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    float VoxelSize = 100.0f;

    // ADDED:
    // Optional debug logging for click-to-grid conversion validation.
    // Useful for confirming repeated clicks on the same tile resolve to the same grid cell.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogGridConversions = true;

protected:
    // Existing BP bridge events
    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Voxel")
    void OnBiomeChunkRequested(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile);

    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Voxel")
    void OnVoxelBlockPlaced(FVector WorldPosition, FGameplayTag MaterialTag);

    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Voxel")
    void OnVoxelDigged(FVector WorldPosition);

private:
    // ADDED:
    // Returns the voxel-world origin if the actor is available.
    // Falls back to world zero if no voxel actor is currently cached/found.
    FVector GetVoxelWorldOrigin() const;

    // Added Phase V-C cache
    UPROPERTY()
    AActor* VoxelWorldActor = nullptr;
};