#pragma once

#include "CoreMinimal.h"
#include "WorldForgeTerrainSnapshot.h"

// ════════════════════════════════════════════════════════════════════
// FWorldForgeTerrainMath — REWRITE v2
//
// Single source of truth: Tile.Elevation (0–1) determines ALL terrain
// shape.  No secondary mountain/basin/river height systems.
// The voxel generator is a pure renderer of this data.
// ════════════════════════════════════════════════════════════════════

struct FWorldForgeTerrainMath
{
    // Converts a 0–1 tile elevation to a voxel-space height.
    // Below WaterThreshold → ocean floor.  Above → land surface.
    static float ElevationToVoxelHeight(
        float Elevation01,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Bilinearly interpolates tile elevation at voxel resolution,
    // then converts to voxel height.  Primary height function.
    static float ComputeSurfaceHeight(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Cache path — tile elevation already known.
    static float ComputeSurfaceHeightFromTileElevation(
        float TileElevation01,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Returns 0.0 at sea level, 1.0 at MaxLandHeightVoxels.
    static float ComputeNormalizedLandElevation(
        float SurfaceHeight,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Bilinear tile elevation sampler (voxel XY → interpolated 0–1).
    static float SampleTileElevationBilinear(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot);
};
