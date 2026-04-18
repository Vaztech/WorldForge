#include "VoxelWorldManager.h"

#include "GameplayTagContainer.h"

float UVoxelWorldManager::GetNormalizedElevationForTerrainCell(FIntPoint GridCell) const
{
    if (!IsValidTerrainCell(GridCell))
    {
        return 0.0f;
    }

    const int32 Index = TerrainIndex(GridCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return 0.0f;
    }

    const float Height = CachedSurfaceHeightsVoxels[Index];
    const float MinH = 0.0f;
    const float MaxH = 256.0f;

    return FMath::Clamp((Height - MinH) / (MaxH - MinH), 0.0f, 1.0f);
}

EWorldForgeTerrainType UVoxelWorldManager::ClassifyTerrainCellInternal(FIntPoint GridCell) const
{
    if (!IsValidTerrainCell(GridCell))
    {
        return EWorldForgeTerrainType::Grass;
    }

    const int32 Index = TerrainIndex(GridCell);

    if (CachedBiomeTags.IsValidIndex(Index))
    {
        const FGameplayTag& Biome = CachedBiomeTags[Index];

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Desert")))))
        {
            return EWorldForgeTerrainType::Sand;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")))))
        {
            return EWorldForgeTerrainType::Rock;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Tundra")))))
        {
            return EWorldForgeTerrainType::Snow;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")))))
        {
            return EWorldForgeTerrainType::Sand;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")))))
        {
            return EWorldForgeTerrainType::Grass;
        }
    }

    const float Elev = GetNormalizedElevationForTerrainCell(GridCell);

    if (Elev < SandMaxElevationNormalized)
    {
        return EWorldForgeTerrainType::Sand;
    }

    if (Elev > SnowMinElevationNormalized)
    {
        return EWorldForgeTerrainType::Snow;
    }

    if (Elev > RockMinElevationNormalized)
    {
        return EWorldForgeTerrainType::Rock;
    }

    return EWorldForgeTerrainType::Grass;
}

bool UVoxelWorldManager::GetTerrainTypeAtCell(FIntPoint GridCell, EWorldForgeTerrainType& OutTerrainType) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    OutTerrainType = ClassifyTerrainCellInternal(GridCell);
    return true;
}

bool UVoxelWorldManager::GetTerrainTypeAtVoxel(FIntPoint VoxelXY, EWorldForgeTerrainType& OutTerrainType) const
{
    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    return GetTerrainTypeAtCell(ChunkCell, OutTerrainType);
}

float UVoxelWorldManager::ComputeTerrainSlopeDegreesInternal(FIntPoint GridCell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return 0.0f;
    }

    const FIntPoint Center = ClampTerrainCellToBounds(GridCell);
    const FIntPoint North = ClampTerrainCellToBounds(FIntPoint(Center.X, Center.Y - 1));
    const FIntPoint South = ClampTerrainCellToBounds(FIntPoint(Center.X, Center.Y + 1));
    const FIntPoint West = ClampTerrainCellToBounds(FIntPoint(Center.X - 1, Center.Y));
    const FIntPoint East = ClampTerrainCellToBounds(FIntPoint(Center.X + 1, Center.Y));

    const int32 CenterIndex = TerrainIndex(Center);
    const int32 NorthIndex = TerrainIndex(North);
    const int32 SouthIndex = TerrainIndex(South);
    const int32 WestIndex = TerrainIndex(West);
    const int32 EastIndex = TerrainIndex(East);

    if (!CachedSurfaceHeightsVoxels.IsValidIndex(CenterIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(NorthIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(SouthIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(WestIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(EastIndex))
    {
        return 0.0f;
    }

    const float RVS = GetRenderVoxelSize();
    const float CenterHeightWorld = CachedSurfaceHeightsVoxels[CenterIndex] * RVS;
    const float NorthHeightWorld = CachedSurfaceHeightsVoxels[NorthIndex] * RVS;
    const float SouthHeightWorld = CachedSurfaceHeightsVoxels[SouthIndex] * RVS;
    const float WestHeightWorld = CachedSurfaceHeightsVoxels[WestIndex] * RVS;
    const float EastHeightWorld = CachedSurfaceHeightsVoxels[EastIndex] * RVS;

    const float CellRunWorld = GetTerrainCellWorldSize();

    const float GradientX = (EastHeightWorld - WestHeightWorld) / (2.0f * CellRunWorld);
    const float GradientY = (SouthHeightWorld - NorthHeightWorld) / (2.0f * CellRunWorld);

    const float GradientMagnitude = FMath::Sqrt((GradientX * GradientX) + (GradientY * GradientY));
    return FMath::RadiansToDegrees(FMath::Atan(GradientMagnitude));
}

bool UVoxelWorldManager::GetTerrainSlopeAtCell(FIntPoint GridCell, float& OutSlopeDegrees) const
{
    OutSlopeDegrees = 0.0f;

    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    OutSlopeDegrees = ComputeTerrainSlopeDegreesInternal(GridCell);
    return true;
}

bool UVoxelWorldManager::GetTerrainSlopeAtVoxel(FIntPoint VoxelXY, float& OutSlopeDegrees) const
{
    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    return GetTerrainSlopeAtCell(ChunkCell, OutSlopeDegrees);
}