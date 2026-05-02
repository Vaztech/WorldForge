#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/WorldForgeDataTypes.h"
#include "WorldForgeTerrainSnapshot.generated.h"

USTRUCT(BlueprintType)
struct FWorldForgeTerrainSnapshot
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 GridWidth = 0;

    UPROPERTY()
    int32 GridHeight = 0;

    UPROPERTY()
    int32 ChunkSizeVoxels = 0;

    UPROPERTY()
    float VoxelSize = 100.0f;

    UPROPERTY()
    float TerrainSurfaceZOffset = 50.0f;

    UPROPERTY()
    int32 SeaLevelVoxels = 10;

    // ── v2 terrain height fields (single-source-of-truth rewrite) ──
    // Tile.Elevation (0–1) is the primary input for terrain shape.
    UPROPERTY()
    float WaterThreshold = 0.48f;

    UPROPERTY()
    int32 OceanDepthVoxels = 30;

    UPROPERTY()
    int32 MaxLandHeightVoxels = 150;

    UPROPERTY()
    float LandElevationExponent = 0.65f;

    UPROPERTY()
    bool bAddGlobalFoundation = true;

    UPROPERTY()
    float GlobalFoundationDepthWorld = 5000.0f;

    UPROPERTY()
    float ContinentFrequency = 0.002f;

    UPROPERTY()
    int32 ContinentHeightRangeVoxels = 150;

    UPROPERTY()
    float ContinentExponent = 1.0f;

    UPROPERTY()
    float BasinFrequency = 0.00055f;

    UPROPERTY()
    float BasinThreshold = 0.82f;

    UPROPERTY()
    int32 LakeBasinDepthVoxels = 14;

    UPROPERTY()
    int32 MountainRangeCount = 8;

    UPROPERTY()
    int32 MountainMinLengthTiles = 24;

    UPROPERTY()
    int32 MountainMaxLengthTiles = 80;

    UPROPERTY()
    float MountainMinWidthVoxels = 6.0f;

    UPROPERTY()
    float MountainMaxWidthVoxels = 18.0f;

    UPROPERTY()
    float MountainMinHeightVoxels = 12.0f;

    UPROPERTY()
    float MountainMaxHeightVoxels = 42.0f;

    UPROPERTY()
    float MountainFalloffPower = 2.2f;

    // ── v2.1 Mountain ridge detail layer ──
    UPROPERTY()
    float MountainRidgeHeightScale = 40.0f;

    UPROPERTY()
    float MountainRidgeWidthScale = 0.8f;

    UPROPERTY()
    float MountainCragFrequency = 0.06f;

    UPROPERTY()
    float MountainCragAmplitude = 6.0f;

    UPROPERTY()
    TArray<FMountainRange> PipelineMountainRanges;

    // ── v3.0 Volcano primitives ──
    UPROPERTY()
    TArray<FVolcanoData> PipelineVolcanoes;

    // ── v4.0: 3D SDF Worm Cave Parameters ──
    UPROPERTY()
    float CaveWormFrequency = 0.012f;

    UPROPERTY()
    float CaveWormThreshold = 0.35f;

    UPROPERTY()
    float CaveCarveStrength = 18.0f;

    UPROPERTY()
    float CaveDepthFadeStart = 8.0f;

    UPROPERTY()
    float CaveDepthFadeEnd = 128.0f;

    // ── v4.0: 3D SDF Canyon Parameters ──
    UPROPERTY()
    float CanyonRidgeFrequency = 0.004f;

    UPROPERTY()
    float CanyonDomainWarpStrength = 200.0f;

    UPROPERTY()
    float CanyonDomainWarpFrequency = 0.003f;

    UPROPERTY()
    float CanyonMaxDepthVoxels = 40.0f;

    // ── v4.0: Lava Tube Sub-Layer ──
    UPROPERTY()
    bool bEnableLavaTubes = true;

    UPROPERTY()
    float LavaTubeZCompress = 0.3f;

    UPROPERTY()
    float RiverFrequency = 0.001f;

    UPROPERTY()
    float RiverThreshold = 0.95f;

    UPROPERTY()
    int32 RiverCarveDepthVoxels = 6;

    UPROPERTY()
    float RiverSharpness = 1.5f;

    UPROPERTY()
    float SandMaxElevationNormalized = 0.35f;

    UPROPERTY()
    float DirtMinElevationNormalized = 0.20f;

    UPROPERTY()
    float RockMinElevationNormalized = 0.75f;

    UPROPERTY()
    float HighRockMinElevationNormalized = 0.42f;

    UPROPERTY()
    float SnowMinElevationNormalized = 0.90f;

    UPROPERTY()
    float CoastBandVoxels = 2.0f;

    UPROPERTY()
    float DirtSlopeThresholdDegrees = 8.0f;

    UPROPERTY()
    float RockSlopeThresholdDegrees = 12.0f;

    UPROPERTY()
    float CliffSlopeThresholdDegrees = 18.0f;

    UPROPERTY()
    float NormalizedElevationMaxExtraVoxels = 24.0f;

    UPROPERTY()
    float BaseHeight = 4000.0f;

    UPROPERTY()
    float HeightMultiplier = 6000.0f;

    UPROPERTY()
    float ContinentScale = 0.00005f;

    UPROPERTY()
    float DetailScale = 0.002f;

    UPROPERTY()
    float DetailAmplitude = 120.0f;

    UPROPERTY()
    int32 DetailOctaves = 3;

    UPROPERTY()
    bool bEnableCaves = true;

    UPROPERTY()
    float CaveFrequency = 0.01f;

    UPROPERTY()
    float CaveThreshold = 0.45f;

    UPROPERTY()
    float CaveSecondOctaveMultiplier = 2.0f;

    UPROPERTY()
    float CavePrimaryWeight = 0.7f;

    UPROPERTY()
    float CaveSecondaryWeight = 0.3f;

    UPROPERTY()
    bool bUseFastTerrainSampling = false;

    UPROPERTY()
    TArray<FWorldTileData> WorldTiles;

    UPROPERTY()
    TArray<float> CachedTileElevations;

    UPROPERTY()
    TArray<FVoxelBiomeProfile> CachedVoxelProfiles;

    UPROPERTY()
    TArray<FGameplayTag> CachedBiomeTags;

    UPROPERTY()
    TArray<float> CachedSurfaceHeightsVoxels;

   

    
    // ─────────────────────────────────────────────────────────────
    // Phase 1 Digging: persistent terrain edit mask
    // ─────────────────────────────────────────────────────────────
    // Generator-driven terrain is rebuilt from this snapshot. Without this
    // mask, dug blocks visually regenerate because GetValueImpl only sees the
    // original terrain height/cave field.
    
    UPROPERTY()
    TSet<FIntVector> RemovedTerrainBlocks;

