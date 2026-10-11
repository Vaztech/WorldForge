// CubeTerrainRuntime.cpp — CUBE-TERRAIN-02A
// Real cube column data model populated from FWorldTileData.
// No rendering, no actors, no HISM/ISM.
// Smooth terrain path in VoxelWorldManager is completely unchanged.
//
// 02A: Added BlocksPerWorldTile storage + early BlockGrid log.
//      No change to chunk data model or query API — expansion to per-block
//      columns is handled entirely in AWFCubeTerrainActor::BuildPatch.

#include "CubeTerrainRuntime.h"
#include "../Data/WorldForgeDataTypes.h"
#include "../Data/WorldForgeTags.h"

// ─────────────────────────────────────────────────────────────────
// 01C height and column constants
// ─────────────────────────────────────────────────────────────────

static constexpr int32 Cube01C_MaxSurfaceBlocks  = 32;
static constexpr int32 Cube01C_WaterLevelBlock   = 8;
static constexpr int32 Cube01C_DirtLayerDepth    = 3;
static constexpr int32 Cube01C_RockLayerDepth    = 5;
static constexpr int32 Cube01C_UndergroundDepth  = 12;
static constexpr int32 Cube01C_TotalColumnDepth  = Cube01C_MaxSurfaceBlocks + Cube01C_UndergroundDepth;

// ─────────────────────────────────────────────────────────────────

static bool BiomeMatchesAny(const FGameplayTag& Biome, std::initializer_list<const TCHAR*> Names)
{
    if (!Biome.IsValid()) return false;
    const FString BiomeStr = Biome.GetTagName().ToString();
    for (const TCHAR* Name : Names)
    {
        if (BiomeStr.Contains(Name)) return true;
    }
    return false;
}

struct FCubeTerrainSurfaceBiomeAudit
{
    int32 Tiles = 0;
    int32 Grass = 0;
    int32 Dirt = 0;
    int32 Sand = 0;
    int32 Rock = 0;
    int32 Snow = 0;
    int32 Water = 0;
    int32 Lava = 0;
    int32 Basalt = 0;
    int32 DeepRock = 0;
    int32 Air = 0;
    double ElevationSum = 0.0;
    double WaterDepthSum = 0.0;
    double RainfallSum = 0.0;
};

// ─────────────────────────────────────────────────────────────────
// ClassifyTopBlock — maps tile biome + elevation + water to surface block type.
// Priority: Water → Lava → Snow → Rock → Sand → Grass (default).
// ─────────────────────────────────────────────────────────────────

static EWorldForgeCubeBlockType ClassifyTopBlock(
    const FGameplayTag& BiomeType,
    float Elevation,
    float WaterDepth)
{
    if (BiomeMatchesAny(BiomeType, {
            TEXT("DeepOcean"), TEXT("Ocean"), TEXT("Sea"),
            TEXT("Lake"), TEXT("River"), TEXT("Water"),
            TEXT("Underwater") })
        || (WaterDepth > 0.01f && Elevation < 0.48f))
    {
        return EWorldForgeCubeBlockType::Water;
    }

    if (BiomeMatchesAny(BiomeType, { TEXT("Lava"), TEXT("Volcanic") }))
        return EWorldForgeCubeBlockType::Lava;

    if (BiomeMatchesAny(BiomeType, { TEXT("Snow"), TEXT("SnowPeak"), TEXT("Ice"), TEXT("Tundra") })
        || Elevation >= 0.90f)
        return EWorldForgeCubeBlockType::Snow;

    if (BiomeMatchesAny(BiomeType, {
            TEXT("Mountain"), TEXT("Rock"), TEXT("Cliff"),
            TEXT("Canyon"), TEXT("Highland") })
        || Elevation >= 0.75f)
        return EWorldForgeCubeBlockType::Rock;

    if (BiomeMatchesAny(BiomeType, {
            TEXT("Coast"), TEXT("Beach"), TEXT("Sand"),
            TEXT("Desert"), TEXT("Dune") })
        || Elevation <= 0.15f)
        return EWorldForgeCubeBlockType::Sand;

    return EWorldForgeCubeBlockType::Grass;
}

// ─────────────────────────────────────────────────────────────────
// CUBE-INLANDWATER-01 helpers
//
// Land tiles map Elevation (0–1) to a surface block Z with quantization.
// Inland water (Lake/River biome) must reuse the SAME mapping so river beds
// and lake shores line up with neighbouring land columns instead of being
// forced down to the ocean water level (the pre-01 behavior, which punched
// sea-level slots through mountains wherever a river or lake crossed them).
// ─────────────────────────────────────────────────────────────────

static int32 CubeLandSurfaceZFromElevation(float Elevation01, int32 BlockHeightStep)
{
    const float NormElev = FMath::Clamp(Elevation01, 0.f, 1.f);
    int32 RawZ = FMath::RoundToInt(NormElev * float(Cube01C_MaxSurfaceBlocks));
    RawZ = (RawZ / BlockHeightStep) * BlockHeightStep;
    RawZ = FMath::Max(RawZ, BlockHeightStep);
    return FMath::Clamp(RawZ, 1, Cube01C_MaxSurfaceBlocks);
}

// River water depth (0–1 from the pipeline, typically 0.05–0.25) → blocks of
// water above the carved bed. At least 1 block so the channel always shows water.
static int32 CubeInlandWaterDepthBlocks(float WaterDepth01)
{
    return FMath::Clamp(FMath::RoundToInt(WaterDepth01 * 8.0f), 1, 3);
}

// Returns the accepted lake basin whose tile bounds contain (TileX, TileY),
// or nullptr. Basins only store bounds, so biome confirmation stays with the caller.
static const FLakeBasinData* FindContainingLakeBasin(
    const TArray<FLakeBasinData>* LakeBasins, int32 TileX, int32 TileY)
{
    if (!LakeBasins) return nullptr;
    for (const FLakeBasinData& Basin : *LakeBasins)
    {
        if (TileX >= Basin.MinTileX && TileX <= Basin.MaxTileX &&
            TileY >= Basin.MinTileY && TileY <= Basin.MaxTileY)
        {
            return &Basin;
        }
    }
    return nullptr;
}

