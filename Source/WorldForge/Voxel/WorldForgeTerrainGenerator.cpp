#include "WorldForgeTerrainGenerator.h"

namespace
{
    static float WF_Bilerp(float V00, float V10, float V01, float V11, float FX, float FY)
    {
        const float X0 = FMath::Lerp(V00, V10, FX);
        const float X1 = FMath::Lerp(V01, V11, FX);
        return FMath::Lerp(X0, X1, FY);
    }

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

    if (TerrainSnapshot.bAddGlobalFoundation)
    {
        const float FoundationDepth = static_cast<float>(TerrainSnapshot.GetFoundationDepthVoxels());

        const float DistanceToFoundationTop = Z;
        const float DistanceToFoundationBottom = -(Z + FoundationDepth);
        const float FoundationDensity = FMath::Max(DistanceToFoundationTop, DistanceToFoundationBottom);

        Density = FMath::Min(Density, FoundationDensity);
    }

    // Caves are intentionally disabled for the stabilization pass.
    // Once the broad terrain shape is validated, cave carving can be
    // reintroduced in a much more conservative form.

    return Density;
}

FVoxelMaterial FWorldForgeTerrainGeneratorInstance::GetMaterialImpl(
    v_flt X,
    v_flt Y,
    v_flt Z,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    return FVoxelMaterial();
}

TVoxelRange<v_flt> FWorldForgeTerrainGeneratorInstance::GetValueRangeImpl(
    const FVoxelIntBox& Bounds,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    if (!IsSnapshotValid())
    {
        return TVoxelRange<v_flt>(1.0f, 1.0f);
    }

    const float Surface00 = ComputeSurfaceHeight(Bounds.Min.X, Bounds.Min.Y);
    const float Surface01 = ComputeSurfaceHeight(Bounds.Min.X, Bounds.Max.Y);
    const float Surface10 = ComputeSurfaceHeight(Bounds.Max.X, Bounds.Min.Y);
    const float Surface11 = ComputeSurfaceHeight(Bounds.Max.X, Bounds.Max.Y);

    const float MinSurface = FMath::Min(FMath::Min(Surface00, Surface01), FMath::Min(Surface10, Surface11));
    const float MaxSurface = FMath::Max(FMath::Max(Surface00, Surface01), FMath::Max(Surface10, Surface11));

    const float MinValue = static_cast<float>(Bounds.Min.Z) - MaxSurface;
    const float MaxValue = static_cast<float>(Bounds.Max.Z) - MinSurface;

    if (TerrainSnapshot.bAddGlobalFoundation)
    {
        const float FoundationDepth = static_cast<float>(TerrainSnapshot.GetFoundationDepthVoxels());

        const float FoundationAtMinZ = FMath::Max(static_cast<float>(Bounds.Min.Z), -(static_cast<float>(Bounds.Min.Z) + FoundationDepth));
        const float FoundationAtMaxZ = FMath::Max(static_cast<float>(Bounds.Max.Z), -(static_cast<float>(Bounds.Max.Z) + FoundationDepth));
        const float FoundationMin = FMath::Min(FoundationAtMinZ, FoundationAtMaxZ);

        return TVoxelRange<v_flt>(FMath::Min(MinValue, FoundationMin), MaxValue);
    }

    return TVoxelRange<v_flt>(MinValue, MaxValue);
}

FVector FWorldForgeTerrainGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
    return FVector::UpVector;
}

float FWorldForgeTerrainGeneratorInstance::SampleContinentHeight(float X, float Y) const
{
    const float ContinentNoise = FMath::PerlinNoise2D(FVector2D(
        X * TerrainSnapshot.ContinentFrequency,
        Y * TerrainSnapshot.ContinentFrequency
    ));

    const float Continent01 = FMath::Clamp((ContinentNoise + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float ShapedContinent = FMath::Pow(
        Continent01,
        FMath::Max(0.1f, TerrainSnapshot.ContinentExponent)
    );

    return ((ShapedContinent * 2.0f) - 1.0f) * static_cast<float>(TerrainSnapshot.ContinentHeightRangeVoxels);
}

float FWorldForgeTerrainGeneratorInstance::SampleBasinDepth(float X, float Y) const
{
    const float BasinNoise = FMath::PerlinNoise2D(FVector2D(
        (X + 913.0f) * TerrainSnapshot.BasinFrequency,
        (Y + 271.0f) * TerrainSnapshot.BasinFrequency
    ));

    const float Basin01 = FMath::Clamp((BasinNoise + 1.0f) * 0.5f, 0.0f, 1.0f);

    if (Basin01 < TerrainSnapshot.BasinThreshold)
    {
        return 0.0f;
    }

    const float BasinAlpha =
        (Basin01 - TerrainSnapshot.BasinThreshold) /
        FMath::Max(0.001f, 1.0f - TerrainSnapshot.BasinThreshold);

    return BasinAlpha * static_cast<float>(TerrainSnapshot.LakeBasinDepthVoxels);
}

float FWorldForgeTerrainGeneratorInstance::SampleRiverCarve(float X, float Y) const
{
    const float RiverNoise = FMath::PerlinNoise2D(FVector2D(
        (X + 1703.0f) * TerrainSnapshot.RiverFrequency,
        (Y + 811.0f) * TerrainSnapshot.RiverFrequency
    ));

    const float RiverMask = 1.0f - FMath::Abs(RiverNoise);

    if (RiverMask < TerrainSnapshot.RiverThreshold)
    {
        return 0.0f;
    }

    const float RiverAlpha =
        (RiverMask - TerrainSnapshot.RiverThreshold) /
        FMath::Max(0.001f, 1.0f - TerrainSnapshot.RiverThreshold);

    const float ShapedRiver = FMath::Pow(
        FMath::Clamp(RiverAlpha, 0.0f, 1.0f),
        FMath::Max(0.1f, TerrainSnapshot.RiverSharpness)
    );

    return ShapedRiver * static_cast<float>(TerrainSnapshot.RiverCarveDepthVoxels);
}

float FWorldForgeTerrainGeneratorInstance::SampleTileElevationBilinear(float VoxelX, float VoxelY) const
{
    if (!IsSnapshotValid())
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));

    const float CellX = VoxelX / ChunkSize;
    const float CellY = VoxelY / ChunkSize;

    const int32 X0 = FMath::FloorToInt(CellX);
    const int32 Y0 = FMath::FloorToInt(CellY);
    const int32 X1 = X0 + 1;
    const int32 Y1 = Y0 + 1;

    const float FX = CellX - static_cast<float>(X0);
    const float FY = CellY - static_cast<float>(Y0);

    const FIntPoint C00 = TerrainSnapshot.ClampCell(FIntPoint(X0, Y0));
    const FIntPoint C10 = TerrainSnapshot.ClampCell(FIntPoint(X1, Y0));
    const FIntPoint C01 = TerrainSnapshot.ClampCell(FIntPoint(X0, Y1));
    const FIntPoint C11 = TerrainSnapshot.ClampCell(FIntPoint(X1, Y1));

    const float E00 = TerrainSnapshot.GetTileElevation(C00, 0.0f);
    const float E10 = TerrainSnapshot.GetTileElevation(C10, 0.0f);
    const float E01 = TerrainSnapshot.GetTileElevation(C01, 0.0f);
    const float E11 = TerrainSnapshot.GetTileElevation(C11, 0.0f);

    return WF_Bilerp(E00, E10, E01, E11, FX, FY);
}

