#include "Utility/WorldForgeTerrainMath.h"

// ════════════════════════════════════════════════════════════════════
// WorldForgeTerrainMath — v3.0
//
// Design principles:
//   1. Tile.Elevation (0–1) is the ONLY source of MACRO terrain shape.
//   2. PipelineMountainRanges add LOCALIZED ridge displacement on top.
//   3. PipelineVolcanoes add composite cone+crater displacement on top.
//   4. Ridge detail is narrow, sharp, and jagged — not blobs.
//   5. Voxel system is a pure renderer of this data.
//
// The pipeline (Step02 + Step03) encodes continents and general
// elevation into Tile.Elevation.  Step03 also stores mountain range
// line segments and volcano primitives.  This file converts all of
// them into world-space voxel heights: a smooth macro layer plus
// sharp ridge detail plus volcanic cone geometry.
// ════════════════════════════════════════════════════════════════════

namespace
{
    static float WF_Bilerp(float V00, float V10, float V01, float V11, float FX, float FY)
    {
        const float X0 = FMath::Lerp(V00, V10, FX);
        const float X1 = FMath::Lerp(V01, V11, FX);
        return FMath::Lerp(X0, X1, FY);
    }

    // Simple 2D hash-based noise for crag detail.
    // Avoids pulling in heavy noise libraries for a small overlay.
    static float WF_CragNoise2D(float X, float Y)
    {
        return FMath::PerlinNoise2D(FVector2D(X, Y));
    }

    // Distance from point P to line segment (A → B).
    // Returns the minimum distance and writes the parametric T (0–1) along AB.
    static float PointToSegmentDistance2D(
        const FVector2D& P,
        const FVector2D& A,
        const FVector2D& B,
        float& OutT)
    {
        const FVector2D AB = B - A;
        const float ABLenSq = AB.SizeSquared();

        if (ABLenSq <= KINDA_SMALL_NUMBER)
        {
            OutT = 0.0f;
            return FVector2D::Distance(P, A);
        }

        OutT = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / ABLenSq, 0.0f, 1.0f);
        const FVector2D ClosestPoint = A + AB * OutT;
        return FVector2D::Distance(P, ClosestPoint);
    }
}

// ─── Core elevation → height conversion ─────────────────────────

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

// ─── Mountain ridge detail layer (v2.1) ─────────────────────────
//
// Iterates all PipelineMountainRanges from Step03 and computes
// additive voxel displacement.  Each range is a line segment in
// tile-space (Start → Start + Direction * Length).
//
// Produces:
//   • Narrow ridge spines with steep power-curve falloff
//   • High-frequency crag noise for jagged broken profiles
//   • Smooth fade-in/fade-out at range endpoints
//
// This is ADDITIVE — it goes on top of the macro height, never
// replaces it.  It's only called on land voxels.

float FWorldForgeTerrainMath::SampleMountainRidgeHeight(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (Snapshot.PipelineMountainRanges.Num() == 0)
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

    // Convert voxel position to tile-space coordinates
    const float TileX = (VoxelX / ChunkSize) + HalfGridX;
    const float TileY = (VoxelY / ChunkSize) + HalfGridY;
    const FVector2D Point(TileX, TileY);

    // Tuning knobs from snapshot
    const float FalloffPower = FMath::Max(0.5f, Snapshot.MountainFalloffPower);
    const float RidgeHeightScale = FMath::Max(0.0f, Snapshot.MountainRidgeHeightScale);
    const float RidgeWidthScale = FMath::Max(0.1f, Snapshot.MountainRidgeWidthScale);
    const float CragFreq = FMath::Max(0.001f, Snapshot.MountainCragFrequency);
    const float CragAmp = FMath::Max(0.0f, Snapshot.MountainCragAmplitude);

    float MaxRidgeHeight = 0.0f;

    for (const FMountainRange& Range : Snapshot.PipelineMountainRanges)
    {
        // Range spine: Start → Start + Direction * Length (tile coords)
        const FVector2D RangeStart = Range.Start;
        const FVector2D RangeEnd = RangeStart + Range.Direction * FMath::Max(1.0f, Range.Length);

        // Distance from this voxel to the range spine (in tile units)
        float T = 0.0f;
        const float DistTiles = PointToSegmentDistance2D(Point, RangeStart, RangeEnd, T);

        // Width in tile units — Range.Width is already in tile units from Step03
        const float EffectiveWidthTiles = FMath::Max(1.0f, Range.Width * RidgeWidthScale);

        // Skip if outside the range's influence radius
        if (DistTiles >= EffectiveWidthTiles)
        {
            continue;
        }

        // ── Radial falloff: 1.0 at spine, 0.0 at edge ──
        const float NormalizedDist = DistTiles / EffectiveWidthTiles;
        const float RadialFalloff = FMath::Pow(
            1.0f - FMath::Clamp(NormalizedDist, 0.0f, 1.0f),
            FalloffPower
        );

        // ── Endpoint fade: smooth taper at both ends of the range ──
        const float EndFadeIn = FMath::Clamp(T / 0.08f, 0.0f, 1.0f);
        const float EndFadeOut = FMath::Clamp((1.0f - T) / 0.08f, 0.0f, 1.0f);
        const float LengthMask = FMath::Min(EndFadeIn, EndFadeOut);

        // ── Ridge height from Step03 data ──
        // HeightMultiplier from Step03 is already a meaningful scale factor.
        // RidgeHeightScale lets the player tune overall mountain drama.
        const float BaseRidgeVoxels = Range.HeightMultiplier * RidgeHeightScale;

        // ── Crag noise: high-frequency jaggedness along the spine ──
        // Only significant near the ridge spine (RadialFalloff already handles this).
        // Use voxel coordinates for sub-tile variation.
        const float CragNoise = WF_CragNoise2D(
            VoxelX * CragFreq + 317.0f,
            VoxelY * CragFreq + 519.0f
        );

        // Crag contribution: signed noise scaled by amplitude and spine proximity
        const float CragContrib = CragNoise * CragAmp * RadialFalloff;

        // ── Final ridge contribution for this range ──
        const float RidgeContrib = (BaseRidgeVoxels * RadialFalloff * LengthMask) + CragContrib;

        // Take the maximum contribution from all overlapping ranges
        MaxRidgeHeight = FMath::Max(MaxRidgeHeight, FMath::Max(0.0f, RidgeContrib));
    }

    return MaxRidgeHeight;
}