// CUBE-VOLCANO-01: true when the tile sits inside a volcano's crater bowl.
// Step03 already bakes cone height and crater carve into Tile.Elevation; this
// only decides the surface block (molten crater vs basalt slopes).
static bool IsTileInVolcanoCrater(
    const TArray<FVolcanoData>* Volcanoes, int32 TileX, int32 TileY)
{
    if (!Volcanoes) return false;
    const FVector2D TilePos(static_cast<float>(TileX), static_cast<float>(TileY));
    for (const FVolcanoData& V : *Volcanoes)
    {
        if (!V.bActive) continue;
        const float CraterRadius = V.ConeRadius * FMath::Max(0.05f, V.CraterRadiusFraction);
        if (FVector2D::Distance(TilePos, V.Center) <= CraterRadius)
        {
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────
// CubeBlockTypeToTerrainTag — maps block type to WorldForge.Terrain.* tags.
// ─────────────────────────────────────────────────────────────────

static FGameplayTag CubeBlockTypeToTerrainTag(EWorldForgeCubeBlockType BlockType)
{
    switch (BlockType)
    {
    case EWorldForgeCubeBlockType::Air:
    case EWorldForgeCubeBlockType::Water:
        return FGameplayTag();

    case EWorldForgeCubeBlockType::Grass:
    case EWorldForgeCubeBlockType::Dirt:
        return TAG_Terrain_Dirt;

    case EWorldForgeCubeBlockType::Sand:
        return TAG_Terrain_Sand;

    case EWorldForgeCubeBlockType::Rock:
    case EWorldForgeCubeBlockType::Snow:
    case EWorldForgeCubeBlockType::Basalt:
        return TAG_Terrain_Stone;

    case EWorldForgeCubeBlockType::DeepRock:
        return TAG_Terrain_Bedrock;

    case EWorldForgeCubeBlockType::Lava:
        return FGameplayTag();

    default:
        return FGameplayTag();
    }
}

// ─────────────────────────────────────────────────────────────────

void UCubeTerrainRuntime::InitializeRuntime()
{
    bRuntimeValid      = false;
    BlockHeightStep    = 4;
    BlocksPerWorldTile = 8;
    CachedGridWidth    = 0;
    CachedGridHeight   = 0;
    CubeChunks.Empty();
    SurfaceZCache.Empty();
    TerrainDepthCache.Empty();
    NumColumns    = 0;
    NumSolidCells = 0;
    NumWaterCells = 0;
    MinSurfaceZ   = 0;
    MaxSurfaceZ   = 0;
    ChunkCount    = 0;
    ClearPatchBlockVolume();

    UE_LOG(LogTemp, Log, TEXT("[CubeTerrain] Runtime initialized."));
}

// ─────────────────────────────────────────────────────────────────
// BuildFromTiles
//
// Populates one chunk per tile. Each chunk is 1x1 in XY and
// Cube01C_TotalColumnDepth cells tall.
//
// 02A: InBlocksPerWorldTile is stored and exposed to the render actor.
//      The chunk data model is unchanged — block grid expansion happens
//      in AWFCubeTerrainActor::BuildPatch using GetSurfaceZAt/GetRawBlockTypeAt
//      plus bilinear interpolation and per-block noise.
// ─────────────────────────────────────────────────────────────────

void UCubeTerrainRuntime::BuildFromTiles(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight,
    int32 InBlockHeightStep,
    int32 InBlocksPerWorldTile,
    float InTileWorldSizeUU,
    const TArray<FLakeBasinData>* InLakeBasins,
    const TArray<FVolcanoData>* InVolcanoes)
{
    const bool bHasLakeBasinData = (InLakeBasins && InLakeBasins->Num() > 0);
    const bool bHasVolcanoData = (InVolcanoes && InVolcanoes->Num() > 0);

    // CUBE-TERRAIN-02A: store block resolution before any early-out.
    BlocksPerWorldTile = FMath::Max(1, InBlocksPerWorldTile);
    // CUBE-VOXEL-SCALE: store the true tile world size for GetChunkWorldSize().
    TileWorldSizeUU = (InTileWorldSizeUU > 1.0f) ? InTileWorldSizeUU : 3200.0f;

    const float EffectiveBlockSizeUU = TileWorldSizeUU / static_cast<float>(BlocksPerWorldTile);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrain] Build start. Grid=%dx%d Tiles=%d BlockHeightStep=%d BlocksPerTile=%d "
             "Constants: MaxSurface=%d WaterLevel=%d DirtDepth=%d RockDepth=%d TotalColDepth=%d"),
        GridWidth, GridHeight, WorldTiles.Num(), InBlockHeightStep, BlocksPerWorldTile,
        Cube01C_MaxSurfaceBlocks, Cube01C_WaterLevelBlock,
        Cube01C_DirtLayerDepth, Cube01C_RockLayerDepth, Cube01C_TotalColumnDepth);

    // CUBE-TERRAIN-02A / CUBE-VOXEL-SCALE: block grid readiness log (read by render actor).
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] BlockGrid mode enabled. BlocksPerTile=%d TileWorldSizeUU=%.1f EffectiveBlockSizeUU=%.1f"),
        BlocksPerWorldTile, TileWorldSizeUU, EffectiveBlockSizeUU);

    // CUBE-INLANDWATER-01 / CUBE-VOLCANO-01: pipeline feature data availability.
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrain] Pipeline features. LakeBasins=%d Volcanoes=%d (inland water elevation=%s, crater lava=%s)"),
        InLakeBasins ? InLakeBasins->Num() : 0,
        InVolcanoes ? InVolcanoes->Num() : 0,
        bHasLakeBasinData ? TEXT("basin-aware") : TEXT("per-tile"),
        bHasVolcanoData ? TEXT("crater-only") : TEXT("legacy biome-wide"));

    if (GridWidth <= 0 || GridHeight <= 0 || WorldTiles.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeTerrain] WARNING: Build failed. Reason=Invalid grid or empty tile array."));
        bRuntimeValid = false;
        return;
    }
    if (WorldTiles.Num() != GridWidth * GridHeight)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeTerrain] WARNING: Build failed. Reason=Tile count mismatch Expected=%d Actual=%d."),
            GridWidth * GridHeight, WorldTiles.Num());
        bRuntimeValid = false;
        return;
    }

    // ── Reset and size all storage ──────────────────────────────
    // CUBE-BLOCKGRID-01: the patch volume belongs to the previous build.
    ClearPatchBlockVolume();

    CachedGridWidth  = GridWidth;
    CachedGridHeight = GridHeight;
    BlockHeightStep  = FMath::Max(1, InBlockHeightStep);

    const int32 TileCount = GridWidth * GridHeight;
    CubeChunks.Empty(TileCount);
    SurfaceZCache.SetNumZeroed(TileCount);
    TerrainDepthCache.SetNumZeroed(TileCount);

    // ── Per-build accumulators ───────────────────────────────────
    int32 StatNumColumns   = 0;
    int32 StatNumSolid     = 0;
    int32 StatNumWater     = 0;
    int32 StatMinSurface   = Cube01C_TotalColumnDepth;
    int32 StatMaxSurface   = 0;
    int32 StatChunkCount   = 0;

    int32 CntAir=0, CntGrass=0, CntDirt=0, CntSand=0, CntRock=0;
    int32 CntSnow=0, CntWater=0, CntDeepRock=0, CntLava=0, CntBasalt=0;

    int32 TopCntAir=0, TopCntGrass=0, TopCntDirt=0, TopCntSand=0, TopCntRock=0;
    int32 TopCntSnow=0, TopCntWater=0, TopCntDeepRock=0, TopCntLava=0, TopCntBasalt=0;

    int32 SandReasonCoast=0, SandReasonBeach=0, SandReasonDesert=0;
    int32 SandReasonDune=0, SandReasonLowElev=0;
    int32 LavaReasonLavaBiome=0, LavaReasonVolcanicBiome=0;

    TMap<FString, FCubeTerrainSurfaceBiomeAudit> SurfaceBiomeAudit;

    // ── Tile loop ────────────────────────────────────────────────
    for (int32 TileY = 0; TileY < GridHeight; ++TileY)
    {
        for (int32 TileX = 0; TileX < GridWidth; ++TileX)
        {
            const int32 TileIdx = TileY * GridWidth + TileX;
            const FWorldTileData& Tile = WorldTiles[TileIdx];

            EWorldForgeCubeBlockType TopBlock =
                ClassifyTopBlock(Tile.BiomeType, Tile.Elevation, Tile.WaterDepth);

            // CUBE-VOLCANO-01: molten surface only inside the crater bowl.
            // Volcanic-biome slopes become basalt. Without volcano data the
            // legacy biome-wide lava behavior is preserved.
            if (TopBlock == EWorldForgeCubeBlockType::Lava && bHasVolcanoData &&
                !IsTileInVolcanoCrater(InVolcanoes, TileX, TileY))
            {
                TopBlock = EWorldForgeCubeBlockType::Basalt;
            }

            const FString BiomeAuditName = Tile.BiomeType.IsValid()
                ? Tile.BiomeType.GetTagName().ToString()
                : FString(TEXT("<Invalid>"));
            FCubeTerrainSurfaceBiomeAudit& BiomeAudit = SurfaceBiomeAudit.FindOrAdd(BiomeAuditName);
            ++BiomeAudit.Tiles;
            BiomeAudit.ElevationSum += static_cast<double>(Tile.Elevation);
            BiomeAudit.WaterDepthSum += static_cast<double>(Tile.WaterDepth);
            BiomeAudit.RainfallSum += static_cast<double>(Tile.Rainfall);

            switch (TopBlock)
            {
            case EWorldForgeCubeBlockType::Air:      ++TopCntAir;      ++BiomeAudit.Air;      break;
            case EWorldForgeCubeBlockType::Grass:    ++TopCntGrass;    ++BiomeAudit.Grass;    break;
            case EWorldForgeCubeBlockType::Dirt:     ++TopCntDirt;     ++BiomeAudit.Dirt;     break;
            case EWorldForgeCubeBlockType::Sand:     ++TopCntSand;     ++BiomeAudit.Sand;     break;
            case EWorldForgeCubeBlockType::Rock:     ++TopCntRock;     ++BiomeAudit.Rock;     break;
            case EWorldForgeCubeBlockType::Snow:     ++TopCntSnow;     ++BiomeAudit.Snow;     break;
            case EWorldForgeCubeBlockType::Water:    ++TopCntWater;    ++BiomeAudit.Water;    break;
            case EWorldForgeCubeBlockType::DeepRock: ++TopCntDeepRock; ++BiomeAudit.DeepRock; break;
            case EWorldForgeCubeBlockType::Lava:     ++TopCntLava;     ++BiomeAudit.Lava;     break;
            case EWorldForgeCubeBlockType::Basalt:   ++TopCntBasalt;   ++BiomeAudit.Basalt;   break;
            default: break;
            }

            if (TopBlock == EWorldForgeCubeBlockType::Sand)
            {
                if (BiomeAuditName.Contains(TEXT("Coast")))  ++SandReasonCoast;
                if (BiomeAuditName.Contains(TEXT("Beach")))  ++SandReasonBeach;
                if (BiomeAuditName.Contains(TEXT("Desert"))) ++SandReasonDesert;
                if (BiomeAuditName.Contains(TEXT("Dune")))   ++SandReasonDune;
                if (Tile.Elevation <= 0.15f)                  ++SandReasonLowElev;
            }
            else if (TopBlock == EWorldForgeCubeBlockType::Lava)
            {
                if (BiomeAuditName.Contains(TEXT("Lava")))     ++LavaReasonLavaBiome;
                if (BiomeAuditName.Contains(TEXT("Volcanic"))) ++LavaReasonVolcanicBiome;
            }

            const bool bIsWaterTile = (TopBlock == EWorldForgeCubeBlockType::Water);
            const bool bIsLavaTile  = (TopBlock == EWorldForgeCubeBlockType::Lava);

            // CUBE-INLANDWATER-01: Lake/River tiles are inland water — their
            // water surface follows world-map elevation instead of the global
            // ocean level. Ocean/Sea/Coastal water keeps the flat ocean plane.
            const bool bIsInlandWaterTile = bIsWaterTile &&
                BiomeMatchesAny(Tile.BiomeType, { TEXT("Lake"), TEXT("River") });

            int32 SurfaceZ = 0;
            int32 SeabedZ  = 0;

            if (bIsInlandWaterTile)
            {
                // Bed uses the same elevation mapping as land so channel walls
                // and lake shores meet neighbouring land columns cleanly.
                // Step03 already carved river/lake floors into Tile.Elevation.
                SeabedZ = CubeLandSurfaceZFromElevation(Tile.Elevation, BlockHeightStep);

                // Basin bounds are rectangles, so gate on the Lake biome to keep
                // rivers that merely cross a basin's bounding box on their own slope.
                int32 WaterSurfZ;
                const FLakeBasinData* Basin =
                    (bHasLakeBasinData && BiomeMatchesAny(Tile.BiomeType, { TEXT("Lake") }))
                        ? FindContainingLakeBasin(InLakeBasins, TileX, TileY)
                        : nullptr;
                if (Basin)
                {
                    // Lakes are flat: one shared surface per basin.
                    WaterSurfZ = CubeLandSurfaceZFromElevation(
                        Basin->WaterSurfaceElevation01, BlockHeightStep);
                }
                else
                {
                    // Rivers (and basin-less lake tiles) slope with the terrain.
                    WaterSurfZ = SeabedZ + CubeInlandWaterDepthBlocks(Tile.WaterDepth);
                }

                SurfaceZ = FMath::Clamp(
                    FMath::Max(WaterSurfZ, SeabedZ + 1), 2, Cube01C_MaxSurfaceBlocks);
                SeabedZ = FMath::Clamp(SeabedZ, 1, SurfaceZ - 1);
            }
            else if (bIsWaterTile)
            {
                SurfaceZ = Cube01C_WaterLevelBlock;
                SeabedZ  = FMath::Clamp(
                    FMath::RoundToInt(Tile.Elevation * float(Cube01C_WaterLevelBlock)),
                    0, Cube01C_WaterLevelBlock - 1);
            }
            else
            {
                SurfaceZ = CubeLandSurfaceZFromElevation(Tile.Elevation, BlockHeightStep);
            }

            FWorldForgeCubeChunkKey Key;
            Key.ChunkX = TileX;
            Key.ChunkY = TileY;

            FWorldForgeCubeChunkData& Chunk = CubeChunks.Add(Key);
            Chunk.Init(1, 1, Cube01C_TotalColumnDepth);

            ++StatChunkCount;
            ++StatNumColumns;

            for (int32 Z = 0; Z < Cube01C_TotalColumnDepth; ++Z)
            {
                EWorldForgeCubeBlockType BlockType = EWorldForgeCubeBlockType::Air;

                if (bIsWaterTile)
                {
                    // CUBE-INLANDWATER-01: fill to SurfaceZ — the ocean plane for
                    // ocean tiles, the elevation-derived surface for inland water.
                    if (Z > SurfaceZ)
                    {
                        BlockType = EWorldForgeCubeBlockType::Air;
                    }
                    else if (Z >= SeabedZ && Z <= SurfaceZ)
                    {
                        BlockType = (Z == SeabedZ)
                            ? EWorldForgeCubeBlockType::Sand
                            : EWorldForgeCubeBlockType::Water;
                    }
                    else
                    {
                        const int32 BelowSeabed = SeabedZ - Z;
                        if (BelowSeabed <= Cube01C_DirtLayerDepth)
                            BlockType = EWorldForgeCubeBlockType::Sand;
                        else if (BelowSeabed <= Cube01C_DirtLayerDepth + Cube01C_RockLayerDepth)
                            BlockType = EWorldForgeCubeBlockType::Rock;
                        else
                            BlockType = EWorldForgeCubeBlockType::DeepRock;
                    }
                }
                else if (bIsLavaTile)
                {
                    if (Z > SurfaceZ)
                        BlockType = EWorldForgeCubeBlockType::Air;
                    else if (Z == SurfaceZ)
                        BlockType = EWorldForgeCubeBlockType::Lava;
                    else
                    {
                        const int32 Below = SurfaceZ - Z;
                        if (Below <= Cube01C_DirtLayerDepth)
                            BlockType = EWorldForgeCubeBlockType::Basalt;
                        else if (Below <= Cube01C_DirtLayerDepth + Cube01C_RockLayerDepth)
                            BlockType = EWorldForgeCubeBlockType::Rock;
                        else
                            BlockType = EWorldForgeCubeBlockType::DeepRock;
                    }
                }
                else
                {
                    if (Z > SurfaceZ)
                        BlockType = EWorldForgeCubeBlockType::Air;
                    else if (Z == SurfaceZ)
                        BlockType = TopBlock;
                    else
                    {
                        const int32 Below = SurfaceZ - Z;

                        EWorldForgeCubeBlockType SubSurface;
                        if (TopBlock == EWorldForgeCubeBlockType::Rock ||
                            TopBlock == EWorldForgeCubeBlockType::Snow)
                            SubSurface = EWorldForgeCubeBlockType::Rock;
                        else if (TopBlock == EWorldForgeCubeBlockType::Sand)
                            SubSurface = EWorldForgeCubeBlockType::Sand;
                        // CUBE-VOLCANO-01: basalt slopes are basalt underneath.
                        else if (TopBlock == EWorldForgeCubeBlockType::Basalt)
                            SubSurface = EWorldForgeCubeBlockType::Basalt;
                        else
                            SubSurface = EWorldForgeCubeBlockType::Dirt;

                        if (Below <= Cube01C_DirtLayerDepth)
                            BlockType = SubSurface;
                        else if (Below <= Cube01C_DirtLayerDepth + Cube01C_RockLayerDepth)
                            BlockType = EWorldForgeCubeBlockType::Rock;
                        else
                            BlockType = EWorldForgeCubeBlockType::DeepRock;
                    }
                }

                if (FWorldForgeCubeCell* Cell = Chunk.GetCell(0, 0, Z))
                    Cell->BlockType = BlockType;

                switch (BlockType)
                {
                case EWorldForgeCubeBlockType::Air:      ++CntAir;                      break;
                case EWorldForgeCubeBlockType::Grass:    ++CntGrass;    ++StatNumSolid; break;
                case EWorldForgeCubeBlockType::Dirt:     ++CntDirt;     ++StatNumSolid; break;
                case EWorldForgeCubeBlockType::Sand:     ++CntSand;     ++StatNumSolid; break;
                case EWorldForgeCubeBlockType::Rock:     ++CntRock;     ++StatNumSolid; break;
                case EWorldForgeCubeBlockType::Snow:     ++CntSnow;     ++StatNumSolid; break;
                case EWorldForgeCubeBlockType::Water:    ++CntWater;    ++StatNumWater;  break;
                case EWorldForgeCubeBlockType::DeepRock: ++CntDeepRock; ++StatNumSolid; break;
                case EWorldForgeCubeBlockType::Lava:     ++CntLava;     ++StatNumSolid; break;
                case EWorldForgeCubeBlockType::Basalt:   ++CntBasalt;   ++StatNumSolid; break;
                default: break;
                }
            }

            SurfaceZCache[TileIdx] = SurfaceZ;

            {
                int32 Depth = 0;
                for (int32 Z = SurfaceZ; Z >= 0; --Z)
                {
                    const FWorldForgeCubeCell* Cell = Chunk.GetCell(0, 0, Z);
                    if (!Cell || Cell->bRemoved ||
                        Cell->BlockType == EWorldForgeCubeBlockType::Air)
                        break;
                    ++Depth;
                }
                TerrainDepthCache[TileIdx] = Depth;
            }

            StatMinSurface = FMath::Min(StatMinSurface, SurfaceZ);
            StatMaxSurface = FMath::Max(StatMaxSurface, SurfaceZ);
        }
    }

    NumColumns    = StatNumColumns;
    NumSolidCells = StatNumSolid;
    NumWaterCells = StatNumWater;
    MinSurfaceZ   = StatMinSurface;
    MaxSurfaceZ   = StatMaxSurface;
    ChunkCount    = StatChunkCount;
    bRuntimeValid = true;

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrain] Build complete. Columns=%d Chunks=%d SolidCells=%d WaterCells=%d SurfaceZ=[%d,%d] BlocksPerTile=%d"),
        NumColumns, ChunkCount, NumSolidCells, NumWaterCells, MinSurfaceZ, MaxSurfaceZ, BlocksPerWorldTile);
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrain] BlockTypes Air=%d Grass=%d Dirt=%d Sand=%d Rock=%d Snow=%d Water=%d DeepRock=%d Lava=%d Basalt=%d"),
        CntAir, CntGrass, CntDirt, CntSand, CntRock, CntSnow, CntWater, CntDeepRock, CntLava, CntBasalt);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainSurfaceAudit] TopBlocks Grass=%d Sand=%d Rock=%d Snow=%d Water=%d Lava=%d Dirt=%d Basalt=%d DeepRock=%d Air=%d"),
        TopCntGrass, TopCntSand, TopCntRock, TopCntSnow, TopCntWater, TopCntLava,
        TopCntDirt, TopCntBasalt, TopCntDeepRock, TopCntAir);
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainSurfaceAudit] SandReasons Coast=%d Beach=%d Desert=%d Dune=%d LowElev=%d"),
        SandReasonCoast, SandReasonBeach, SandReasonDesert, SandReasonDune, SandReasonLowElev);
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainSurfaceAudit] LavaReasons LavaBiome=%d VolcanicBiome=%d"),
        LavaReasonLavaBiome, LavaReasonVolcanicBiome);

    for (const TPair<FString, FCubeTerrainSurfaceBiomeAudit>& BiomePair : SurfaceBiomeAudit)
    {
        const FCubeTerrainSurfaceBiomeAudit& Audit = BiomePair.Value;
        const double InvTiles = Audit.Tiles > 0 ? (1.0 / static_cast<double>(Audit.Tiles)) : 0.0;
        UE_LOG(LogTemp, Log,
            TEXT("[CubeTerrainSurfaceAudit] Biome=%s Tiles=%d Grass=%d Sand=%d Rock=%d Snow=%d Water=%d Lava=%d AvgElev=%.3f AvgWater=%.3f AvgRain=%.3f"),
            *BiomePair.Key,
            Audit.Tiles,
            Audit.Grass,
            Audit.Sand,
            Audit.Rock,
            Audit.Snow,
            Audit.Water,
            Audit.Lava,
            static_cast<float>(Audit.ElevationSum * InvTiles),
            static_cast<float>(Audit.WaterDepthSum * InvTiles),
            static_cast<float>(Audit.RainfallSum * InvTiles));
    }
}

