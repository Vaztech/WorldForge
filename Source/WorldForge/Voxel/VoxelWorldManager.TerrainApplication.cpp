#include "VoxelWorldManager.h"

#include "VoxelWorld.h"

bool UVoxelWorldManager::ApplyBiomeChunkNative(
    AVoxelWorld* VoxelWorld,
    FIntPoint ChunkCoord,
    const FVoxelBiomeProfile& Profile,
    float TileElevation)
{
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: VoxelWorld is null.")
        );
        return false;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: VoxelWorld runtime is not created yet.")
        );
        return false;
    }

    if (!HasTerrainSnapshot())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: no valid terrain snapshot is available.")
        );
        return false;
    }

    if (!ValidateSmoothSDF(Profile))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: biome profile is not Smooth SDF.")
        );
        return false;
    }

    if (bLogTerrainGeneration && ((ChunkCoord.X + ChunkCoord.Y) % 64 == 0))
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[VoxelWorldManager] ApplyBiomeChunkNative is now generator-driven. Chunk=(%d,%d) Elev=%.3f"),
            ChunkCoord.X,
            ChunkCoord.Y,
            TileElevation
        );
    }

    // Terrain is no longer written here.
    // Density generation is handled by UWorldForgeTerrainGenerator through the voxel world generator path.
    return true;
}

bool UVoxelWorldManager::ApplyGlobalFoundationNative(AVoxelWorld* VoxelWorld, int32 GridWidth, int32 GridHeight)
{
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative skipped: VoxelWorld is null.")
        );
        return false;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative skipped: VoxelWorld runtime is not created yet.")
        );
        return false;
    }

    if (!HasTerrainSnapshot())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative skipped: no valid terrain snapshot is available.")
        );
        return false;
    }

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative is now generator-driven. Grid=%dx%d"),
            GridWidth,
            GridHeight
        );
    }

    // Foundation is no longer written as box voxels.
    // It is part of the density field in UWorldForgeTerrainGenerator::GetValueImpl().
    return true;
}