public:
    bool IsValid() const
    {
        if (GridWidth <= 0 || GridHeight <= 0)
        {
            return false;
        }

        if (ChunkSizeVoxels <= 0)
        {
            return false;
        }

        if (VoxelSize <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        const int32 ExpectedTileCount = GridWidth * GridHeight;
        return WorldTiles.Num() == ExpectedTileCount;
    }

    int32 NumTiles() const
    {
        return GridWidth * GridHeight;
    }

    bool IsValidCell(const FIntPoint& Cell) const
    {
        return Cell.X >= 0
            && Cell.Y >= 0
            && Cell.X < GridWidth
            && Cell.Y < GridHeight;
    }

    int32 TileIndex(const FIntPoint& Cell) const
    {
        return (Cell.Y * GridWidth) + Cell.X;
    }

    FIntPoint ClampCell(const FIntPoint& Cell) const
    {
        return FIntPoint(
            FMath::Clamp(Cell.X, 0, FMath::Max(0, GridWidth - 1)),
            FMath::Clamp(Cell.Y, 0, FMath::Max(0, GridHeight - 1))
        );
    }

    const FWorldTileData* GetTile(const FIntPoint& Cell) const
    {
        if (!IsValidCell(Cell))
        {
            return nullptr;
        }

        const int32 Index = TileIndex(Cell);
        return WorldTiles.IsValidIndex(Index) ? &WorldTiles[Index] : nullptr;
    }

    float GetTileElevation(const FIntPoint& Cell, float DefaultValue = 0.0f) const
    {
        if (!IsValidCell(Cell))
        {
            return DefaultValue;
        }

        const int32 Index = TileIndex(Cell);

        if (CachedTileElevations.IsValidIndex(Index))
        {
            return CachedTileElevations[Index];
        }

        if (WorldTiles.IsValidIndex(Index))
        {
            return WorldTiles[Index].Elevation;
        }

        return DefaultValue;
    }

    const FVoxelBiomeProfile* GetVoxelProfile(const FIntPoint& Cell) const
    {
        if (!IsValidCell(Cell))
        {
            return nullptr;
        }

        const int32 Index = TileIndex(Cell);

        if (CachedVoxelProfiles.IsValidIndex(Index))
        {
            return &CachedVoxelProfiles[Index];
        }

        if (WorldTiles.IsValidIndex(Index))
        {
            return &WorldTiles[Index].VoxelProfile;
        }

        return nullptr;
    }

    FGameplayTag GetBiomeTag(const FIntPoint& Cell) const
    {
        if (!IsValidCell(Cell))
        {
            return FGameplayTag();
        }

        const int32 Index = TileIndex(Cell);

        if (CachedBiomeTags.IsValidIndex(Index))
        {
            return CachedBiomeTags[Index];
        }

        if (WorldTiles.IsValidIndex(Index))
        {
            return WorldTiles[Index].BiomeType;
        }

        return FGameplayTag();
    }

    

    int32 GetFoundationDepthVoxels() const
    {
        const float SafeVoxelSize = VoxelSize > KINDA_SMALL_NUMBER ? VoxelSize : 100.0f;
        return FMath::Max(8, FMath::RoundToInt(GlobalFoundationDepthWorld / SafeVoxelSize));
    }

    bool IsTerrainBlockRemoved(const FIntVector& GridPos) const
    {
        return RemovedTerrainBlocks.Contains(GridPos);
    }

    void MarkTerrainBlockRemoved(const FIntVector& GridPos)
    {
        RemovedTerrainBlocks.Add(GridPos);
    }

    void UnmarkTerrainBlockRemoved(const FIntVector& GridPos)
    {
        RemovedTerrainBlocks.Remove(GridPos);
    }
};