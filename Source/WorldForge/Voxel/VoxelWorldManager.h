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
class UResourceSpawnSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTerrainReady);

UENUM(BlueprintType)
enum class EWorldForgeTerrainType : uint8
{
    Water UMETA(DisplayName = "Water"),
    Grass UMETA(DisplayName = "Grass"),
    Dirt  UMETA(DisplayName = "Dirt"),
    Rock  UMETA(DisplayName = "Rock"),
    Sand  UMETA(DisplayName = "Sand"),
    Snow  UMETA(DisplayName = "Snow"),
    Lava  UMETA(DisplayName = "Lava")
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

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    int32 GetChunkSizeInVoxels() const;

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

    // ----- Phase 1: Dig API -----

    /**
     * Remove the block at the given grid position, replacing it with air.
     *
     * @param GridPos  Grid-space position of the block to remove.
     * @return         True if a solid block was removed. False if already air,
     *                 out of bounds, or is bedrock/undiggable.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool RemoveBlockAt(const FIntVector& GridPos);

    bool TryFindTopSolidBlockInColumn(
        const FIntPoint& GridXY,
        FIntVector& OutBlock,
        int32 MaxSearchDown = 64) const;


    /**
     * Restore a previously removed block for undo.
     *
     * @param GridPos    Grid-space position to restore.
     * @param BlockType  Gameplay tag identifying the block type to place.
     * @return           True if restoration succeeded.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool RestoreBlockAt(const FIntVector& GridPos, const FGameplayTag& BlockType);

    /**
     * Query the block type at a given grid position.
     *
     * @return Valid gameplay tag if a solid block exists.
     *         Invalid tag if air, void, or out of bounds.
     */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FGameplayTag GetBlockTypeAt(const FIntVector& GridPos) const;

    /**
 * Finds the nearest local surface block around a target Z.
 *
 * This is used by digging so slope brushes do not search from the
 * absolute top of the column and accidentally drill downward under hills.
 */
    bool TryFindSurfaceBlockNearZ(
        const FIntPoint& GridXY,
        int32 TargetZ,
        int32 VerticalSearchRadius,
        FIntVector& OutBlock) const;

    /**
     * Check if a grid position is within valid terrain/world bounds.
     */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool IsValidGridPosition(const FIntVector& GridPos) const;

    /**
     * Get the Z coordinate of the topmost solid terrain block at this XY column.
     */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    int32 GetSurfaceZAt(const FIntPoint& GridXY) const;

    /**
     * Get the contiguous solid terrain depth from the surface downward.
     */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    int32 GetTerrainDepthAt(const FIntPoint& GridXY) const;



    // ── FIX (Issue 1): Accept Step03 tectonic ranges so the voxel generator
    // uses the same mountain data as the tile map.  Call before GenerateTerrainFromTiles.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void SetPipelineMountainRanges(const TArray<FMountainRange>& InRanges);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void SetPipelineVolcanoes(const TArray<FVolcanoData>& InVolcanoes);

    // ── FIX (Resource Discovery): Accept updated tile resource data from the pipeline.
    // Call AFTER Step08_SeedResources so PendingWorldTiles and the snapshot
    // contain VeinConfig/ResourceSurvey data for the terrain-ready hook.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    void SetPipelineResourceData(const TArray<FWorldTileData>& UpdatedTiles);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool HasCachedTerrainData() const;

    // ── Resource Discovery: Public accessors for ResourceSpawnSubsystem ──

    /** Get the cached world tiles array (read-only). Returns empty if terrain not cached. */
    const TArray<FWorldTileData>& GetCachedWorldTiles() const;

    /** Get the cached terrain grid width. */
    int32 GetCachedGridWidth() const { return CachedTerrainGridWidth; }

    /** Get the cached terrain grid height. */
    int32 GetCachedGridHeight() const { return CachedTerrainGridHeight; }

    /**
     * Get the world-space surface height at a terrain cell.
     * Returns the surface Z in world units (voxel Z * VoxelSize + origin Z).
     * Returns 0 if the cell is invalid or terrain data is not cached.
     */
    float GetSurfaceHeightWorldAtTerrainCell(FIntPoint Cell) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool TryGetSurfaceWorldLocation(FIntPoint GridCell, FVector& OutWorldLocation, float ZOffset = 0.0f) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool TryGetTerrainSurfaceVoxelZ(FIntPoint VoxelXY, int32& OutVoxelZ) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    bool IsSpawnableTerrainCell(FIntPoint GridCell) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool FindNearestSpawnableTerrainCell(FIntPoint StartCell, FIntPoint& OutCell, int32 MaxSearchRadius = 16) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Voxel")
    bool TryGetNearestSpawnLocationForTerrainCell(
        FIntPoint DesiredCell,
        FVector& OutWorldLocation,
        FIntPoint& OutResolvedCell,
        int32 MaxSearchRadius = 16,
        float ZOffset = 0.0f) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntPoint ClampTerrainCellToBounds(FIntPoint GridCell) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel")
    FIntPoint VoxelCellToChunkCell(FIntPoint VoxelCell) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel|Terrain")
    bool GetTerrainTypeAtCell(FIntPoint GridCell, EWorldForgeTerrainType& OutTerrainType) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel|Terrain")
    bool GetTerrainTypeAtVoxel(FIntPoint VoxelXY, EWorldForgeTerrainType& OutTerrainType) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Voxel|Terrain")
    bool GetTerrainTypeAtVoxelSample(FIntPoint VoxelXY, EWorldForgeTerrainType& OutTerrainType) const;

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
    float DirtMinElevationNormalized = 0.20f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RockMinElevationNormalized = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HighRockMinElevationNormalized = 0.42f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SnowMinElevationNormalized = 0.90f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0"))
    float CoastBandVoxels = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0"))
    float DirtSlopeThresholdDegrees = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0"))
    float RockSlopeThresholdDegrees = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0"))
    float CliffSlopeThresholdDegrees = 18.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Terrain", meta = (ClampMin = "0.0"))
    float NormalizedElevationMaxExtraVoxels = 24.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Water")
    int32 SeaLevelVoxels = 10;