// ─────────────────────────────────────────────────────────────────

void UCubeTerrainRuntime::Reset()
{
    bRuntimeValid      = false;
    CachedGridWidth    = 0;
    CachedGridHeight   = 0;
    BlockHeightStep    = 4;
    BlocksPerWorldTile = 8;
    TileWorldSizeUU    = 3200.0f;
    CubeChunks.Empty();
    SurfaceZCache.Empty();
    TerrainDepthCache.Empty();
    NumColumns    = 0;
    NumSolidCells = 0;
    NumWaterCells = 0;
    MinSurfaceZ   = 0;
    MaxSurfaceZ   = 0;
    ChunkCount    = 0;
    ClearPatchBlockVolume();

    UE_LOG(LogTemp, Log, TEXT("[CubeTerrain] Reset."));
}

// ─────────────────────────────────────────────────────────────────
// Query methods — backed by real CubeChunks data after BuildFromTiles.
// ─────────────────────────────────────────────────────────────────

FGameplayTag UCubeTerrainRuntime::GetBlockTypeAt(const FIntVector& GridPos) const
{
    if (!bRuntimeValid) return FGameplayTag();

    if (GridPos.X < 0 || GridPos.X >= CachedGridWidth ||
        GridPos.Y < 0 || GridPos.Y >= CachedGridHeight ||
        GridPos.Z < 0 || GridPos.Z >= Cube01C_TotalColumnDepth)
    {
        return FGameplayTag();
    }

    const FWorldForgeCubeChunkKey Key{ GridPos.X, GridPos.Y };
    const FWorldForgeCubeChunkData* Chunk = CubeChunks.Find(Key);
    if (!Chunk || !Chunk->IsValid()) return FGameplayTag();

    const FWorldForgeCubeCell* Cell = Chunk->GetCell(0, 0, GridPos.Z);
    if (!Cell || Cell->bRemoved) return FGameplayTag();

    return CubeBlockTypeToTerrainTag(Cell->BlockType);
}

