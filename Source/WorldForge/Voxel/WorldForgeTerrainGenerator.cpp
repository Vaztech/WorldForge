// ════════════════════════════════════════════════════════════════════
// WorldForgeTerrainGenerator.cpp — v4.0
//
// 3D SDF LAYERED DENSITY COMPOSITOR
//
// The voxel generator produces a true 3D density field via layered
// composition.  Each layer is independent and additive:
//
//   Layer 1: Base terrain (2D heightmap → density gradient) [existing]
//   Layer 2: Worm cave networks (3D dual-noise intersection)
//   Layer 3: Canyon carving (domain-warped ridge noise)
//   Layer 4: Lava tube sub-layer (horizontal-biased worm caves)
//   Layer 5: Biome density bias
//   Foundation: Solid floor below all terrain [existing]
//
// Positive density = air.  Negative density = solid.
// Voxel Plugin meshes the isosurface at density = 0.
//
// Caves, canyons, overhangs, arched ceilings, and underground
// biomes all emerge naturally from the 3D density field.
// ════════════════════════════════════════════════════════════════════

#include "WorldForgeTerrainGenerator.h"

#include "VoxelMaterialBuilder.h"
#include "Utility/WorldForgeTerrainSettings.h"
#include "Utility/WorldForgeTerrainMath.h"

namespace
{
    static float WF_FractalPerlin2D(float X, float Y, float Frequency, int32 Octaves)
    {
        float Total = 0.0f;
        float Amplitude = 1.0f;
        float SumAmplitude = 0.0f;
        float CurrentFrequency = FMath::Max(KINDA_SMALL_NUMBER, Frequency);

        for (int32 OctaveIndex = 0; OctaveIndex < FMath::Max(1, Octaves); ++OctaveIndex)
        {
            const float Sample = FMath::PerlinNoise2D(FVector2D(X * CurrentFrequency, Y * CurrentFrequency));
            Total += Sample * Amplitude;
            SumAmplitude += Amplitude;

            CurrentFrequency *= 2.0f;
            Amplitude *= 0.5f;
        }

        if (SumAmplitude <= KINDA_SMALL_NUMBER)
        {
            return 0.0f;
        }

        return Total / SumAmplitude;
    }

    static float WF_Perlin3D(float X, float Y, float Z)
    {
        return FMath::PerlinNoise3D(FVector(X, Y, Z));
    }
}

TVoxelSharedRef<FVoxelGeneratorInstance> UWorldForgeTerrainGenerator::GetInstance()
{
    return MakeVoxelShared<FWorldForgeTerrainGeneratorInstance>(*this);
}

FWorldForgeTerrainGeneratorInstance::FWorldForgeTerrainGeneratorInstance(UWorldForgeTerrainGenerator& GeneratorObject)
    : Super(&GeneratorObject)
    , TerrainSnapshot(GeneratorObject.TerrainSnapshot)
    , Seed(GeneratorObject.Seed)
{
}

void FWorldForgeTerrainGeneratorInstance::Init(const FVoxelGeneratorInit& InitStruct)
{
    Noise2D.SetSeed(Seed);
    CaveNoise.SetSeed(Seed + 5000);       // worm field A
    CaveNoiseB.SetSeed(Seed + 9000);      // worm field B
    CanyonNoise.SetSeed(Seed + 13000);    // canyon domain warp
    LavaTubeNoise.SetSeed(Seed + 17000);  // lava tube field A
    LavaTubeNoiseB.SetSeed(Seed + 21000); // lava tube field B

    if (TerrainSnapshot.IsValid())
    {
        const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
        const float ChunkSizeF = static_cast<float>(TerrainSnapshot.ChunkSizeVoxels);
        const float MinVoxel = -HalfGridX * ChunkSizeF;
        const float MaxVoxel = HalfGridX * ChunkSizeF;
        UE_LOG(LogTemp, Log,
            TEXT("[TerrainGen] Init: Grid=%dx%d ChunkSizeVoxels=%d VoxelSize=%.1f "
                 "Expected voxel range=[%.0f..%.0f] (total=%.0f) Volcanoes=%d"),
            TerrainSnapshot.GridWidth, TerrainSnapshot.GridHeight,
            TerrainSnapshot.ChunkSizeVoxels,
            TerrainSnapshot.VoxelSize,
            MinVoxel, MaxVoxel, MaxVoxel - MinVoxel,
            TerrainSnapshot.PipelineVolcanoes.Num());
    }
}

