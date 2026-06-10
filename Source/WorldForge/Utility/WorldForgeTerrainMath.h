#pragma once

#include "CoreMinimal.h"
#include "WorldForgeTerrainSnapshot.h"

// ════════════════════════════════════════════════════════════════════
// FWorldForgeTerrainMath — v3.0
//
// Single source of truth: Tile.Elevation (0–1) determines MACRO
// terrain shape.  PipelineMountainRanges add LOCALIZED ridge detail
// on top — narrow, jagged displacements along Step03 tectonic lines.
// PipelineVolcanoes add composite cone+crater displacement.
//
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
    // then converts to voxel height.  Primary macro height function.
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

    // Returns the distance in tile units from the sample point to the
    // nearest ocean/sea/coastal water tile used by shoreline shaping.
    // Returns -1.0f when no eligible coastal water tile is nearby.
    static float SampleCoastalWaterDistanceTiles(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Applies a conservative shoreline feather to ordinary coast and inlet
    // land, while preserving lava, volcanic, mountain, and canyon tiles.
    static float ApplyCoastalShorelineShaping(
        float VoxelX,
        float VoxelY,
        float MacroHeight,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Samples coast-adjacent terrain columns and logs shoreline shaping
    // diagnostics using the current snapshot tuning.
    static void LogCoastDiagnostics(
        const FWorldForgeTerrainSnapshot& Snapshot);

    // ── v2.1: Mountain ridge detail layer ────────────────────────
    // Samples all PipelineMountainRanges and returns additive voxel
    // height from the closest ridge.  Returns 0 when no range is
    // nearby.  Includes high-frequency crag noise for jagged peaks.
    //
    // This is ONLY called on land voxels — never underwater.
    // The result is added AFTER the macro height, not inside it.
    static float SampleMountainRidgeHeight(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // ── v3.0: Volcano composite height layer ────────────────────
    // Samples all PipelineVolcanoes and returns additive voxel height
    // from the nearest volcano cone+crater composite.  Returns 0
    // when no volcano is nearby.  Only called on land voxels.
    static float SampleVolcanoHeight(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // ── v4.0: Canyon ridge noise ────────────────────────────────
    // Domain-warped ridge noise that defines canyon carve paths.
    // Returns a 0–1 ridge value: 1.0 at the canyon center, 0.0 away.
    // The caller uses this to compute carve depth at each voxel.
    static float SampleCanyonRidgeNoise(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // ── LAKE-FIX-01A: Lake basin shaping ─────────────────────────
    // Bilinearly interpolates Tile.WaterDepth at voxel resolution.
    // Returns 0.0 on land tiles, ~0.15–0.25 at lake centers.
    static float SampleTileWaterDepthBilinear(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Shapes accepted lake basins using the descriptor metadata built
    // after Step04. Uses a shallow elliptical core, an outer shoreline
    // feather ring, and a clamped floor depth so the rendered terrain
    // stays visually aligned with lightweight lake surfaces.
    // Returns MacroHeight unchanged when no accepted basin influences
    // the sample point.
    static float ApplyLakeBasinShaping(
        float VoxelX,
        float VoxelY,
        float MacroHeight,
        const FWorldForgeTerrainSnapshot& Snapshot);

    // Applies shallow river channel shaping from lightweight RiverSurfaceDescs.
    // This is intentionally conservative: enough to visually seat the river
    // surfaces without introducing deep trenches or a full hydrology model.
    static float ApplyRiverChannelShaping(
        float VoxelX,
        float VoxelY,
        float MacroHeight,
        const FWorldForgeTerrainSnapshot& Snapshot);
};
