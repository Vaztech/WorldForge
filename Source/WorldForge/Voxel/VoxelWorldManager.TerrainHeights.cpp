#include "VoxelWorldManager.h"

int32 UVoxelWorldManager::ComputeRiverCarveDepthVoxels(FIntPoint ChunkCoord) const
{
    const float SampleX = static_cast<float>(ChunkCoord.X);
    const float SampleY = static_cast<float>(ChunkCoord.Y);

    const float RiverNoise = PerlinNoise2D(
        SampleX * RiverFrequency + 1703.0f,
        SampleY * RiverFrequency + 811.0f
    );

    const float RiverMask = 1.0f - FMath::Abs(RiverNoise);

    if (RiverMask < RiverThreshold)
    {
        return 0;
    }

    const float RiverAlpha = (RiverMask - RiverThreshold) / FMath::Max(0.001f, 1.0f - RiverThreshold);
    const float ShapedRiver = FMath::Pow(
        FMath::Clamp(RiverAlpha, 0.0f, 1.0f),
        FMath::Max(0.1f, RiverSharpness)
    );

    return FMath::RoundToInt(ShapedRiver * static_cast<float>(RiverCarveDepthVoxels));
}

int32 UVoxelWorldManager::ComputeChunkSurfaceHeightVoxels(
    FIntPoint ChunkCoord,
    const FVoxelBiomeProfile& Profile,
    float TileElevation
) const
{
    const int32 ChunkSize = GetChunkSizeInVoxels();

    const int32 SampleX = (ChunkCoord.X * ChunkSize) + (ChunkSize / 2);
    const int32 SampleY = (ChunkCoord.Y * ChunkSize) + (ChunkSize / 2);

    const float LocalX01 = 0.5f;
    const float LocalY01 = 0.5f;

    const FIntPoint C00 = ClampTerrainCellToBounds(ChunkCoord);
    const FIntPoint C10 = ClampTerrainCellToBounds(FIntPoint(ChunkCoord.X + 1, ChunkCoord.Y));
    const FIntPoint C01 = ClampTerrainCellToBounds(FIntPoint(ChunkCoord.X, ChunkCoord.Y + 1));
    const FIntPoint C11 = ClampTerrainCellToBounds(FIntPoint(ChunkCoord.X + 1, ChunkCoord.Y + 1));

    const int32 I00 = TerrainIndex(C00);
    const int32 I10 = TerrainIndex(C10);
    const int32 I01 = TerrainIndex(C01);
    const int32 I11 = TerrainIndex(C11);

    const float E00 = CachedTileElevations.IsValidIndex(I00) ? CachedTileElevations[I00] : TileElevation;
    const float E10 = CachedTileElevations.IsValidIndex(I10) ? CachedTileElevations[I10] : TileElevation;
    const float E01 = CachedTileElevations.IsValidIndex(I01) ? CachedTileElevations[I01] : TileElevation;
    const float E11 = CachedTileElevations.IsValidIndex(I11) ? CachedTileElevations[I11] : TileElevation;

    const float EX0 = FMath::Lerp(E00, E10, LocalX01);
    const float EX1 = FMath::Lerp(E01, E11, LocalX01);
    const float BlendedElevation = FMath::Lerp(EX0, EX1, LocalY01);

    const float MacroSampleX = static_cast<float>(SampleX);
    const float MacroSampleY = static_cast<float>(SampleY);

    const float ContinentNoise = PerlinNoise2D(
        MacroSampleX * ContinentFrequency,
        MacroSampleY * ContinentFrequency
    );

    const float Continent01 = FMath::Clamp((ContinentNoise + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float ShapedContinent = FMath::Pow(Continent01, FMath::Max(0.1f, ContinentExponent));
    const float ContinentHeight = ((ShapedContinent * 2.0f) - 1.0f) * static_cast<float>(ContinentHeightRangeVoxels);

    const float BasinNoise = PerlinNoise2D(
        (MacroSampleX + 913.0f) * BasinFrequency,
        (MacroSampleY + 271.0f) * BasinFrequency
    );

    const float Basin01 = FMath::Clamp((BasinNoise + 1.0f) * 0.5f, 0.0f, 1.0f);

    float BasinDepth = 0.0f;
    if (Basin01 >= BasinThreshold)
    {
        const float BasinAlpha = (Basin01 - BasinThreshold) / FMath::Max(0.001f, 1.0f - BasinThreshold);
        BasinDepth = BasinAlpha * static_cast<float>(LakeBasinDepthVoxels);
    }

    const float ElevationContribution =
        FMath::Pow(FMath::Clamp(BlendedElevation, 0.0f, 1.0f), 1.25f) * 28.0f;

    const float MacroBaseHeightF =
        static_cast<float>(SeaLevelVoxels) +
        ContinentHeight +
        ElevationContribution -
        BasinDepth;

    const float H00 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(SampleX, SampleY, Profile));
    const float H10 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(SampleX + 1, SampleY, Profile));
    const float H01 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(SampleX, SampleY + 1, Profile));
    const float H11 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(SampleX + 1, SampleY + 1, Profile));

    const float SmoothedLocalNoiseHeight =
        (H00 + H10 + H01 + H11) * 0.25f;

    const float RiverNoise = PerlinNoise2D(
        (MacroSampleX + 1703.0f) * RiverFrequency,
        (MacroSampleY + 811.0f) * RiverFrequency
    );

    const float RiverMask = 1.0f - FMath::Abs(RiverNoise);

    float RiverCarveDepth = 0.0f;
    if (RiverMask >= RiverThreshold)
    {
        const float RiverAlpha = (RiverMask - RiverThreshold) / FMath::Max(0.001f, 1.0f - RiverThreshold);
        const float ShapedRiver = FMath::Pow(
            FMath::Clamp(RiverAlpha, 0.0f, 1.0f),
            FMath::Max(0.1f, RiverSharpness)
        );
        RiverCarveDepth = ShapedRiver * static_cast<float>(RiverCarveDepthVoxels);
    }

    float SurfaceHeightF = MacroBaseHeightF + SmoothedLocalNoiseHeight - RiverCarveDepth;

    SurfaceHeightF = FMath::Max(
        static_cast<float>(SeaLevelVoxels - LakeBasinDepthVoxels),
        SurfaceHeightF
    );

    return FMath::RoundToInt(SurfaceHeightF);
}