// ═══════════════════════════════════════════════════════════════════
// DENSITY FIELD — v4.0 Layered 3D SDF Compositor
// ═══════════════════════════════════════════════════════════════════

v_flt FWorldForgeTerrainGeneratorInstance::GetValueImpl(
    v_flt X,
    v_flt Y,
    v_flt Z,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    if (!IsSnapshotValid())
    {
        return 1.0f;
    }

// ── DIG MASK: carve a visible SDF hole around removed blocks ──
// Exact integer matching is too small for smooth SDF meshing.
// This creates a small spherical air pocket around the removed voxel.
    constexpr float DigRadiusVoxels = 1.45f;

    for (const FIntVector& RemovedBlock : TerrainSnapshot.RemovedTerrainBlocks)
    {
        const FVector RemovedCenter(
            static_cast<float>(RemovedBlock.X),
            static_cast<float>(RemovedBlock.Y),
            static_cast<float>(RemovedBlock.Z)
        );

        const FVector SamplePos(
            static_cast<float>(X),
            static_cast<float>(Y),
            static_cast<float>(Z)
        );

        const float Dist = FVector::Dist(SamplePos, RemovedCenter);

        if (Dist <= DigRadiusVoxels)
        {
            return DigRadiusVoxels - Dist + 0.25f; // positive = air
        }
    }

    // ── Layer 1: Base terrain (2D heightmap → density gradient) ──
    const float SurfaceHeight = ComputeSurfaceHeight(X, Y);
    float Density = Z - SurfaceHeight;

    // Biome profile for this column
    const FVoxelBiomeProfile* Profile = SamplePrimaryBiomeProfile(X, Y);

    if (Profile)
    {
        // ── Layer 2: Worm cave networks ──
        if (Profile->bEnableCaves && Profile->CaveIntensity > 0.0f)
        {
            Density += ComputeWormCaveDensity(X, Y, Z, SurfaceHeight, *Profile);
        }

        // ── Layer 3: Canyon carving ──
        if (Profile->CanyonIntensity > 0.0f)
        {
            Density += ComputeCanyonCarve(X, Y, Z, SurfaceHeight, *Profile);
        }

        // ── Layer 4: Lava tube sub-layer (inside volcano cones) ──
        if (TerrainSnapshot.bEnableLavaTubes)
        {
            Density += ComputeLavaTubeDensity(X, Y, Z, SurfaceHeight);
        }

        // ── Layer 5: Biome density bias ──
        Density += Profile->DensityBias;
    }

    // ── Foundation: solid floor below all terrain (unchanged) ──
    if (TerrainSnapshot.bAddGlobalFoundation)
    {
        const float FoundationDepth = static_cast<float>(TerrainSnapshot.GetFoundationDepthVoxels());
        const float DistanceToFoundationTop = Z;
        const float DistanceToFoundationBottom = -(Z + FoundationDepth);
        const float FoundationDensity = FMath::Max(DistanceToFoundationTop, DistanceToFoundationBottom);
        Density = FMath::Min(Density, FoundationDensity);
    }

    return Density;
}

// ═══════════════════════════════════════════════════════════════════
// MATERIAL ASSIGNMENT — v4.0 Depth-Stratified Underground
// ═══════════════════════════════════════════════════════════════════

FVoxelMaterial FWorldForgeTerrainGeneratorInstance::GetMaterialImpl(
    v_flt X,
    v_flt Y,
    v_flt Z,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    if (!IsSnapshotValid())
    {
        return FVoxelMaterial::Default();
    }

    const float SurfaceHeight = ComputeSurfaceHeight(X, Y);
    const float DepthBelowSurface = SurfaceHeight - Z;

    // At or above surface — use the surface classification
    if (DepthBelowSurface <= 1.5f)
    {
        const int32 SurfaceLayer = ClassifyTerrainMaterialLayer(X, Y, SurfaceHeight);
        return MakeVoxelMaterialFromLayer(SurfaceLayer);
    }

    // Underground — use depth-stratified material
    return MakeVoxelMaterialFromLayer(ClassifyUndergroundMaterial(X, Y, Z, SurfaceHeight));
}

// ─── Value range (LOD culling) — v4.0 conservative for 3D ─────

