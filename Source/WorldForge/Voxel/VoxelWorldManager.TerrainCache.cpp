#include "VoxelWorldManager.h"

#include "GameplayTagContainer.h"

namespace
{
    static FGameplayTag GetBiomeCoastalTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
    }

    static FGameplayTag GetBiomeLavaTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
    }
}

void UVoxelWorldManager::CacheTerrainMetadata(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight)
{
    CachedTerrainGridWidth = GridWidth;
    CachedTerrainGridHeight = GridHeight;

    CachedSurfaceHeightsVoxels.SetNumZeroed(GridWidth * GridHeight);
    CachedVoxelProfiles.SetNum(GridWidth * GridHeight);
    CachedBiomeTags.SetNum(GridWidth * GridHeight);
    CachedTileElevations.SetNumZeroed(GridWidth * GridHeight);

    // Pass 1: cache source data
    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = (Y * GridWidth) + X;
            const FWorldTileData& Tile = WorldTiles[Index];

            CachedTileElevations[Index] = FMath::Clamp(Tile.Elevation, 0.0f, 1.0f);
            CachedVoxelProfiles[Index] = Tile.VoxelProfile;
            CachedBiomeTags[Index] = Tile.BiomeType;
        }
    }

    // Pass 2: compute heights
    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = (Y * GridWidth) + X;

            CachedSurfaceHeightsVoxels[Index] = static_cast<float>(
                ComputeChunkSurfaceHeightVoxels(
                    FIntPoint(X, Y),
                    CachedVoxelProfiles[Index],
                    CachedTileElevations[Index]
                )
                );
        }
    }

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Cached terrain metadata. Cells=%d Grid=%dx%d"),
            CachedSurfaceHeightsVoxels.Num(),
            CachedTerrainGridWidth,
            CachedTerrainGridHeight
        );
    }
}

void UVoxelWorldManager::ClearTerrainCache()
{
    CachedSurfaceHeightsVoxels.Reset();
    CachedVoxelProfiles.Reset();
    CachedBiomeTags.Reset();
    CachedTileElevations.Reset();

    CachedTerrainGridWidth = 0;
    CachedTerrainGridHeight = 0;

    bTerrainReadyBroadcasted = false;
}

bool UVoxelWorldManager::IsValidTerrainCell(FIntPoint GridCell) const
{
    return
        GridCell.X >= 0 &&
        GridCell.Y >= 0 &&
        GridCell.X < CachedTerrainGridWidth &&
        GridCell.Y < CachedTerrainGridHeight;
}

FIntPoint UVoxelWorldManager::ClampTerrainCellToBounds(FIntPoint GridCell) const
{
    if (CachedTerrainGridWidth <= 0 || CachedTerrainGridHeight <= 0)
    {
        return GridCell;
    }

    return FIntPoint(
        FMath::Clamp(GridCell.X, 0, CachedTerrainGridWidth - 1),
        FMath::Clamp(GridCell.Y, 0, CachedTerrainGridHeight - 1)
    );
}

int32 UVoxelWorldManager::TerrainIndex(FIntPoint GridCell) const
{
    return (GridCell.Y * CachedTerrainGridWidth) + GridCell.X;
}

bool UVoxelWorldManager::HasCachedTerrainData() const
{
    return
        CachedTerrainGridWidth > 0 &&
        CachedTerrainGridHeight > 0 &&
        CachedSurfaceHeightsVoxels.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedVoxelProfiles.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedBiomeTags.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedTileElevations.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight);
}

bool UVoxelWorldManager::IsSpawnableTerrainCell(FIntPoint GridCell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(GridCell);

    if (!CachedBiomeTags.IsValidIndex(Index) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    const FGameplayTag& BiomeTag = CachedBiomeTags[Index];
    const FGameplayTag CoastalTag = GetBiomeCoastalTag();
    const FGameplayTag LavaTag = GetBiomeLavaTag();

    if (BiomeTag.MatchesTag(CoastalTag) || BiomeTag.MatchesTag(LavaTag))
    {
        return false;
    }

    return CachedSurfaceHeightsVoxels[Index] > 0.0f;
}

bool UVoxelWorldManager::FindNearestSpawnableTerrainCell(
    FIntPoint StartCell,
    FIntPoint& OutCell,
    int32 MaxSearchRadius) const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    if (IsSpawnableTerrainCell(StartCell))
    {
        OutCell = StartCell;
        return true;
    }

    const int32 RadiusMax = FMath::Max(1, MaxSearchRadius);

    for (int32 Radius = 1; Radius <= RadiusMax; ++Radius)
    {
        for (int32 Y = StartCell.Y - Radius; Y <= StartCell.Y + Radius; ++Y)
        {
            for (int32 X = StartCell.X - Radius; X <= StartCell.X + Radius; ++X)
            {
                const bool bOnRing =
                    (X == StartCell.X - Radius) ||
                    (X == StartCell.X + Radius) ||
                    (Y == StartCell.Y - Radius) ||
                    (Y == StartCell.Y + Radius);

                if (!bOnRing)
                {
                    continue;
                }

                const FIntPoint Candidate(X, Y);

                if (IsSpawnableTerrainCell(Candidate))
                {
                    OutCell = Candidate;
                    return true;
                }
            }
        }
    }

    return false;
}