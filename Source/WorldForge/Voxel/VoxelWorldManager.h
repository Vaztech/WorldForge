#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Engine/TimerHandle.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldForgeTerrainSnapshot.h"
#include "VoxelWorldManager.generated.h"

class AVoxelWorld;
class AStaticMeshActor;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTerrainReady);

UENUM(BlueprintType)
enum class EWorldForgeTerrainType : uint8
{
    Grass UMETA(DisplayName = "Grass"),
    Dirt  UMETA(DisplayName = "Dirt"),
    Rock  UMETA(DisplayName = "Rock"),
    Sand  UMETA(DisplayName = "Sand"),
    Snow  UMETA(DisplayName = "Snow")
};

USTRUCT(BlueprintType)
struct FPerlinOctaveSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin")
    float BaseFrequency = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin")
    float BaseAmplitude = 3.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin", meta = (ClampMin = "1", ClampMax = "8"))
    int32 OctaveCount = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin", meta = (ClampMin = "1.5", ClampMax = "3.0"))
    float Lacunarity = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin", meta = (ClampMin = "0.1", ClampMax = "0.9"))
    float Persistence = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin", meta = (ClampMin = "2"))
    int32 MinHeight = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin")
    int32 SeedOffset = 0;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UVoxelWorldManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool FindAndCacheVoxelWorld();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool IsReady();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool IsTerrainReadyForGameplay() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool IsTerrainRuntimeStable() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool IsSurfaceReady() const
    {
        return bTerrainReadyBroadcasted && IsTerrainRuntimeStable();
    }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FVector GridToWorld(FIntPoint GridCell, int32 ZLevel = 0) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntPoint WorldToGrid(FVector WorldPosition) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntVector WorldToGrid3D(FVector WorldPosition) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntPoint WorldToTerrainCell(FVector WorldPosition) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FVector TerrainCellToWorld(FIntPoint GridCell, float WorldZ = 0.0f, bool bCenterInCell = true) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    float GetRenderVoxelSize() const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void ApplyBiomeProfileToChunk(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool GenerateTerrainFromTiles(
        const TArray<FWorldTileData>& WorldTiles,
        int32 GridWidth,
        int32 GridHeight,
        ETerrainMode TerrainMode = ETerrainMode::Smooth,
        int32 BlockHeightStep = 4);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void PlaceVoxelBlock(FVector WorldPosition, FGameplayTag MaterialTag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void NotifyVoxelDig(FVector WorldPosition, int32 AgentID);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool HasCachedTerrainData() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool TryGetSurfaceWorldLocation(FIntPoint GridCell, FVector& OutWorldLocation, float ZOffset = 50.0f) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool TryGetTerrainSurfaceVoxelZ(FIntPoint VoxelXY, int32& OutVoxelZ) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool IsSpawnableTerrainCell(FIntPoint GridCell) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool FindNearestSpawnableTerrainCell(FIntPoint StartCell, FIntPoint& OutCell, int32 MaxSearchRadius = 16) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntPoint ClampTerrainCellToBounds(FIntPoint GridCell) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntPoint VoxelCellToChunkCell(FIntPoint VoxelCell) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel|Terrain")
    bool GetTerrainTypeAtCell(FIntPoint GridCell, EWorldForgeTerrainType& OutTerrainType) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel|Terrain")
    bool GetTerrainTypeAtVoxel(FIntPoint VoxelXY, EWorldForgeTerrainType& OutTerrainType) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel|Terrain")
    bool GetTerrainSlopeAtCell(FIntPoint GridCell, float& OutSlopeDegrees) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel|Terrain")
    bool GetTerrainSlopeAtVoxel(FIntPoint VoxelXY, float& OutSlopeDegrees) const;

    bool ValidateSmoothSDF(const FVoxelBiomeProfile& Profile) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Perlin")
    static float PerlinNoise2D(float X, float Y);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Perlin")
    static float FractalNoise2D(float X, float Y, const FPerlinOctaveSettings& Settings);

    UPROPERTY(BlueprintAssignable, Category = "WorldForge|Voxel")
    FOnTerrainReady OnTerrainReady;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    float ChunkWorldSize = 3200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    float VoxelSize = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    float TerrainSurfaceZOffset = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    bool bAddGlobalFoundation = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    float GlobalFoundationDepthWorld = 5000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Perlin")
    FPerlinOctaveSettings DefaultPerlinSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SandMaxElevationNormalized = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RockMinElevationNormalized = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SnowMinElevationNormalized = 0.90f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Water")
    int32 SeaLevelVoxels = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float ContinentFrequency = 0.00018f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    int32 ContinentHeightRangeVoxels = 90;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float ContinentExponent = 2.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float BasinFrequency = 0.00055f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float BasinThreshold = 0.82f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    int32 LakeBasinDepthVoxels = 14;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    float RiverFrequency = 0.0015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    float RiverThreshold = 0.92f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    int32 RiverCarveDepthVoxels = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    float RiverSharpness = 2.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogGridConversions = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogTerrainGeneration = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogTerrainRetry = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bUseFallbackPlacedBlockVisuals = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Performance")
    bool bUseFastTerrainSampling = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bPreferSurfaceAnchoredPlacedBlocks = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogPlacedBlockVisuals = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    float FallbackPlacedBlockVerticalNudge = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    float TerrainRetryIntervalSeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel")
    int32 MaxTerrainRetryCount = 40;

protected:
    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Voxel")
    void OnBiomeChunkRequested(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile);

    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Voxel")
    void OnVoxelBlockPlaced(FVector WorldPosition, FGameplayTag MaterialTag);

    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Voxel")
    void OnVoxelDigged(FVector WorldPosition);

private:
    FVector GetVoxelWorldOrigin() const;
    float GetTerrainCellWorldSize() const;
    bool HasPendingTerrainRequest() const;

    bool IsVoxelRuntimeReady() const;
    bool ApplyBiomeChunkNative(AVoxelWorld* VoxelWorld, FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile, float TileElevation);
    bool ApplyTerrainFromTilesInternal(AVoxelWorld* VoxelWorld, const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight);
    bool ApplyGlobalFoundationNative(AVoxelWorld* VoxelWorld, int32 GridWidth, int32 GridHeight);
    void CacheTerrainMetadata(const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight);
    void ClearTerrainCache();
    void ClearPlacedBlockVisuals();

    void BuildTerrainSnapshot(const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight);
    void ClearTerrainSnapshot();
    bool HasTerrainSnapshot() const;

    int32 GetChunkSizeInVoxels() const;
    int32 ComputeChunkSurfaceHeightVoxels(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile, float TileElevation) const;
    int32 ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const;
    int32 GetEffectiveBlockHeightStep() const;

    float ComputeSmoothSurfaceHeightAtVoxelFloat(int32 VoxelX, int32 VoxelY, const FVoxelBiomeProfile& Profile) const;
    int32 ComputeSmoothSurfaceHeightAtVoxel(int32 VoxelX, int32 VoxelY, const FVoxelBiomeProfile& Profile) const;

    float ComputeSmoothSurfaceHeightAtWorldGrid(FIntPoint GridCell, const FVoxelBiomeProfile& Profile) const;
    int32 ComputeMacroTerrainBaseHeightVoxels(FIntPoint ChunkCoord, float TileElevation) const;
    int32 ComputeRiverCarveDepthVoxels(FIntPoint ChunkCoord) const;

    FPerlinOctaveSettings GetOctaveSettingsForProfile(const FVoxelBiomeProfile& Profile) const;

    float ComputeDetailNoiseHeightBilinear(
        int32 MinX,
        int32 MinY,
        int32 MaxX,
        int32 MaxY,
        int32 X,
        int32 Y,
        const FVoxelBiomeProfile& Profile
    ) const;

    float ComputeDetailNoiseHeightBilinearFloat(
        int32 MinX,
        int32 MinY,
        int32 MaxX,
        int32 MaxY,
        int32 X,
        int32 Y,
        const FVoxelBiomeProfile& Profile
    ) const;

    float ComputeSurfaceHeightFloat(
        int32 VoxelX,
        int32 VoxelY,
        int32 MinX,
        int32 MinY,
        int32 MaxX,
        int32 MaxY,
        float Elev00,
        float Elev10,
        float Elev01,
        float Elev11,
        float Continent00,
        float Continent10,
        float Continent01,
        float Continent11,
        float Basin00,
        float Basin10,
        float Basin01,
        float Basin11,
        float River00,
        float River10,
        float River01,
        float River11,
        const FVoxelBiomeProfile& Profile
    ) const;

    int32 QuantizeSurfaceHeightToVoxel(float SurfaceHeightF) const;

    EWorldForgeTerrainType ClassifyTerrainCellInternal(FIntPoint GridCell) const;
    float GetNormalizedElevationForTerrainCell(FIntPoint GridCell) const;
    float ComputeTerrainSlopeDegreesInternal(FIntPoint GridCell) const;

    static float Fade(float T);
    static float Lerp(float T, float A, float B);
    static float Grad(int32 Hash, float X, float Y);
    static const int32 PermutationTable[512];

    bool IsValidTerrainCell(FIntPoint GridCell) const;
    int32 TerrainIndex(FIntPoint GridCell) const;

    bool TryGetCachedSurfaceHeightWorldForVoxelCell(FIntPoint VoxelCell, float& OutSurfaceHeightWorld) const;
    FVector ResolvePlacedBlockVisualWorldPosition(const FVector& RequestedWorldPosition) const;
    AStaticMeshActor* SpawnFallbackPlacedBlockActor(const FIntVector& GridCell3D, const FVector& SpawnWorldPosition, FGameplayTag MaterialTag);

    void ScheduleTerrainRetry();
    void RetryPendingTerrainGeneration();
    void ClearPendingTerrainRequest();
    void BroadcastTerrainReadyIfNeeded();

    UPROPERTY()
    AActor* VoxelWorldActor = nullptr;

    FWorldForgeTerrainSnapshot TerrainSnapshot;

    UPROPERTY(Transient)
    TArray<FWorldTileData> PendingWorldTiles;

    UPROPERTY(Transient)
    TArray<float> CachedSurfaceHeightsVoxels;

    UPROPERTY(Transient)
    TArray<float> CachedTileElevations;

    UPROPERTY(Transient)
    TArray<FVoxelBiomeProfile> CachedVoxelProfiles;

    UPROPERTY(Transient)
    TArray<FGameplayTag> CachedBiomeTags;

    UPROPERTY(Transient)
    TMap<FIntVector, TObjectPtr<AStaticMeshActor>> FallbackPlacedBlockActors;

    UPROPERTY(Transient)
    FTimerHandle TerrainRetryTimerHandle;

    UPROPERTY(Transient)
    int32 PendingGridWidth = 0;

    UPROPERTY(Transient)
    int32 PendingGridHeight = 0;

    UPROPERTY(Transient)
    int32 CachedTerrainGridWidth = 0;

    UPROPERTY(Transient)
    int32 CachedTerrainGridHeight = 0;

    UPROPERTY(Transient)
    int32 TerrainRetryCount = 0;

    UPROPERTY(Transient)
    bool bTerrainRetryScheduled = false;

    UPROPERTY(Transient)
    bool bTerrainReadyBroadcasted = false;

    UPROPERTY(Transient)
    ETerrainMode CurrentTerrainMode = ETerrainMode::Smooth;

    UPROPERTY(Transient)
    int32 CurrentBlockHeightStep = 4;
};