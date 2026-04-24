// ════════════════════════════════════════════════════════════════════
// WorldForgeTerrainGenerator.cpp — REWRITE v2
//
// The voxel generator is now a PURE RENDERER.  It converts tile
// elevation data into voxel density and material, nothing more.
//
// All terrain shape comes from Tile.Elevation (0–1) via
// FWorldForgeTerrainMath::ComputeSurfaceHeight().
//
// Responsibilities:
//   ✓ Convert tile elevation to voxel surface height (bilinear)
//   ✓ Add small biome-dependent detail noise
//   ✓ Classify terrain material (grass/dirt/rock/snow/sand/water)
//   ✓ Generate cave density (optional)
//   ✗ Does NOT create mountains, rivers, basins, or any macro shape
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
    CaveNoise.SetSeed(Seed + 1337);
}

// ─── Density field ──────────────────────────────────────────────

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

    const float SurfaceHeight = ComputeSurfaceHeight(X, Y);

    float Density = Z - SurfaceHeight;

    // Optional cave carving
    if (const FVoxelBiomeProfile* Profile = SamplePrimaryBiomeProfile(X, Y))
    {
        Density += ComputeCaveDensity(X, Y, Z, *Profile);
    }

    // Global foundation (solid floor below all terrain)
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

// ─── Material assignment ────────────────────────────────────────

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

    const int32 SurfaceLayer = ClassifyTerrainMaterialLayer(X, Y, SurfaceHeight);

    if (DepthBelowSurface <= 1.5f)
    {
        return MakeVoxelMaterialFromLayer(SurfaceLayer);
    }

    if (DepthBelowSurface <= 4.5f)
    {
        switch (SurfaceLayer)
        {
        case TerrainLayer_Water: return MakeVoxelMaterialFromLayer(TerrainLayer_Water);
        case TerrainLayer_Sand:  return MakeVoxelMaterialFromLayer(TerrainLayer_Sand);
        case TerrainLayer_Snow:  return MakeVoxelMaterialFromLayer(TerrainLayer_Dirt);
        case TerrainLayer_Rock:  return MakeVoxelMaterialFromLayer(TerrainLayer_Rock);
        default:                 return MakeVoxelMaterialFromLayer(TerrainLayer_Dirt);
        }
    }

    return MakeVoxelMaterialFromLayer(TerrainLayer_Rock);
}

// ─── Value range (LOD culling) ──────────────────────────────────

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

    const float MinValue = static_cast<float>(Bounds.Min.Z) - MaxSurface;
    const float MaxValue = static_cast<float>(Bounds.Max.Z) - MinSurface;

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
        return TVoxelRange<v_flt>(FMath::Min(MinValue, FoundationMin), MaxValue);
    }

    return TVoxelRange<v_flt>(MinValue, MaxValue);
}

FVector FWorldForgeTerrainGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
    return FVector::UpVector;
}

// ─── Height computation (single path) ───────────────────────────