bool UVoxelWorldManager::TryGetTerrainSurfaceVoxelZ(FIntPoint VoxelXY, int32& OutVoxelZ) const
{
    OutVoxelZ = 0;

    if (!HasCachedTerrainData())
    {
        return false;
    }

    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    if (!IsValidTerrainCell(ChunkCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(ChunkCell);
    if (!CachedVoxelProfiles.IsValidIndex(Index))
    {
        return false;
    }

    const FVoxelBiomeProfile& Profile = CachedVoxelProfiles[Index];
    const int32 ChunkSize = GetChunkSizeInVoxels();

    const float LocalX01 =
        static_cast<float>(VoxelXY.X - (ChunkCell.X * ChunkSize)) /
        static_cast<float>(FMath::Max(1, ChunkSize));

    const float LocalY01 =
        static_cast<float>(VoxelXY.Y - (ChunkCell.Y * ChunkSize)) /
        static_cast<float>(FMath::Max(1, ChunkSize));

    const FIntPoint C00 = ClampTerrainCellToBounds(ChunkCell);
    const FIntPoint C10 = ClampTerrainCellToBounds(FIntPoint(ChunkCell.X + 1, ChunkCell.Y));
    const FIntPoint C01 = ClampTerrainCellToBounds(FIntPoint(ChunkCell.X, ChunkCell.Y + 1));
    const FIntPoint C11 = ClampTerrainCellToBounds(FIntPoint(ChunkCell.X + 1, ChunkCell.Y + 1));

    const int32 I00 = TerrainIndex(C00);
    const int32 I10 = TerrainIndex(C10);
    const int32 I01 = TerrainIndex(C01);
    const int32 I11 = TerrainIndex(C11);

    const float DefaultElevation = CachedTileElevations.IsValidIndex(Index) ? CachedTileElevations[Index] : 0.5f;

    const float E00 = CachedTileElevations.IsValidIndex(I00) ? CachedTileElevations[I00] : DefaultElevation;
    const float E10 = CachedTileElevations.IsValidIndex(I10) ? CachedTileElevations[I10] : DefaultElevation;
    const float E01 = CachedTileElevations.IsValidIndex(I01) ? CachedTileElevations[I01] : DefaultElevation;
    const float E11 = CachedTileElevations.IsValidIndex(I11) ? CachedTileElevations[I11] : DefaultElevation;

    const float EX0 = FMath::Lerp(E00, E10, LocalX01);
    const float EX1 = FMath::Lerp(E01, E11, LocalX01);
    const float BlendedElevation = FMath::Lerp(EX0, EX1, LocalY01);

    const float MacroSampleX = static_cast<float>(VoxelXY.X);
    const float MacroSampleY = static_cast<float>(VoxelXY.Y);

    const float ContinentNoise = PerlinNoise2D(
        MacroSampleX * ContinentFrequency,
        MacroSampleY * ContinentFrequency
    );

    const float Continent01 = FMath::Clamp((ContinentNoise + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float ShapedContinent = FMath::Pow(Continent01, FMath::Max(0.1f, ContinentExponent));
    const float ContinentHeight = ((ShapedContinent * 2.0f) - 1.0f) * static_cast<float>(ContinentHeightRangeVoxels);

    const float BasinNoise = PerlinNoise2D(
        (MacroSampleX + 913.0f) * BasinFrequency,
        (MacroSampleY + 271.0f) * BasinFrequency
    );

    const float Basin01 = FMath::Clamp((BasinNoise + 1.0f) * 0.5f, 0.0f, 1.0f);

    float BasinDepth = 0.0f;
    if (Basin01 >= BasinThreshold)
    {
        const float BasinAlpha = (Basin01 - BasinThreshold) / FMath::Max(0.001f, 1.0f - BasinThreshold);
        BasinDepth = BasinAlpha * static_cast<float>(LakeBasinDepthVoxels);
    }

    const float ElevationContribution =
        FMath::Pow(FMath::Clamp(BlendedElevation, 0.0f, 1.0f), 1.25f) * 28.0f;

    const float MacroBaseHeightF =
        static_cast<float>(SeaLevelVoxels) +
        ContinentHeight +
        ElevationContribution -
        BasinDepth;

    const float H00 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(VoxelXY.X, VoxelXY.Y, Profile));
    const float H10 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(VoxelXY.X + 1, VoxelXY.Y, Profile));
    const float H01 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(VoxelXY.X, VoxelXY.Y + 1, Profile));
    const float H11 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(VoxelXY.X + 1, VoxelXY.Y + 1, Profile));

    const float SmoothedLocalNoiseHeight =
        (H00 + H10 + H01 + H11) * 0.25f;

    const float RiverNoise = PerlinNoise2D(
        (MacroSampleX + 1703.0f) * RiverFrequency,
        (MacroSampleY + 811.0f) * RiverFrequency
    );

    const float RiverMask = 1.0f - FMath::Abs(RiverNoise);

    float RiverCarveDepth = 0.0f;
    if (RiverMask >= RiverThreshold)
    {
        const float RiverAlpha = (RiverMask - RiverThreshold) / FMath::Max(0.001f, 1.0f - RiverThreshold);
        const float ShapedRiver = FMath::Pow(
            FMath::Clamp(RiverAlpha, 0.0f, 1.0f),
            FMath::Max(0.1f, RiverSharpness)
        );
        RiverCarveDepth = ShapedRiver * static_cast<float>(RiverCarveDepthVoxels);
    }

    float SurfaceHeightF = MacroBaseHeightF + SmoothedLocalNoiseHeight - RiverCarveDepth;

    SurfaceHeightF = FMath::Max(
        static_cast<float>(SeaLevelVoxels - LakeBasinDepthVoxels),
        SurfaceHeightF
    );

    OutVoxelZ = FMath::RoundToInt(SurfaceHeightF);
    return true;
}