TVoxelRange<v_flt> FWorldForgeTerrainGeneratorInstance::GetValueRangeImpl(
    const FVoxelIntBox& Bounds,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    if (!IsSnapshotValid())
    {
        return TVoxelRange<v_flt>(1.0f, 1.0f);
    }

    const float S00 = ComputeSurfaceHeight(Bounds.Min.X, Bounds.Min.Y);
    const float S01 = ComputeSurfaceHeight(Bounds.Min.X, Bounds.Max.Y);
    const float S10 = ComputeSurfaceHeight(Bounds.Max.X, Bounds.Min.Y);
    const float S11 = ComputeSurfaceHeight(Bounds.Max.X, Bounds.Max.Y);

    const float MinSurface = FMath::Min(FMath::Min(S00, S01), FMath::Min(S10, S11));
    const float MaxSurface = FMath::Max(FMath::Max(S00, S01), FMath::Max(S10, S11));

    float MinValue = static_cast<float>(Bounds.Min.Z) - MaxSurface;
    float MaxValue = static_cast<float>(Bounds.Max.Z) - MinSurface;

    // v4.0: Cave/Canyon overlap check
    if (TerrainSnapshot.bEnableCaves)
    {
        const float MaxCaveDepth = FMath::Max(
            TerrainSnapshot.CaveDepthFadeEnd,
            TerrainSnapshot.CanyonMaxDepthVoxels);

        if (static_cast<float>(Bounds.Max.Z) > (MinSurface - MaxCaveDepth) &&
            static_cast<float>(Bounds.Min.Z) < MaxSurface)
        {
            return TVoxelRange<v_flt>(
                FMath::Min(MinValue, -100.0f),
                FMath::Max(MaxValue, 100.0f));
        }
    }

    // Foundation bounds
    if (TerrainSnapshot.bAddGlobalFoundation)
    {
        const float FoundationDepth = static_cast<float>(TerrainSnapshot.GetFoundationDepthVoxels());
        const float FoundationAtMinZ = FMath::Max(
            static_cast<float>(Bounds.Min.Z),
            -(static_cast<float>(Bounds.Min.Z) + FoundationDepth));
        const float FoundationAtMaxZ = FMath::Max(
            static_cast<float>(Bounds.Max.Z),
            -(static_cast<float>(Bounds.Max.Z) + FoundationDepth));
        const float FoundationMin = FMath::Min(FoundationAtMinZ, FoundationAtMaxZ);
        MinValue = FMath::Min(MinValue, FoundationMin);
    }

    // ── NEW FIX: Prevent Voxel Plugin from culling chunks with dug holes ──
    if (TerrainSnapshot.RemovedTerrainBlocks.Num() > 0)
    {
        for (const FIntVector& RemovedBlock : TerrainSnapshot.RemovedTerrainBlocks)
        {
            // If the removed block falls within this chunk's bounds
            constexpr int32 DigRangePadding = 3;

            if (RemovedBlock.X >= Bounds.Min.X - DigRangePadding && RemovedBlock.X <= Bounds.Max.X + DigRangePadding &&
                RemovedBlock.Y >= Bounds.Min.Y - DigRangePadding && RemovedBlock.Y <= Bounds.Max.Y + DigRangePadding &&
                RemovedBlock.Z >= Bounds.Min.Z - DigRangePadding && RemovedBlock.Z <= Bounds.Max.Z + DigRangePadding)
            {
                return TVoxelRange<v_flt>(
                    FMath::Min(MinValue, -10.0f),
                    FMath::Max(MaxValue, 10.0f)
                );
            }
        }
    }

    return TVoxelRange<v_flt>(MinValue, MaxValue);
}

FVector FWorldForgeTerrainGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
    return FVector::UpVector;
}

// ═══════════════════════════════════════════════════════════════════
// SURFACE HEIGHT (unchanged — 2D macro shape)
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::ComputeSurfaceHeight(float VoxelX, float VoxelY) const
{
    // Step 1: Get bilinearly interpolated macro height from tile elevation
    const float MacroHeight = FWorldForgeTerrainMath::ComputeSurfaceHeight(
        VoxelX, VoxelY, TerrainSnapshot);

    // Step 2: Add mountain ridge displacement + volcano height from Step03.
    // Only on land — skip if macro height is at or below sea level.
    float RidgeHeight = 0.0f;
    float VolcanoHeight = 0.0f;
    const float SeaLevel = static_cast<float>(TerrainSnapshot.SeaLevelVoxels);
    if (MacroHeight > SeaLevel)
    {
        RidgeHeight = FWorldForgeTerrainMath::SampleMountainRidgeHeight(
            VoxelX, VoxelY, TerrainSnapshot);

        VolcanoHeight = FWorldForgeTerrainMath::SampleVolcanoHeight(
            VoxelX, VoxelY, TerrainSnapshot);
    }

    // Step 3: Add small biome-dependent detail noise
    float DetailHeight = 0.0f;
    if (const FVoxelBiomeProfile* Profile = SamplePrimaryBiomeProfile(VoxelX, VoxelY))
    {
        DetailHeight = ComputeDetailHeight(VoxelX, VoxelY, *Profile);
    }
    DetailHeight = FMath::Clamp(DetailHeight, -24.0f, 24.0f);

    return MacroHeight + RidgeHeight + VolcanoHeight + DetailHeight;
}

