#include "Utility/WorldForgeTerrainMath.h"

// ════════════════════════════════════════════════════════════════════
// WorldForgeTerrainMath — REWRITE v2
//
// Design principles (from GDD rewrite prompt):
//   1. Tile.Elevation (0–1) is the ONLY source of terrain shape.
//   2. No separate mountain, basin, or river height layers.
//   3. One formula, one file, one truth.
//   4. Voxel system is a pure renderer of this data.
//
// The pipeline (Step02 + Step03) encodes ALL geographic features
// — continents, mountains, valleys, ridges — into Tile.Elevation.
// This file converts that single 0–1 value into world-space voxel
// heights via a clean, non-linear remapping.
// ════════════════════════════════════════════════════════════════════

namespace
{
    static float WF_Bilerp(float V00, float V10, float V01, float V11, float FX, float FY)
    {
        const float X0 = FMath::Lerp(V00, V10, FX);
        const float X1 = FMath::Lerp(V01, V11, FX);
        return FMath::Lerp(X0, X1, FY);
    }
}

// ─── Core elevation → height conversion ─────────────────────────
//
// Tile elevation 0.0 → deep ocean floor
// Tile elevation WaterThreshold (~0.48) → sea level
// Tile elevation 1.0 → mountain peak
//
// Land uses a power curve (LandElevationExponent < 1.0) to expand
// the lowland range and compress peaks, giving most land a gentle
// terrain with rare dramatic mountains — exactly what the GDD wants.

float FWorldForgeTerrainMath::ElevationToVoxelHeight(
    float Elevation01,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float Elev = FMath::Clamp(Elevation01, 0.0f, 1.0f);
    const float SeaLevel = static_cast<float>(Snapshot.SeaLevelVoxels);
    const float WaterThreshold = FMath::Clamp(Snapshot.WaterThreshold, 0.05f, 0.95f);

    if (Elev <= WaterThreshold)
    {
        // ── Underwater: [0, WaterThreshold] → [SeaLevel - OceanDepth, SeaLevel]
        const float T = Elev / WaterThreshold;
        const float OceanDepth = static_cast<float>(FMath::Max(1, Snapshot.OceanDepthVoxels));
        return FMath::Lerp(SeaLevel - OceanDepth, SeaLevel, T);
    }
    else
    {
        // ── Land: [WaterThreshold, 1.0] → [SeaLevel, SeaLevel + MaxLandHeight]
        const float LandRange = 1.0f - WaterThreshold;
        const float T = (Elev - WaterThreshold) / FMath::Max(0.001f, LandRange);

        // Power curve: exponent < 1.0 expands lowlands, compresses peaks.
        // At 0.65, a tile at mid-elevation (0.65) maps to ~0.34 normalized,
        // keeping most land in the gentle grass/dirt range.
        const float Exponent = FMath::Max(0.1f, Snapshot.LandElevationExponent);
        const float Shaped = FMath::Pow(FMath::Clamp(T, 0.0f, 1.0f), Exponent);

        const float MaxLandHeight = static_cast<float>(FMath::Max(1, Snapshot.MaxLandHeightVoxels));
        return SeaLevel + Shaped * MaxLandHeight;
    }
}

// ─── Bilinear tile elevation sampler ─────────────────────────────

float FWorldForgeTerrainMath::SampleTileElevationBilinear(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

    // Convert voxel coords to tile-grid coords (0-based)
    const float CellX = (VoxelX / ChunkSize) + HalfGridX;
    const float CellY = (VoxelY / ChunkSize) + HalfGridY;

    const int32 X0 = FMath::FloorToInt(CellX);
    const int32 Y0 = FMath::FloorToInt(CellY);
    const int32 X1 = X0 + 1;
    const int32 Y1 = Y0 + 1;

    const float FX = CellX - static_cast<float>(X0);
    const float FY = CellY - static_cast<float>(Y0);

    const FIntPoint C00 = Snapshot.ClampCell(FIntPoint(X0, Y0));
    const FIntPoint C10 = Snapshot.ClampCell(FIntPoint(X1, Y0));
    const FIntPoint C01 = Snapshot.ClampCell(FIntPoint(X0, Y1));
    const FIntPoint C11 = Snapshot.ClampCell(FIntPoint(X1, Y1));

    const float E00 = Snapshot.GetTileElevation(C00, 0.0f);
    const float E10 = Snapshot.GetTileElevation(C10, 0.0f);
    const float E01 = Snapshot.GetTileElevation(C01, 0.0f);
    const float E11 = Snapshot.GetTileElevation(C11, 0.0f);

    return WF_Bilerp(E00, E10, E01, E11, FX, FY);
}

// ─── Primary surface height (voxel resolution) ──────────────────

float FWorldForgeTerrainMath::ComputeSurfaceHeight(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float Elevation01 = SampleTileElevationBilinear(VoxelX, VoxelY, Snapshot);
    return ElevationToVoxelHeight(Elevation01, Snapshot);
}

// ─── Cache path (tile elevation already known) ──────────────────

float FWorldForgeTerrainMath::ComputeSurfaceHeightFromTileElevation(
    float TileElevation01,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    return ElevationToVoxelHeight(TileElevation01, Snapshot);
}

// ─── Normalized land elevation (for material classification) ────

float FWorldForgeTerrainMath::ComputeNormalizedLandElevation(
    float SurfaceHeight,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float SeaLevel = static_cast<float>(Snapshot.SeaLevelVoxels);
    const float MaxLandHeight = static_cast<float>(FMath::Max(1, Snapshot.MaxLandHeightVoxels));

    if (SurfaceHeight <= SeaLevel)
    {
        return 0.0f;
    }

    return FMath::Clamp((SurfaceHeight - SeaLevel) / MaxLandHeight, 0.0f, 1.0f);
}