// ─────────────────────────────────────────────────────────────────

int32 UCubeTerrainRuntime::GetSurfaceZAt(const FIntPoint& GridXY) const
{
    if (!bRuntimeValid) return 0;
    if (GridXY.X < 0 || GridXY.X >= CachedGridWidth ||
        GridXY.Y < 0 || GridXY.Y >= CachedGridHeight) return 0;

    const int32 Idx = GridXY.Y * CachedGridWidth + GridXY.X;
    return SurfaceZCache.IsValidIndex(Idx) ? SurfaceZCache[Idx] : 0;
}

// ─────────────────────────────────────────────────────────────────

int32 UCubeTerrainRuntime::GetTerrainDepthAt(const FIntPoint& GridXY) const
{
    if (!bRuntimeValid) return 0;
    if (GridXY.X < 0 || GridXY.X >= CachedGridWidth ||
        GridXY.Y < 0 || GridXY.Y >= CachedGridHeight) return 0;

    const int32 Idx = GridXY.Y * CachedGridWidth + GridXY.X;
    return TerrainDepthCache.IsValidIndex(Idx) ? TerrainDepthCache[Idx] : 0;
}

// ─────────────────────────────────────────────────────────────────

bool UCubeTerrainRuntime::RemoveBlockAt(const FIntVector& GridPos)
{
    if (!bRuntimeValid) return false;

    if (GridPos.X < 0 || GridPos.X >= CachedGridWidth ||
        GridPos.Y < 0 || GridPos.Y >= CachedGridHeight ||
        GridPos.Z < 0 || GridPos.Z >= Cube01C_TotalColumnDepth)
    {
        return false;
    }

    const FWorldForgeCubeChunkKey Key{ GridPos.X, GridPos.Y };
    FWorldForgeCubeChunkData* Chunk = CubeChunks.Find(Key);
    if (!Chunk || !Chunk->IsValid()) return false;

    FWorldForgeCubeCell* Cell = Chunk->GetCell(0, 0, GridPos.Z);
    if (!Cell) return false;

    if (Cell->BlockType == EWorldForgeCubeBlockType::Air || Cell->bRemoved) return false;
    if (Cell->BlockType == EWorldForgeCubeBlockType::Lava ||
        Cell->BlockType == EWorldForgeCubeBlockType::Water) return false;

    Cell->bRemoved = true;

    const int32 TileIdx = GridPos.Y * CachedGridWidth + GridPos.X;
    if (!SurfaceZCache.IsValidIndex(TileIdx))
    {
        return true;
    }

    if (SurfaceZCache[TileIdx] == GridPos.Z)
    {
        int32 NewSurfaceZ = 0;
        for (int32 Z = GridPos.Z - 1; Z >= 0; --Z)
        {
            const FWorldForgeCubeCell* Below = Chunk->GetCell(0, 0, Z);
            if (Below && !Below->bRemoved &&
                Below->BlockType != EWorldForgeCubeBlockType::Air)
            {
                NewSurfaceZ = Z;
                break;
            }
        }
        SurfaceZCache[TileIdx] = NewSurfaceZ;
    }

    // Removing any block in the column (not only the surface one) can shorten
    // the contiguous depth, so always recompute it.
    int32 NewDepth = 0;
    for (int32 Z = SurfaceZCache[TileIdx]; Z >= 0; --Z)
    {
        const FWorldForgeCubeCell* Below = Chunk->GetCell(0, 0, Z);
        if (!Below || Below->bRemoved ||
            Below->BlockType == EWorldForgeCubeBlockType::Air) break;
        ++NewDepth;
    }
    if (TerrainDepthCache.IsValidIndex(TileIdx))
        TerrainDepthCache[TileIdx] = NewDepth;

    return true;
}