// ─── Tile elevation sampler (delegates to TerrainMath) ──────────

float FWorldForgeTerrainGeneratorInstance::SampleTileElevationBilinear(float VoxelX, float VoxelY) const
{
    return FWorldForgeTerrainMath::SampleTileElevationBilinear(VoxelX, VoxelY, TerrainSnapshot);
}

// ─── Biome profile lookup ───────────────────────────────────────

const FVoxelBiomeProfile* FWorldForgeTerrainGeneratorInstance::SamplePrimaryBiomeProfile(float VoxelX, float VoxelY) const
{
    if (!IsSnapshotValid())
    {
        return nullptr;
    }

    const float ChunkSizeF = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;

    const int32 ChunkX = FMath::FloorToInt((VoxelX / ChunkSizeF) + HalfGridX);
    const int32 ChunkY = FMath::FloorToInt((VoxelY / ChunkSizeF) + HalfGridY);

    const FIntPoint ChunkCell = TerrainSnapshot.ClampCell(FIntPoint(ChunkX, ChunkY));
    return TerrainSnapshot.GetVoxelProfile(ChunkCell);
}

// ─── Detail noise (unchanged) ───────────────────────────────────

float FWorldForgeTerrainGeneratorInstance::ComputeDetailHeight(float VoxelX, float VoxelY, const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = FMath::Max(KINDA_SMALL_NUMBER, TerrainSnapshot.VoxelSize);

    const float RawAmplitudeVoxels = Profile.NoiseAmplitude / SafeVoxelSize;
    const float RoughnessScale = FMath::Max(0.0f, Profile.SurfaceRoughnessScale);
    const float DetailAmplitudeVoxels = FMath::Clamp(
        RawAmplitudeVoxels * TerrainSnapshot.DetailAmplitude * 0.01f * RoughnessScale,
        0.0f,
        48.0f
    );

    const float DetailFrequency = FMath::Max(0.0001f, TerrainSnapshot.DetailScale);
    const int32 DetailOctaves = FMath::Clamp(TerrainSnapshot.DetailOctaves, 1, 8);

    const float NoiseValue = WF_FractalPerlin2D(
        VoxelX + static_cast<float>(Seed),
        VoxelY + static_cast<float>(Seed * 2),
        DetailFrequency,
        DetailOctaves
    );

    return NoiseValue * DetailAmplitudeVoxels;
}

// ═══════════════════════════════════════════════════════════════════
// LAYER 2: WORM CAVE NETWORKS (v4.0)
// ═══════════════════════════════════════════════════════════════════
//
// Two independent 3D noise fields are sampled.  Caves form where
// BOTH fields are near zero simultaneously, producing natural
// tubular passages ("worm caves").
//
// The intersection condition creates narrow tube-shaped voids:
//   caveMask = 1.0 - (n1² + n2²)
//   When caveMask > threshold → carve
//
// This produces rounded cave walls naturally because the SDF
// Transvoxel mesh follows the smooth noise gradient, giving the
// "natural arched ceilings" the GDD describes.
//
// Parameters are biome-driven via FVoxelBiomeProfile:
//   CaveIntensity: overall multiplier (0=none, 1=normal, 2+=dense)
//   CaveWormFrequency: spatial frequency of the noise
//   CaveMinDepth / CaveMaxDepth: depth gate below surface
//   CaveZScale: Z-axis frequency scale (<1 = horizontal tubes)