    // ── v2 terrain height fields ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|v2")
    float WaterThreshold = 0.48f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|v2")
    int32 OceanDepthVoxels = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|v2")
    int32 MaxLandHeightVoxels = 150;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|v2")
    float LandElevationExponent = 0.65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float ContinentFrequency = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    int32 ContinentHeightRangeVoxels = 150;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float ContinentExponent = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float BasinFrequency = 0.00055f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    float BasinThreshold = 0.82f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Macro")
    int32 LakeBasinDepthVoxels = 14;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    float RiverFrequency = 0.001f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    float RiverThreshold = 0.95f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    int32 RiverCarveDepthVoxels = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Rivers")
    float RiverSharpness = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0"))
    int32 MountainRangeCount = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "1"))
    int32 MountainMinLengthTiles = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "1"))
    int32 MountainMaxLengthTiles = 80;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "1.0"))
    float MountainMinWidthVoxels = 6.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "1.0"))
    float MountainMaxWidthVoxels = 18.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0.0"))
    float MountainMinHeightVoxels = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0.0"))
    float MountainMaxHeightVoxels = 42.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0.1"))
    float MountainFalloffPower = 2.2f;

    // ── v2.1 Mountain ridge detail layer ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0.0"))
    float MountainRidgeHeightScale = 40.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0.1"))
    float MountainRidgeWidthScale = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0.001"))
    float MountainCragFrequency = 0.06f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Mountains", meta = (ClampMin = "0.0"))
    float MountainCragAmplitude = 6.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Base")
    float BaseHeight = 4000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Base")
    float HeightMultiplier = 6000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Base")
    float ContinentScale = 0.00005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Detail")
    float DetailScale = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Detail")
    float DetailAmplitude = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Detail", meta = (ClampMin = "1", ClampMax = "8"))
    int32 DetailOctaves = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves")
    bool bEnableCaves = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves")
    float CaveFrequency = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CaveThreshold = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves")
    float CaveSecondOctaveMultiplier = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CavePrimaryWeight = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CaveSecondaryWeight = 0.3f;

    // ── v4.0: 3D SDF Worm Cave Settings ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves|WormCaves", meta = (ClampMin = "0.001"))
    float CaveWormFrequency = 0.012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves|WormCaves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CaveWormThreshold = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves|WormCaves", meta = (ClampMin = "0.0"))
    float CaveCarveStrength = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves|WormCaves", meta = (ClampMin = "0.0"))
    float CaveDepthFadeStart = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves|WormCaves", meta = (ClampMin = "0.0"))
    float CaveDepthFadeEnd = 128.0f;

    // ── v4.0: 3D SDF Canyon Settings ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Canyons", meta = (ClampMin = "0.001"))
    float CanyonRidgeFrequency = 0.004f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Canyons", meta = (ClampMin = "0.0"))
    float CanyonDomainWarpStrength = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Canyons", meta = (ClampMin = "0.001"))
    float CanyonDomainWarpFrequency = 0.003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Canyons", meta = (ClampMin = "0.0"))
    float CanyonMaxDepthVoxels = 40.0f;

    // ── v4.0: Lava Tube Settings ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves|LavaTubes")
    bool bEnableLavaTubes = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain|Caves|LavaTubes", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float LavaTubeZCompress = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogGridConversions = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogTerrainGeneration = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bLogTerrainRetry = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Debug")
    bool bUseFallbackPlacedBlockVisuals = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Voxel|Performance")
    bool bUseFastTerrainSampling = false;

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
    // ── FIX: Single source of truth for local snapshot construction.
    // Eliminates all partial-snapshot bugs where GridWidth, GridHeight,
    // MountainMaxHeightVoxels, or other fields were accidentally omitted.
    FWorldForgeTerrainSnapshot MakeLocalTerrainSnapshot() const;

    FVector GetVoxelWorldOrigin() const;
    float GetTerrainCellWorldSize() const;
    bool HasPendingTerrainRequest() const;

    bool IsVoxelRuntimeReady() const;
    bool ApplyBiomeChunkNative(AVoxelWorld* VoxelWorld, FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile, float TileElevation);
    bool ApplyTerrainFromTilesInternal(AVoxelWorld* VoxelWorld, const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight);
    bool ApplyGlobalFoundationNative(AVoxelWorld* VoxelWorld, int32 GridWidth, int32 GridHeight);
    bool RebuildVoxelWorldFromCurrentSnapshot();
    void CacheTerrainMetadata(const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight);
    void ClearTerrainCache();
    void ClearPlacedBlockVisuals();

    void BuildTerrainSnapshot(const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight);
    void ClearTerrainSnapshot();
    bool HasTerrainSnapshot() const;

    int32 ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const;
    int32 GetEffectiveBlockHeightStep() const;

    float ComputeSmoothSurfaceHeightAtVoxelFloat(int32 VoxelX, int32 VoxelY, const FVoxelBiomeProfile& Profile) const;
    int32 ComputeSmoothSurfaceHeightAtVoxel(int32 VoxelX, int32 VoxelY, const FVoxelBiomeProfile& Profile) const;

    float ComputeSmoothSurfaceHeightAtWorldGrid(FIntPoint GridCell, const FVoxelBiomeProfile& Profile) const;
    int32 ComputeMacroTerrainBaseHeightVoxels(FIntPoint ChunkCoord, float TileElevation) const;
    int32 ComputeRiverCarveDepthVoxels(FIntPoint ChunkCoord) const;
    int32 ComputeChunkSurfaceHeightVoxels(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile, float TileElevation) const;

    FPerlinOctaveSettings GetOctaveSettingsForProfile(const FVoxelBiomeProfile& Profile) const;

    float ComputeDetailNoiseHeightBilinear(
        int32 MinX, int32 MinY, int32 MaxX, int32 MaxY,
        int32 X, int32 Y,
        const FVoxelBiomeProfile& Profile) const;

    float ComputeDetailNoiseHeightBilinearFloat(
        int32 MinX, int32 MinY, int32 MaxX, int32 MaxY,
        int32 X, int32 Y,
        const FVoxelBiomeProfile& Profile) const;

    float ComputeSurfaceHeightFloat(
        int32 VoxelX, int32 VoxelY,
        int32 MinX, int32 MinY, int32 MaxX, int32 MaxY,
        float Elev00, float Elev10, float Elev01, float Elev11,
        float Continent00, float Continent10, float Continent01, float Continent11,
        float Basin00, float Basin10, float Basin01, float Basin11,
        float River00, float River10, float River01, float River11,
        const FVoxelBiomeProfile& Profile) const;

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

    // ── Resource Discovery: notify ResourceSpawnSubsystem when terrain is ready ──
    void NotifyResourceSpawnSystemTerrainReady();

    UPROPERTY(Transient)
    TObjectPtr<AVoxelWorld> VoxelWorldActor = nullptr;

    FWorldForgeTerrainSnapshot TerrainSnapshot;

    // ── v5.0: SDF brush helpers for the Hybrid Chunking Modification Path ──
    FSDFBrush MakeDigBrush(const FIntVector& GridPos) const;
    bool ApplyRuntimeBrush(const FSDFBrush& Brush);
    bool ApplyRuntimeVoxelRestore(const FIntVector& GridPos);
    void RefreshEditedVoxel(const FIntVector& GridPos);

    UPROPERTY(Transient)
    TMap<FIntVector, TObjectPtr<AStaticMeshActor>> RuntimeDigHoleActors;

    AStaticMeshActor* SpawnRuntimeDigHoleActor(const FIntVector& GridPos);
    void RemoveRuntimeDigHoleActor(const FIntVector& GridPos);

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

    // ── FIX (Issue 1): Cached Step03 mountain ranges, copied into the
    // terrain snapshot so the voxel generator uses real tectonic data.
    UPROPERTY(Transient)
    TArray<FMountainRange> CachedMountainRanges;

    UPROPERTY(Transient)
    TArray<FVolcanoData> CachedVolcanoes;

    UPROPERTY(Transient)
    TMap<FIntVector, TObjectPtr<AStaticMeshActor>> FallbackPlacedBlockActors;

    // v5.0: RemovedTerrainBlocks replaced by TerrainSnapshot.Modifications
    // (FTerrainModificationMap — chunk-indexed SDF brush volumes)

    UPROPERTY(Transient)
    FTimerHandle TerrainRetryTimerHandle;

    FTimerHandle BatchedTerrainRebuildTimerHandle;
    bool bBatchedTerrainRebuildPending = false;

    void RequestBatchedTerrainRebuild();
    void ExecuteBatchedTerrainRebuild();

    UPROPERTY(EditAnywhere, Category = "WorldForge|Voxel|Dig")
    bool bRebuildWorldAfterDigEdits = false;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Voxel|Dig", meta = (ClampMin = "0.25"))
    float DigFullRebuildDebounceSeconds = 5.0f;

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