// ─── Volcano composite height (v3.0) ────────────────────────────
//
// Each volcano is a cone with a crater carved from the top.
// The tile-space geometry from Step03 is converted to voxel-space
// and sampled at sub-tile resolution.

float FWorldForgeTerrainMath::SampleVolcanoHeight(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (Snapshot.PipelineVolcanoes.Num() == 0)
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

    // Convert voxel position to tile-space
    const float TileX = (VoxelX / ChunkSize) + HalfGridX;
    const float TileY = (VoxelY / ChunkSize) + HalfGridY;

    float MaxContrib = 0.0f;

    for (const FVolcanoData& V : Snapshot.PipelineVolcanoes)
    {
        const float DX = TileX - V.Center.X;
        const float DY = TileY - V.Center.Y;
        const float Dist = FMath::Sqrt(DX * DX + DY * DY);

        if (Dist >= V.ConeRadius)
        {
            continue;
        }

        // Cone: steep radial gradient, peak at center
        const float NormDist = Dist / V.ConeRadius;
        const float ConeElev = FMath::Pow(
            1.0f - FMath::Clamp(NormDist, 0.0f, 1.0f),
            V.ConeSteepness);

        // Crater: inverted bowl subtracted from center
        float CraterCarve = 0.0f;
        const float CraterRadius = V.ConeRadius * V.CraterRadiusFraction;
        if (Dist < CraterRadius && CraterRadius > KINDA_SMALL_NUMBER)
        {
            const float CraterNorm = Dist / CraterRadius;
            CraterCarve = (1.0f - CraterNorm * CraterNorm) * V.CraterDepthFraction;
        }

        // Convert 0-1 contribution to voxel height using MaxLandHeightVoxels
        const float HeightScale = V.ConeHeight *
            static_cast<float>(FMath::Max(1, Snapshot.MaxLandHeightVoxels));
        const float Contribution = (ConeElev - CraterCarve) * HeightScale;

        MaxContrib = FMath::Max(MaxContrib, FMath::Max(0.0f, Contribution));
    }

    return MaxContrib;
}

// ─── Canyon ridge noise (v4.0) ──────────────────────────────────
//
// Domain-warped ridge noise for canyon carving paths.
// Ridge noise = 1.0 - abs(Perlin), which produces narrow bands of
// high values along zero-crossings of the underlying noise.
// Domain warping feeds noise back into the coordinates, making
// the canyon paths meander naturally instead of following straight
// lines.  The result is a 0–1 ridge value.

float FWorldForgeTerrainMath::SampleCanyonRidgeNoise(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float RidgeFreq = FMath::Max(0.0001f, Snapshot.CanyonRidgeFrequency);
    const float WarpStrength = FMath::Max(0.0f, Snapshot.CanyonDomainWarpStrength);
    const float WarpFreq = FMath::Max(0.0001f, Snapshot.CanyonDomainWarpFrequency);

    // Domain warp: offset coordinates by a low-frequency noise field
    // to make canyon paths curve and meander.
    const float WarpX = FMath::PerlinNoise2D(FVector2D(
        VoxelX * WarpFreq + 317.0f,
        VoxelY * WarpFreq + 519.0f
    )) * WarpStrength;

    const float WarpY = FMath::PerlinNoise2D(FVector2D(
        VoxelX * WarpFreq + 1013.0f,
        VoxelY * WarpFreq + 2731.0f
    )) * WarpStrength;

    // Sample Perlin at warped coordinates
    const float WarpedX = VoxelX + WarpX;
    const float WarpedY = VoxelY + WarpY;

    const float RawPerlin = FMath::PerlinNoise2D(FVector2D(
        WarpedX * RidgeFreq,
        WarpedY * RidgeFreq
    ));

    // Ridge transform: peaks at zero-crossings of the noise
    const float Ridge = 1.0f - FMath::Abs(RawPerlin);

    return FMath::Clamp(Ridge, 0.0f, 1.0f);
}