float FWorldForgeTerrainGeneratorInstance::ComputeWormCaveDensity(
    float X, float Y, float Z,
    float SurfaceHeight, const FVoxelBiomeProfile& Profile) const
{
    const float DepthBelowSurface = SurfaceHeight - Z;

    // Depth gate: no caves above MinDepth or below MaxDepth
    const float MinDepth = FMath::Max(1.0f, Profile.CaveMinDepth);
    const float MaxDepth = FMath::Max(MinDepth + 1.0f, Profile.CaveMaxDepth);

    if (DepthBelowSurface < MinDepth || DepthBelowSurface > MaxDepth)
    {
        return 0.0f;
    }

    // Depth fade: caves ramp in over the first 20% of the range
    // and fade out over the last 10%.
    const float DepthRange = MaxDepth - MinDepth;
    const float FadeInEnd = MinDepth + DepthRange * 0.2f;
    const float FadeOutStart = MaxDepth - DepthRange * 0.1f;

    float DepthAlpha = 1.0f;
    if (DepthBelowSurface < FadeInEnd)
    {
        DepthAlpha = FMath::Clamp((DepthBelowSurface - MinDepth) / (FadeInEnd - MinDepth), 0.0f, 1.0f);
    }
    else if (DepthBelowSurface > FadeOutStart)
    {
        DepthAlpha = FMath::Clamp((MaxDepth - DepthBelowSurface) / (MaxDepth - FadeOutStart), 0.0f, 1.0f);
    }

    // Sample two 3D noise fields with offset seeds
    const float Freq = FMath::Max(0.00001f, Profile.CaveWormFrequency);
    const float ZScale = FMath::Max(0.05f, Profile.CaveZScale);
    const float SeedF = static_cast<float>(Seed);

    const float N1 = WF_Perlin3D(
        (X + SeedF * 1.17f) * Freq,
        (Y + SeedF * 1.73f) * Freq,
        (Z + SeedF * 2.11f) * Freq * ZScale
    );

    const float N2 = WF_Perlin3D(
        (X + 1000.0f + SeedF * 0.37f) * Freq,
        (Y + 1000.0f + SeedF * 0.53f) * Freq,
        (Z + 1000.0f + SeedF * 0.89f) * Freq * ZScale
    );

    // Worm intersection: peaks where both fields are near zero
    const float WormMask = 1.0f - (N1 * N1 + N2 * N2);

    // Apply threshold — only carve where mask exceeds it
    const float Threshold = FMath::Clamp(TerrainSnapshot.CaveWormThreshold, 0.0f, 1.0f);

    if (WormMask <= Threshold)
    {
        return 0.0f;
    }

    // Carve strength: positive = push toward air
    const float CarveStrength = FMath::Max(0.0f, TerrainSnapshot.CaveCarveStrength);
    const float Intensity = FMath::Max(0.0f, Profile.CaveIntensity);

    const float RawCarve = (WormMask - Threshold) * CarveStrength * Intensity * DepthAlpha;

    return RawCarve;
}

// ═══════════════════════════════════════════════════════════════════
// LAYER 3: CANYON CARVING (v4.0)
// ═══════════════════════════════════════════════════════════════════
//
// Canyons are deep, narrow cuts from the surface downward.
// Domain-warped ridge noise defines the canyon paths (see
// FWorldForgeTerrainMath::SampleCanyonRidgeNoise).
//
// The carve is 3D-aware: canyon walls aren't perfectly vertical
// because the ridge noise field is continuous.  Slight overhangs
// form naturally where the noise bows inward.
//
// Parameters are biome-driven:
//   CanyonIntensity: 0 = no canyons (most biomes)
//   CanyonMaxDepth: how deep the canyon cuts (voxels)
//   CanyonSharpness: power curve on the ridge (higher = narrower)