// ─────────────────────────────────────────────────────────────────

EWorldForgeCubeBlockType UCubeTerrainRuntime::GetRawBlockTypeAt(const FIntVector& GridPos) const
{
    if (!bRuntimeValid) return EWorldForgeCubeBlockType::Air;

    if (GridPos.X < 0 || GridPos.X >= CachedGridWidth ||
        GridPos.Y < 0 || GridPos.Y >= CachedGridHeight ||
        GridPos.Z < 0 || GridPos.Z >= Cube01C_TotalColumnDepth)
    {
        return EWorldForgeCubeBlockType::Air;
    }

    const FWorldForgeCubeChunkKey Key{ GridPos.X, GridPos.Y };
    const FWorldForgeCubeChunkData* Chunk = CubeChunks.Find(Key);
    if (!Chunk || !Chunk->IsValid()) return EWorldForgeCubeBlockType::Air;

    const FWorldForgeCubeCell* Cell = Chunk->GetCell(0, 0, GridPos.Z);
    if (!Cell || Cell->bRemoved) return EWorldForgeCubeBlockType::Air;

    return Cell->BlockType;
}

// ─────────────────────────────────────────────────────────────────
// CUBE-BLOCKGRID-01: per-block patch volume
// ─────────────────────────────────────────────────────────────────