float FWorldForgeTerrainGeneratorInstance::ComputeSurfaceHeight(float VoxelX, float VoxelY) const
{
    // Step 1: Get bilinearly interpolated macro height from tile elevation
    const float MacroHeight = FWorldForgeTerrainMath::ComputeSurfaceHeight(
        VoxelX, VoxelY, TerrainSnapshot);

    // Step 2: Add small biome-dependent detail noise
    float DetailHeight = 0.0f;
    if (const FVoxelBiomeProfile* Profile = SamplePrimaryBiomeProfile(VoxelX, VoxelY))
    {
        DetailHeight = ComputeDetailHeight(VoxelX, VoxelY, *Profile);
    }
    DetailHeight = FMath::Clamp(DetailHeight, -24.0f, 24.0f);

    return MacroHeight + DetailHeight;
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

// ─── Detail noise (small-scale surface variation only) ──────────

float FWorldForgeTerrainGeneratorInstance::ComputeDetailHeight(float VoxelX, float VoxelY, const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = FMath::Max(KINDA_SMALL_NUMBER, TerrainSnapshot.VoxelSize);

    const float RawAmplitudeVoxels = Profile.NoiseAmplitude / SafeVoxelSize;
    const float DetailAmplitudeVoxels = FMath::Clamp(
        RawAmplitudeVoxels * TerrainSnapshot.DetailAmplitude * 0.01f,
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

// ─── Cave density ───────────────────────────────────────────────

float FWorldForgeTerrainGeneratorInstance::ComputeCaveDensity(float X, float Y, float Z, const FVoxelBiomeProfile& Profile) const
{
    if (!IsSnapshotValid() || !TerrainSnapshot.bEnableCaves)
    {
        return 0.0f;
    }

    const float SurfaceHeight = ComputeSurfaceHeight(X, Y);
    const float DepthBelowSurface = SurfaceHeight - Z;

    if (DepthBelowSurface < 12.0f || DepthBelowSurface > 96.0f)
    {
        return 0.0f;
    }

    const float CaveFreq = FMath::Max(0.00001f, TerrainSnapshot.CaveFrequency);
    const float SecondOctaveMultiplier = FMath::Max(0.00001f, TerrainSnapshot.CaveSecondOctaveMultiplier);
    const float PrimaryWeight = FMath::Clamp(TerrainSnapshot.CavePrimaryWeight, 0.0f, 1.0f);
    const float SecondaryWeight = FMath::Clamp(TerrainSnapshot.CaveSecondaryWeight, 0.0f, 1.0f);
    const float WeightSum = FMath::Max(KINDA_SMALL_NUMBER, PrimaryWeight + SecondaryWeight);

    const float N1 = WF_Perlin3D(
        (X + static_cast<float>(Seed) * 1.17f) * CaveFreq,
        (Y + static_cast<float>(Seed) * 1.73f) * CaveFreq,
        (Z + static_cast<float>(Seed) * 2.11f) * CaveFreq
    );

    const float N2 = WF_Perlin3D(
        (X + 913.0f + static_cast<float>(Seed) * 0.37f) * (CaveFreq * SecondOctaveMultiplier),
        (Y + 271.0f + static_cast<float>(Seed) * 0.53f) * (CaveFreq * SecondOctaveMultiplier),
        (Z + 611.0f + static_cast<float>(Seed) * 0.89f) * (CaveFreq * SecondOctaveMultiplier)
    );

    const float CaveNoiseValue =
        (N1 * (PrimaryWeight / WeightSum)) +
        (N2 * (SecondaryWeight / WeightSum));

    const float DepthAlpha = FMath::Clamp((DepthBelowSurface - 12.0f) / 48.0f, 0.0f, 1.0f);
    const float CaveThreshold = FMath::Clamp(TerrainSnapshot.CaveThreshold, 0.0f, 1.0f);

    return (CaveNoiseValue - CaveThreshold) * DepthAlpha * 18.0f;
}

// ─── Slope computation ──────────────────────────────────────────

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

// ─── Normalized elevation (delegates to TerrainMath) ────────────

float FWorldForgeTerrainGeneratorInstance::ComputeNormalizedSurfaceElevation(float SurfaceHeight) const
{
    return FWorldForgeTerrainMath::ComputeNormalizedLandElevation(SurfaceHeight, TerrainSnapshot);
}

// ─── Material classification ────────────────────────────────────

int32 FWorldForgeTerrainGeneratorInstance::ClassifyTerrainMaterialLayer(float VoxelX, float VoxelY, float SurfaceHeight) const
{
    const float NormElev = ComputeNormalizedSurfaceElevation(SurfaceHeight);
    const float SlopeDeg = ComputeApproximateSlopeDegrees(VoxelX, VoxelY);
    const float SeaLevel = static_cast<float>(TerrainSnapshot.SeaLevelVoxels);

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
    // v2: delegates to the single-path formula
    return FWorldForgeTerrainMath::ComputeSurfaceHeight(VoxelX, VoxelY, TerrainSnapshot);
}
