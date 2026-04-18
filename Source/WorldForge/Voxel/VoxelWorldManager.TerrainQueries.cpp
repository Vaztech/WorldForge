#include "VoxelWorldManager.h"

bool UVoxelWorldManager::TryGetSurfaceWorldLocation(
    FIntPoint GridCell,
    FVector& OutWorldLocation,
    float ZOffset) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(GridCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    const float SurfaceHeightWorld =
        (CachedSurfaceHeightsVoxels[Index] * GetRenderVoxelSize()) + ZOffset;

    OutWorldLocation = TerrainCellToWorld(GridCell, SurfaceHeightWorld, true);
    return true;
}

bool UVoxelWorldManager::TryGetCachedSurfaceHeightWorldForVoxelCell(
    FIntPoint VoxelCell,
    float& OutSurfaceHeightWorld) const
{
    OutSurfaceHeightWorld = 0.0f;

    if (!HasCachedTerrainData())
    {
        return false;
    }

    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelCell);
    if (!IsValidTerrainCell(ChunkCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(ChunkCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    OutSurfaceHeightWorld = CachedSurfaceHeightsVoxels[Index] * GetRenderVoxelSize();
    return true;
}