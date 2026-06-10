#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldGenBootstrapActor.generated.h"

class AWorldForgeLakeSurfaceActor;
class AWorldForgeRiverSurfaceActor;
class UWorldForgeRNG;
class UWorldGenPipeline;
class UUserWidget;
class AWorldForgePlayerController;

UCLASS()
class WORLDFORGE_API AWorldGenBootstrapActor : public AActor
{
    GENERATED_BODY()

public:
    AWorldGenBootstrapActor();
    void QueueSelectedTileCameraHandoff(int32 TileX, int32 TileY);
    void QueueSelectedTileCameraHandoff(int32 TileX, int32 TileY, const FVector& ResolvedSpawnWorldLocation);

    bool IsPrePlayBlocking() const { return bWorldForgePrePlayBlocking; }
    bool NotifyFullMapConfirmedAndDeferDismiss(UUserWidget* Widget, int32 TileX, int32 TileY);
    bool NotifyFullMapConfirmedAndDeferDismissWithSpawn(UUserWidget* Widget, int32 TileX, int32 TileY, const FVector& SpawnWorldLocation);
    void NotifyFullMapCancelled();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleTerrainReady();

    // Bootstrap owns runtime presentation. Generated map data is handed to
    // voxel terrain only after WorldMapSubsystem has been initialized.
    bool StartVoxelTerrainHandoff();
    void ProcessPendingSelectedTileCameraHandoff();
    void PollVoxelMeshReady();
    void CompleteSelectedTileCameraHandoff();
    void ReleasePrePlayBlock();
    void ClearPendingSelectedTileCameraHandoff();
    bool ResolvePendingCameraHandoffWorldLocation(FVector& OutWorldLocation) const;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    int64 WorldSeed = 12345;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    FWorldGenConfig WorldGenConfig;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    bool bGenerateOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    bool bDestroyAfterBootstrap = false;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    TSubclassOf<UWorldGenPipeline> PipelineClass;

    UPROPERTY(EditAnywhere, Category = "WorldForge|UI")
    TSubclassOf<UUserWidget> FullMapWidgetClass;

    UPROPERTY(Transient)
    TObjectPtr<UWorldForgeRNG> RNG = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UWorldGenPipeline> Pipeline = nullptr;

    bool bDecorationPassDispatched = false;
    bool bBoundToTerrainReady = false;
    bool bPendingSelectedTileCameraHandoff = false;
    bool bWorldForgePrePlayBlocking = false;
    bool bFullMapConfirmed = false;
    bool bTerrainVisualReadyForPlay = false;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> PendingFullMapWidgetInstance = nullptr;
    FIntPoint PendingCameraHandoffTile = FIntPoint(INDEX_NONE, INDEX_NONE);
    FVector PendingCameraHandoffWorldLocation = FVector::ZeroVector;
    bool bPendingCameraHandoffWorldLocationPreResolved = false;
    FTimerHandle CameraHandoffTimerHandle;
    float CameraHandoffPollStartTimeSeconds = 0.0f;
    int32 ConsecutiveReadyPolls = 0;
    int32 LastPollTotalTasks = -1;
    int32 LastPollMeshTasks = -1;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Camera", meta = (ClampMin = "0.0"))
    float LocalMeshWarmupDelaySeconds = 3.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline")
    bool bEnableWaterlineLakePoolPlacement = false;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline", meta = (ClampMin = "1.0"))
    float WaterlineLakeBaseHalfExtent = 5000.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline")
    float WaterlineLakeZOffset = 25.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline", meta = (ClampMin = "0.01"))
    float MinWaterlineLakeScaleXY = 0.10f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline", meta = (ClampMin = "0.01"))
    float MaxWaterlineLakeScaleXY = 50.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline", meta = (ClampMin = "0.0"))
    float MinWaterlineLakeHalfExtent = 1600.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight", meta = (ClampMin = "1.0"))
    float LightweightLakeBaseHalfExtent = 5000.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight", meta = (ClampMin = "0.01"))
    float LightweightLakeCoverageScale = 0.92f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight")
    float LightweightLakeZOffset = 5.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight", meta = (ClampMin = "0.01"))
    float MinLightweightLakeScaleXY = 0.05f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight", meta = (ClampMin = "0.01"))
    float MaxLightweightLakeScaleXY = 50.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight", meta = (ClampMin = "0.0"))
    float MinLightweightLakeHalfExtent = 800.0f;

    // Legacy BP_WaterSim_V2_Local runtime spawn controls are retained but ignored.
    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline")
    bool bEnableRuntimeWaterlineLakeActorSpawn = false;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline", meta = (ClampMin = "0.0"))
    float MaxRuntimeWaterlineLakeBoundsZExtent = 500.0f;

    // WATERLINE-LAKE-02: retained only for the explicit legacy toggle path.
    bool bWaterlineLakePoolApplied = false;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldForgeLakeSurfaceActor>> SpawnedLightweightLakeActors;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight")
    bool bUseLightweightRiverSurfaces = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Waterline|Lightweight")
    float LightweightRiverZOffset = 4.0f;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldForgeRiverSurfaceActor>> SpawnedLightweightRiverActors;

    void SpawnWaterlineLakeSurfaces();
    void ApplyWaterlineLakePoolPlacement(const TArray<FWaterlineLakeSurfaceDesc>& Descs);
    void SpawnLightweightWaterlineLakeSurfaces(const TArray<FWaterlineLakeSurfaceDesc>& Descs);
    int32 CleanupSpawnedLightweightLakeActors();

    void SpawnWaterlineRiverSurfaces();
    void SpawnLightweightRiverSurfaces(const TArray<FWorldForgeRiverSurfaceDesc>& Descs);
    int32 CleanupSpawnedLightweightRiverActors();

    void AlignWaterlineOceanToSeaLevel();
    void RunBootstrap();
};