float FWorldForgeTerrainGeneratorInstance::ComputeCanyonCarve(
    float X, float Y, float Z,
    float SurfaceHeight, const FVoxelBiomeProfile& Profile) const
{
    // Only carve between surface and canyon floor
    const float MaxDepth = FMath::Max(1.0f, Profile.CanyonMaxDepth);
    const float DepthBelowSurface = SurfaceHeight - Z;

    // Above surface or below canyon floor → no carve
    if (DepthBelowSurface < -2.0f || DepthBelowSurface > MaxDepth)
    {
        return 0.0f;
    }

    // Sample the ridge noise (0–1, peaks at canyon center)
    const float RidgeRaw = FWorldForgeTerrainMath::SampleCanyonRidgeNoise(
        X, Y, TerrainSnapshot);

    // Apply sharpness power curve: higher = narrower canyon cuts
    const float Sharpness = FMath::Max(0.5f, Profile.CanyonSharpness);
    const float Ridge = FMath::Pow(FMath::Clamp(RidgeRaw, 0.0f, 1.0f), Sharpness);

    // Canyon depth at this XY: how deep the ridge noise wants to cut
    const float CanyonFloor = SurfaceHeight - Ridge * MaxDepth;

    // Slight overhang via 3D noise perturbation: offset the wall
    // by a small 3D noise that varies with depth.
    const float OverhangNoise = WF_Perlin3D(
        X * 0.015f + 3100.0f,
        Y * 0.015f + 7300.0f,
        Z * 0.02f + 1900.0f
    ) * 3.0f; // ±3 voxels of wall variation

    // If this voxel is inside the canyon void, push density positive (air)
    if (Z > (CanyonFloor + OverhangNoise) && Z < (SurfaceHeight + 1.0f))
    {
        // Smooth falloff at canyon walls
        const float DistFromFloor = Z - (CanyonFloor + OverhangNoise);
        const float DistFromSurface = SurfaceHeight - Z;
        const float WallFade = FMath::Min(
            FMath::Clamp(DistFromFloor / 3.0f, 0.0f, 1.0f),
            FMath::Clamp(DistFromSurface / 2.0f, 0.0f, 1.0f)
        );

        const float Intensity = FMath::Max(0.0f, Profile.CanyonIntensity);
        return WallFade * Intensity * 8.0f; // positive = air
    }

    return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════
// LAYER 4: LAVA TUBE SUB-LAYER (v4.0)
// ═══════════════════════════════════════════════════════════════════
//
// Lava tubes are horizontal-biased worm caves that only generate
// within the cone radius of volcanoes.  They use a compressed Z
// frequency to produce flat, tube-shaped passages.

float FWorldForgeTerrainGeneratorInstance::ComputeLavaTubeDensity(
    float X, float Y, float Z,
    float SurfaceHeight) const
{
    if (TerrainSnapshot.PipelineVolcanoes.Num() == 0)
    {
        return 0.0f;
    }

    const float DepthBelowSurface = SurfaceHeight - Z;

    // Lava tubes only exist 15–80 voxels below surface
    if (DepthBelowSurface < 15.0f || DepthBelowSurface > 80.0f)
    {
        return 0.0f;
    }

    // Check if this voxel is within any volcano's cone
    const float ChunkSize = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;
    const float TileX = (X / ChunkSize) + HalfGridX;
    const float TileY = (Y / ChunkSize) + HalfGridY;

    bool bInsideVolcano = false;
    for (const FVolcanoData& V : TerrainSnapshot.PipelineVolcanoes)
    {
        const float DX = TileX - V.Center.X;
        const float DY = TileY - V.Center.Y;
        if ((DX * DX + DY * DY) < (V.ConeRadius * V.ConeRadius))
        {
            bInsideVolcano = true;
            break;
        }
    }

    if (!bInsideVolcano)
    {
        return 0.0f;
    }

    // Worm cave with Z-compressed frequency → horizontal tubes
    const float Freq = 0.015f;
    const float ZCompress = FMath::Max(0.05f, TerrainSnapshot.LavaTubeZCompress);
    const float SeedF = static_cast<float>(Seed);

    const float N1 = WF_Perlin3D(
        (X + SeedF * 3.17f + 5000.0f) * Freq,
        (Y + SeedF * 3.73f + 5000.0f) * Freq,
        (Z + SeedF * 3.11f + 5000.0f) * Freq * ZCompress
    );

    const float N2 = WF_Perlin3D(
        (X + 2000.0f + SeedF * 1.37f) * Freq,
        (Y + 2000.0f + SeedF * 1.53f) * Freq,
        (Z + 2000.0f + SeedF * 1.89f) * Freq * ZCompress
    );

    const float WormMask = 1.0f - (N1 * N1 + N2 * N2);

    if (WormMask <= 0.60f)
    {
        return 0.0f;
    }

    // Depth fade
    const float DepthAlpha = FMath::Clamp((DepthBelowSurface - 15.0f) / 20.0f, 0.0f, 1.0f) *
                              FMath::Clamp((80.0f - DepthBelowSurface) / 15.0f, 0.0f, 1.0f);

    return (WormMask - 0.60f) * 10.0f * DepthAlpha;
}

// ─── Legacy cave density (v3.0 — replaced by worm caves) ────────

float FWorldForgeTerrainGeneratorInstance::ComputeCaveDensity(float X, float Y, float Z, const FVoxelBiomeProfile& Profile) const
{
    // v4.0: This is no longer called.  Worm caves (ComputeWormCaveDensity)
    // replaced this system.  Kept for reference/rollback.
    return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════
// SLOPE + MATERIAL CLASSIFICATION
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::ComputeApproximateSlopeDegrees(float VoxelX, float VoxelY) const
{
    const float SampleOffset = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));

    const float Hx0 = ComputeSurfaceHeight(VoxelX - SampleOffset, VoxelY);
    const float Hx1 = ComputeSurfaceHeight(VoxelX + SampleOffset, VoxelY);
    const float Hy0 = ComputeSurfaceHeight(VoxelX, VoxelY - SampleOffset);
    const float Hy1 = ComputeSurfaceHeight(VoxelX, VoxelY + SampleOffset);

    const float Dx = (Hx1 - Hx0) / (2.0f * SampleOffset);
    const float Dy = (Hy1 - Hy0) / (2.0f * SampleOffset);
    const float GradientMagnitude = FMath::Sqrt((Dx * Dx) + (Dy * Dy));

    return FMath::RadiansToDegrees(FMath::Atan(GradientMagnitude));
}

