#pragma once

#include "CoreMinimal.h"
#include "Data/WorldForgeDataTypes.h"

/**
 * Immutable terrain-domain snapshot used as the handoff between
 * UVoxelWorldManager orchestration and future density-based voxel generation.
 *
 * This file is intentionally plugin-agnostic:
 * - no AVoxelWorld include
 * - no voxel generator include
 * - no edit tool include
 *
 * It is safe to introduce before replacing AddBox terrain writes.
 */
struct FWorldForgeTerrainSnapshot
{
    int32 GridWidth = 0;
    int32 GridHeight = 0;

    int32 ChunkSizeVoxels = 0;
    float VoxelSize = 100.0f;

    int32 SeaLevelVoxels = 0;
    bool bAddGlobalFoundation = true;
    float GlobalFoundationDepthWorld = 5000.0f;

    float ContinentFrequency = 0.00018f;
    int32 ContinentHeightRangeVoxels = 90;
    float ContinentExponent = 2.2f;

    float BasinFrequency = 0.00055f;
    float BasinThreshold = 0.82f;
    int32 LakeBasinDepthVoxels = 14;

    float RiverFrequency = 0.0015f;
    float RiverThreshold = 0.92f;
    int32 RiverCarveDepthVoxels = 5;
    float RiverSharpness = 2.2f;

    bool bUseFastTerrainSampling = true;

    TArray<FWorldTileData> WorldTiles;
    TArray<float> CachedTileElevations;
    TArray<FVoxelBiomeProfile> CachedVoxelProfiles;
    TArray<FGameplayTag> CachedBiomeTags;
    TArray<float> CachedSurfaceHeightsVoxels;

    bool IsValid() const
    {
        if (GridWidth <= 0 || GridHeight <= 0)
        {
            return false;
        }

        const int32 ExpectedTileCount = GridWidth * GridHeight;
        if (WorldTiles.Num() != ExpectedTileCount)
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

        return true;
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
};