static int32 CubeFloorDiv(int32 Value, int32 Divisor)
{
    return (Value >= 0) ? (Value / Divisor) : -((-Value + Divisor - 1) / Divisor);
}

int32 UCubeTerrainRuntime::GetSeaLevelBlockZ()
{
    return Cube01C_WaterLevelBlock;
}

void UCubeTerrainRuntime::SetPatchBlockVolume(
    const FIntPoint& InPatchMinGlobalBlock,
    int32 InBlocksX,
    int32 InBlocksY,
    int32 InMaxZ,
    TArray<uint8>&& InVolume)
{
    const int32 ExpectedNum = InBlocksX * InBlocksY * (InMaxZ + 1);
    if (InBlocksX <= 0 || InBlocksY <= 0 || InMaxZ < 0 || InVolume.Num() != ExpectedNum)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeBlockGrid] SetPatchBlockVolume rejected. Blocks=%dx%d MaxZ=%d VolumeNum=%d Expected=%d"),
            InBlocksX, InBlocksY, InMaxZ, InVolume.Num(), ExpectedNum);
        ClearPatchBlockVolume();
        return;
    }

    PatchMinGlobalBlock = InPatchMinGlobalBlock;
    PatchBlocksX = InBlocksX;
    PatchBlocksY = InBlocksY;
    PatchMaxZ = InMaxZ;
    PatchVolume = MoveTemp(InVolume);
    RemovedPatchBlocks.Empty();

    PatchSurfaceZ.SetNumUninitialized(PatchBlocksX * PatchBlocksY);
    for (int32 LocalY = 0; LocalY < PatchBlocksY; ++LocalY)
    {
        for (int32 LocalX = 0; LocalX < PatchBlocksX; ++LocalX)
        {
            RecomputePatchColumnSurface(LocalX, LocalY);
        }
    }

    const int32 BPT = FMath::Max(1, BlocksPerWorldTile);
    if (((CachedGridWidth * BPT) % 2) != 0 || ((CachedGridHeight * BPT) % 2) != 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeBlockGrid] Grid=%dx%d BlocksPerTile=%d gives an odd block count; render blocks are offset half a cell from the selection grid."),
            CachedGridWidth, CachedGridHeight, BPT);
    }

    const FIntPoint MinCell = PatchMinGlobalBlock - GetGridCellOffset();
    UE_LOG(LogTemp, Log,
        TEXT("[CubeBlockGrid] Patch volume stored. Blocks=%dx%d MaxZ=%d MinGlobalBlock=(%d,%d) GridCells=(%d,%d)->(%d,%d)"),
        PatchBlocksX, PatchBlocksY, PatchMaxZ,
        PatchMinGlobalBlock.X, PatchMinGlobalBlock.Y,
        MinCell.X, MinCell.Y,
        MinCell.X + PatchBlocksX - 1, MinCell.Y + PatchBlocksY - 1);
}