float FWorldForgeTerrainGeneratorInstance::ComputeNormalizedSurfaceElevation(float SurfaceHeight) const
{
    return FWorldForgeTerrainMath::ComputeNormalizedLandElevation(SurfaceHeight, TerrainSnapshot);
}

// ─── Surface material classification (unchanged) ────────────────

int32 FWorldForgeTerrainGeneratorInstance::ClassifyTerrainMaterialLayer(float VoxelX, float VoxelY, float SurfaceHeight) const
{
    const float NormElev = ComputeNormalizedSurfaceElevation(SurfaceHeight);
    const float SlopeDeg = ComputeApproximateSlopeDegrees(VoxelX, VoxelY);
    const float SeaLevel = static_cast<float>(TerrainSnapshot.SeaLevelVoxels);

    // ── Biome tag lookup for special terrain types (Lava, Volcanic)
    {
        const float ChunkSizeF = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;

        const int32 ChunkX = FMath::FloorToInt((VoxelX / ChunkSizeF) + HalfGridX);
        const int32 ChunkY = FMath::FloorToInt((VoxelY / ChunkSizeF) + HalfGridY);
        const FIntPoint Cell = TerrainSnapshot.ClampCell(FIntPoint(ChunkX, ChunkY));

        const FGameplayTag BiomeTag = TerrainSnapshot.GetBiomeTag(Cell);

        // Lava biome override
        static const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
        if (LavaTag.IsValid() && BiomeTag.MatchesTag(LavaTag))
        {
            return TerrainLayer_Lava;
        }

        // Volcanic biome → basalt
        static const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
        if (VolcanicTag.IsValid() && BiomeTag.MatchesTag(VolcanicTag))
        {
            return TerrainLayer_Basalt;
        }
    }

    // ── Water
    if (SurfaceHeight <= SeaLevel)
    {
        return TerrainLayer_Water;
    }

    // ── Sand (beach strip)
    const float CoastBand = FMath::Max(0.0f, TerrainSnapshot.CoastBandVoxels);
    if (SurfaceHeight <= (SeaLevel + CoastBand) ||
        NormElev <= FMath::Clamp(TerrainSnapshot.SandMaxElevationNormalized, 0.0f, 1.0f))
    {
        return TerrainLayer_Sand;
    }

    // ── Snow (high peaks)
    if (NormElev >= FMath::Clamp(TerrainSnapshot.SnowMinElevationNormalized, 0.0f, 1.0f))
    {
        return TerrainLayer_Snow;
    }

    // ── Rock (cliffs by slope)
    if (SlopeDeg >= FMath::Max(0.0f, TerrainSnapshot.CliffSlopeThresholdDegrees))
    {
        return TerrainLayer_Rock;
    }

    // ── Rock (high altitude)
    if (NormElev >= FMath::Clamp(TerrainSnapshot.HighRockMinElevationNormalized, 0.0f, 1.0f))
    {
        return TerrainLayer_Rock;
    }

    // ── Rock (mid altitude + steep)
    if (NormElev >= FMath::Clamp(TerrainSnapshot.RockMinElevationNormalized, 0.0f, 1.0f) &&
        SlopeDeg >= FMath::Max(0.0f, TerrainSnapshot.RockSlopeThresholdDegrees))
    {
        return TerrainLayer_Rock;
    }

    // ── Dirt (mid elevation or moderate slope)
    if (NormElev >= FMath::Clamp(TerrainSnapshot.DirtMinElevationNormalized, 0.0f, 1.0f) ||
        SlopeDeg >= FMath::Max(0.0f, TerrainSnapshot.DirtSlopeThresholdDegrees))
    {
        return TerrainLayer_Dirt;
    }

    // ── Grass (everything else — the broad lowland band)
    return TerrainLayer_Grass;
}