const FVoxelBiomeProfile* FWorldForgeTerrainGeneratorInstance::SamplePrimaryBiomeProfile(float VoxelX, float VoxelY) const
{
    if (!IsSnapshotValid())
    {
        return nullptr;
    }

    const int32 ChunkSizeVoxels = FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels);
    const int32 ChunkX = FMath::FloorToInt(VoxelX / static_cast<float>(ChunkSizeVoxels));
    const int32 ChunkY = FMath::FloorToInt(VoxelY / static_cast<float>(ChunkSizeVoxels));

    const FIntPoint ChunkCell = TerrainSnapshot.ClampCell(FIntPoint(ChunkX, ChunkY));
    return TerrainSnapshot.GetVoxelProfile(ChunkCell);
}

float FWorldForgeTerrainGeneratorInstance::ComputeMacroBaseHeight(float VoxelX, float VoxelY) const
{
    const float TileElevation = SampleTileElevationBilinear(VoxelX, VoxelY);

    const float BlendedContinent = SampleContinentHeight(VoxelX, VoxelY);
    const float BlendedBasin = SampleBasinDepth(VoxelX, VoxelY);

    const float ElevationContribution =
        FMath::Pow(FMath::Clamp(TileElevation, 0.0f, 1.0f), 1.25f) * 28.0f;

    return
        static_cast<float>(TerrainSnapshot.SeaLevelVoxels) +
        BlendedContinent +
        ElevationContribution -
        BlendedBasin;
}

float FWorldForgeTerrainGeneratorInstance::ComputeDetailHeight(float VoxelX, float VoxelY, const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = FMath::Max(KINDA_SMALL_NUMBER, TerrainSnapshot.VoxelSize);

    // Stabilization pass:
    // Reduce the biome detail amplitude sharply so the broad macro terrain
    // shape dominates and we stop producing shredded vertical spikes.
    const float RawAmplitudeVoxels = Profile.NoiseAmplitude / SafeVoxelSize;
    const float StabilizedAmplitudeVoxels = FMath::Clamp(RawAmplitudeVoxels * 0.18f, 0.0f, 6.0f);

    // Also damp frequency slightly to reduce contour-heavy aliasing.
    const float StabilizedFrequency = FMath::Max(0.0001f, Profile.NoiseFrequency * 0.35f);

    const float NoiseValue = WF_FractalPerlin2D(
        VoxelX + static_cast<float>(Seed),
        VoxelY + static_cast<float>(Seed * 2),
        StabilizedFrequency,
        FMath::Clamp(Profile.OctaveCount, 1, 4)
    );

    return NoiseValue * StabilizedAmplitudeVoxels;
}

float FWorldForgeTerrainGeneratorInstance::ComputeSurfaceHeight(float VoxelX, float VoxelY) const
{
    const FVoxelBiomeProfile* Profile = SamplePrimaryBiomeProfile(VoxelX, VoxelY);

    const float MacroBaseHeight = ComputeMacroBaseHeight(VoxelX, VoxelY);
    const float RiverCarve = SampleRiverCarve(VoxelX, VoxelY);

    float DetailHeight = 0.0f;
    if (Profile)
    {
        DetailHeight = ComputeDetailHeight(VoxelX, VoxelY, *Profile);
    }

    // Clamp local variation so the macro terrain remains dominant.
    DetailHeight = FMath::Clamp(DetailHeight, -6.0f, 6.0f);

    float SurfaceHeight = MacroBaseHeight + DetailHeight - RiverCarve;

    SurfaceHeight = FMath::Max(
        static_cast<float>(TerrainSnapshot.SeaLevelVoxels - TerrainSnapshot.LakeBasinDepthVoxels),
        SurfaceHeight
    );

    return SurfaceHeight;
}

float FWorldForgeTerrainGeneratorInstance::ComputeCaveDensity(float X, float Y, float Z, const FVoxelBiomeProfile& Profile) const
{
    // Intentionally disabled during stabilization pass.
    return 0.0f;
}

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