void UCubeTerrainRuntime::ClearPatchBlockVolume()
{
    PatchMinGlobalBlock = FIntPoint::ZeroValue;
    PatchBlocksX = 0;
    PatchBlocksY = 0;
    PatchMaxZ = 0;
    PatchVolume.Empty();
    PatchSurfaceZ.Empty();
    RemovedPatchBlocks.Empty();
}

void UCubeTerrainRuntime::RecomputePatchColumnSurface(int32 LocalX, int32 LocalY)
{
    int32 TopZ = INDEX_NONE;
    for (int32 Z = PatchMaxZ; Z >= 0; --Z)
    {
        if (PatchVolume[PatchVolumeIndex(LocalX, LocalY, Z)] != static_cast<uint8>(EWorldForgeCubeBlockType::Air))
        {
            TopZ = Z;
            break;
        }
    }
    PatchSurfaceZ[(LocalY * PatchBlocksX) + LocalX] = static_cast<int16>(TopZ);
}

FIntPoint UCubeTerrainRuntime::GetGridCellOffset() const
{
    const int32 BPT = FMath::Max(1, BlocksPerWorldTile);
    return FIntPoint((CachedGridWidth * BPT) / 2, (CachedGridHeight * BPT) / 2);
}

FIntPoint UCubeTerrainRuntime::GridCellToTile(const FIntPoint& GridCell) const
{
    const int32 BPT = FMath::Max(1, BlocksPerWorldTile);
    const FIntPoint GlobalBlock = GridCell + GetGridCellOffset();
    return FIntPoint(CubeFloorDiv(GlobalBlock.X, BPT), CubeFloorDiv(GlobalBlock.Y, BPT));
}

FIntPoint UCubeTerrainRuntime::TileToCenterGridCell(const FIntPoint& TileXY) const
{
    const int32 BPT = FMath::Max(1, BlocksPerWorldTile);
    const FIntPoint GlobalBlock(TileXY.X * BPT + BPT / 2, TileXY.Y * BPT + BPT / 2);
    return GlobalBlock - GetGridCellOffset();
}