// ─── v4.0: Underground material classification ──────────────────
//
// Depth-stratified: the deeper you go, the harder the rock.
// Cave walls and lava tube walls get special treatment.

int32 FWorldForgeTerrainGeneratorInstance::ClassifyUndergroundMaterial(
    float VoxelX, float VoxelY, float Z, float SurfaceHeight) const
{
    const float DepthBelowSurface = SurfaceHeight - Z;
    const int32 SurfaceLayer = ClassifyTerrainMaterialLayer(VoxelX, VoxelY, SurfaceHeight);

    // ── Biome tag check for volcanic underground
    {
        const float ChunkSizeF = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;
        const int32 ChunkX = FMath::FloorToInt((VoxelX / ChunkSizeF) + HalfGridX);
        const int32 ChunkY = FMath::FloorToInt((VoxelY / ChunkSizeF) + HalfGridY);
        const FIntPoint Cell = TerrainSnapshot.ClampCell(FIntPoint(ChunkX, ChunkY));
        const FGameplayTag BiomeTag = TerrainSnapshot.GetBiomeTag(Cell);

        static const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
        static const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);

        if (LavaTag.IsValid() && BiomeTag.MatchesTag(LavaTag))
        {
            return TerrainLayer_Lava;
        }
        if (VolcanicTag.IsValid() && BiomeTag.MatchesTag(VolcanicTag))
        {
            return TerrainLayer_Basalt;
        }
    }

    // ── Depth strata
    if (DepthBelowSurface <= 4.5f)
    {
        // Shallow subsurface: transitional material
        switch (SurfaceLayer)
        {
        case TerrainLayer_Water:  return TerrainLayer_Water;
        case TerrainLayer_Sand:   return TerrainLayer_Sand;
        case TerrainLayer_Snow:   return TerrainLayer_Dirt;
        case TerrainLayer_Rock:   return TerrainLayer_Rock;
        case TerrainLayer_Lava:   return TerrainLayer_Lava;
        case TerrainLayer_Basalt: return TerrainLayer_Basalt;
        default:                  return TerrainLayer_Dirt;
        }
    }

    if (DepthBelowSurface <= 20.0f)
    {
        // Mid depth: rock
        return TerrainLayer_Rock;
    }

    // Deep underground: deep rock
    return TerrainLayer_DeepRock;
}

// ─── Material builder ───────────────────────────────────────────

FVoxelMaterial FWorldForgeTerrainGeneratorInstance::MakeVoxelMaterialFromLayer(int32 MaterialLayer) const
{
    FVoxelMaterialBuilder Builder;
    Builder.SetMaterialConfig(EVoxelMaterialConfig::SingleIndex);
    Builder.SetSingleIndex(static_cast<uint8>(FMath::Clamp(MaterialLayer, 0, 255)));
    return Builder.Build();
}

// ─── Helpers ────────────────────────────────────────────────────

bool FWorldForgeTerrainGeneratorInstance::IsSnapshotValid() const
{
    return TerrainSnapshot.IsValid();
}

FIntPoint FWorldForgeTerrainGeneratorInstance::ClampChunkCell(const FIntPoint& Cell) const
{
    return TerrainSnapshot.ClampCell(Cell);
}

int32 FWorldForgeTerrainGeneratorInstance::ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = (TerrainSnapshot.VoxelSize > KINDA_SMALL_NUMBER)
        ? TerrainSnapshot.VoxelSize
        : 100.0f;
    const int32 DerivedDepth = FMath::RoundToInt(Profile.NoiseAmplitude / SafeVoxelSize);
    return FMath::Max(12, DerivedDepth);
}

// ─── Legacy stubs (kept for header compatibility) ───────────────

float FWorldForgeTerrainGeneratorInstance::SampleContinentHeight(float X, float Y) const { return 0.0f; }
float FWorldForgeTerrainGeneratorInstance::SampleBasinDepth(float X, float Y) const { return 0.0f; }
float FWorldForgeTerrainGeneratorInstance::SampleRiverCarve(float X, float Y) const { return 0.0f; }
float FWorldForgeTerrainGeneratorInstance::SampleMountainHeight(float X, float Y) const { return 0.0f; }

float FWorldForgeTerrainGeneratorInstance::ComputeMacroBaseHeight(float VoxelX, float VoxelY) const
{
    return FWorldForgeTerrainMath::ComputeSurfaceHeight(VoxelX, VoxelY, TerrainSnapshot);
}