bool UCubeTerrainRuntime::GridCellToPatchLocal(const FIntPoint& GridCell, FIntPoint& OutPatchLocal) const
{
    if (!HasPatchBlockVolume())
    {
        return false;
    }

    OutPatchLocal = GridCell + GetGridCellOffset() - PatchMinGlobalBlock;
    return OutPatchLocal.X >= 0 && OutPatchLocal.Y >= 0 &&
           OutPatchLocal.X < PatchBlocksX && OutPatchLocal.Y < PatchBlocksY;
}

EWorldForgeCubeBlockType UCubeTerrainRuntime::GetGridBlockRaw(const FIntVector& GridCell) const
{
    if (!bRuntimeValid || GridCell.Z < 0)
    {
        return EWorldForgeCubeBlockType::Air;
    }

    FIntPoint Local;
    if (GridCellToPatchLocal(FIntPoint(GridCell.X, GridCell.Y), Local))
    {
        if (GridCell.Z > PatchMaxZ)
        {
            return EWorldForgeCubeBlockType::Air;
        }
        return static_cast<EWorldForgeCubeBlockType>(PatchVolume[PatchVolumeIndex(Local.X, Local.Y, GridCell.Z)]);
    }

    const FIntPoint Tile = GridCellToTile(FIntPoint(GridCell.X, GridCell.Y));
    return GetRawBlockTypeAt(FIntVector(Tile.X, Tile.Y, GridCell.Z));
}

FGameplayTag UCubeTerrainRuntime::GetGridBlockTag(const FIntVector& GridCell) const
{
    return CubeBlockTypeToTerrainTag(GetGridBlockRaw(GridCell));
}

bool UCubeTerrainRuntime::TryGetGridSurfaceZ(const FIntPoint& GridCell, int32& OutSurfaceZ) const
{
    OutSurfaceZ = 0;
    if (!bRuntimeValid)
    {
        return false;
    }

    int32 SurfaceZ = INDEX_NONE;
    EWorldForgeCubeBlockType TopType = EWorldForgeCubeBlockType::Air;

    FIntPoint Local;
    if (GridCellToPatchLocal(GridCell, Local))
    {
        SurfaceZ = PatchSurfaceZ[(Local.Y * PatchBlocksX) + Local.X];
        if (SurfaceZ == INDEX_NONE)
        {
            return false;
        }
        TopType = static_cast<EWorldForgeCubeBlockType>(PatchVolume[PatchVolumeIndex(Local.X, Local.Y, SurfaceZ)]);
    }
    else
    {
        const FIntPoint Tile = GridCellToTile(GridCell);
        if (Tile.X < 0 || Tile.Y < 0 || Tile.X >= CachedGridWidth || Tile.Y >= CachedGridHeight)
        {
            return false;
        }
        SurfaceZ = GetSurfaceZAt(Tile);
        TopType = GetRawBlockTypeAt(FIntVector(Tile.X, Tile.Y, SurfaceZ));
    }

    if (TopType == EWorldForgeCubeBlockType::Air ||
        TopType == EWorldForgeCubeBlockType::Water ||
        TopType == EWorldForgeCubeBlockType::Lava)
    {
        return false;
    }

    OutSurfaceZ = SurfaceZ;
    return true;
}

int32 UCubeTerrainRuntime::GetGridTerrainDepth(const FIntPoint& GridCell) const
{
    int32 SurfaceZ = 0;
    if (!TryGetGridSurfaceZ(GridCell, SurfaceZ))
    {
        return 0;
    }

    int32 Depth = 0;
    for (int32 Z = SurfaceZ; Z >= 0; --Z)
    {
        const EWorldForgeCubeBlockType Type = GetGridBlockRaw(FIntVector(GridCell.X, GridCell.Y, Z));
        if (Type == EWorldForgeCubeBlockType::Air ||
            Type == EWorldForgeCubeBlockType::Water ||
            Type == EWorldForgeCubeBlockType::Lava)
        {
            break;
        }
        ++Depth;
    }
    return Depth;
}

bool UCubeTerrainRuntime::RemoveGridBlock(const FIntVector& GridCell)
{
    if (!bRuntimeValid || GridCell.Z <= 0 || GridCell.Z > PatchMaxZ)
    {
        return false;
    }

    FIntPoint Local;
    if (!GridCellToPatchLocal(FIntPoint(GridCell.X, GridCell.Y), Local))
    {
        return false;
    }

    const int32 Index = PatchVolumeIndex(Local.X, Local.Y, GridCell.Z);
    const EWorldForgeCubeBlockType Type = static_cast<EWorldForgeCubeBlockType>(PatchVolume[Index]);
    if (Type == EWorldForgeCubeBlockType::Air ||
        Type == EWorldForgeCubeBlockType::Water ||
        Type == EWorldForgeCubeBlockType::Lava)
    {
        return false;
    }

    RemovedPatchBlocks.Add(GridCell, PatchVolume[Index]);
    PatchVolume[Index] = static_cast<uint8>(EWorldForgeCubeBlockType::Air);
    RecomputePatchColumnSurface(Local.X, Local.Y);
    return true;
}

bool UCubeTerrainRuntime::RestoreGridBlock(const FIntVector& GridCell)
{
    const uint8* OriginalType = RemovedPatchBlocks.Find(GridCell);
    if (!OriginalType || GridCell.Z < 0 || GridCell.Z > PatchMaxZ)
    {
        return false;
    }

    FIntPoint Local;
    if (!GridCellToPatchLocal(FIntPoint(GridCell.X, GridCell.Y), Local))
    {
        return false;
    }

    const int32 Index = PatchVolumeIndex(Local.X, Local.Y, GridCell.Z);
    if (PatchVolume[Index] != static_cast<uint8>(EWorldForgeCubeBlockType::Air))
    {
        return false;
    }

    PatchVolume[Index] = *OriginalType;
    RemovedPatchBlocks.Remove(GridCell);
    RecomputePatchColumnSurface(Local.X, Local.Y);
    return true;
}
