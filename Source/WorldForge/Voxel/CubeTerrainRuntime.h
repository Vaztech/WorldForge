// CubeTerrainRuntime.h — CUBE-TERRAIN-02A
// Parallel cube terrain runtime, owned by UVoxelWorldManager.
// Disabled unless ETerrainMode::CubePrototype is explicitly passed.
// Smooth terrain default is unchanged. No rendering here.
//
// 02A additions over 01C:
//   - BlocksPerWorldTile setting stored at build time (default 8).
//     Exposed via GetBlocksPerWorldTile() for the render actor.
//   - BuildFromTiles() accepts InBlocksPerWorldTile so the pipeline
//     can override the default; existing callers with 4 args are
//     unaffected (default parameter = 8).
//   - All chunk data, query methods, and mining API unchanged.
//
// 01C additions over 01B:
//   - BuildFromTiles() populates real per-tile column data from FWorldTileData.
//   - SurfaceZCache and TerrainDepthCache enable O(1) surface/depth queries.
//   - All four query methods return real data from CubeChunks.
//   - RemoveBlockAt marks cells removed and updates surface/depth caches.
//   - Build stats tracked: Columns, Chunks, SolidCells, WaterCells, SurfaceZ range.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "CubeTerrainTypes.h"
#include "../Data/WorldForgeDataTypes.h"
#include "CubeTerrainRuntime.generated.h"

struct FWorldTileData;

// ─────────────────────────────────────────────────────────────────
// UCubeTerrainRuntime
//
// Responsibilities:
//   - Hold BlockHeightStep, BlocksPerWorldTile, and grid dimensions.
//   - Accept BuildFromTiles() — populates CubeChunks from tile elevation
//     and biome data. One chunk per tile, 1x1 footprint, full Z column.
//   - SurfaceZCache[TileY * GridWidth + TileX] = topmost solid/water Z.
//   - TerrainDepthCache[same index] = contiguous solid depth from surface.
//   - Query methods: GetBlockTypeAt, GetSurfaceZAt, GetTerrainDepthAt,
//     RemoveBlockAt — all backed by real data after Build.
//   - IsValid() returns true only after a successful Build call.
//   - Reset() tears down all state.
//
// 02A: BlocksPerWorldTile is read by AWFCubeTerrainActor::BuildPatch to
//      expand each world tile into an 8x8 local block grid at 100 UU scale.
// ─────────────────────────────────────────────────────────────────

UCLASS()
class WORLDFORGE_API UCubeTerrainRuntime : public UObject
{
    GENERATED_BODY()

public:
    // Called by UVoxelWorldManager::Initialize.
    void InitializeRuntime();

    // Primary build entry point. Called from GenerateTerrainFromTiles when
    // ETerrainMode::CubePrototype is selected. Populates CubeChunks from
    // tile data and caches surface/depth lookup arrays.
    // InBlocksPerWorldTile: local block resolution per world tile (default 8 for 02A).
    // CUBE-INLANDWATER-01 / CUBE-VOLCANO-01: optional pipeline feature data.
    //   InLakeBasins — accepted lake basins (flat per-basin water surface elevation).
    //   InVolcanoes  — volcano descriptors (crater lava cap vs basalt slopes).
    // Null/empty inputs preserve the previous behavior exactly.
    void BuildFromTiles(
        const TArray<FWorldTileData>& WorldTiles,
        int32 GridWidth,
        int32 GridHeight,
        int32 InBlockHeightStep,
        int32 InBlocksPerWorldTile = 8,
        float InTileWorldSizeUU = 3200.0f,
        const TArray<FLakeBasinData>* InLakeBasins = nullptr,
        const TArray<FVolcanoData>* InVolcanoes = nullptr);

    // Tear down all runtime state. Safe to call at any time.
    void Reset();

    // True after a successful BuildFromTiles call.
    bool IsValid() const { return bRuntimeValid; }

    // ── Block queries ────────────────────────────────────────────

    // Returns the block type tag at a global grid position (X=TileX, Y=TileY, Z=blockZ).
    // Returns invalid tag (Air equivalent) for out-of-bounds, air, water, or removed cells.
    FGameplayTag GetBlockTypeAt(const FIntVector& GridPos) const;

    // Returns the raw EWorldForgeCubeBlockType at a global grid position.
    // Used by CUBE-TERRAIN-01D HISM rendering to select the correct component per block.
    // Returns Air for out-of-bounds, unbuilt runtime, or removed cells.
    EWorldForgeCubeBlockType GetRawBlockTypeAt(const FIntVector& GridPos) const;

    // Returns the topmost solid or water surface Z at this XY column.
    // Returns 0 for out-of-bounds or unbuilt runtime.
    int32 GetSurfaceZAt(const FIntPoint& GridXY) const;

    // Returns the contiguous solid depth from the surface downward.
    // Returns 0 for out-of-bounds or unbuilt runtime.
    int32 GetTerrainDepthAt(const FIntPoint& GridXY) const;

    // Marks a cell as removed (Air). Updates SurfaceZCache and TerrainDepthCache
    // if the removed cell was the surface block.
    // Returns false for: out-of-bounds, Air/already-removed, DeepRock/Lava (undiggable),
    // or unbuilt runtime.
    bool RemoveBlockAt(const FIntVector& GridPos);

    // ── Accessors ────────────────────────────────────────────────
    int32 GetBlockHeightStep()    const { return BlockHeightStep; }
    int32 GetGridWidth()          const { return CachedGridWidth; }
    int32 GetGridHeight()         const { return CachedGridHeight; }
    // CUBE-TERRAIN-02A: Local block resolution per world tile.
    int32 GetBlocksPerWorldTile() const { return BlocksPerWorldTile; }

    // Returns the world size of one world tile/chunk in Unreal Units.
    // CUBE-VOXEL-SCALE: this now returns the TRUE tile world size stored at build time
    // (default 3200), NOT BlocksPerWorldTile*100. The old formula returned 800 for the
    // 8-block default and would have shifted to 1600 at 16 blocks — wrong in both cases,
    // since world->tile snapping (TryGetCubeSnapForWorldPos) must divide by the real tile
    // size. Returning the stored tile size makes that path correct and independent of the
    // render subdivision count.
    float GetChunkWorldSize() const
    {
        return TileWorldSizeUU > 1.0f ? TileWorldSizeUU : 3200.0f;
    }

    // Effective world size of one render block = TileWorldSizeUU / BlocksPerWorldTile.
    float GetEffectiveBlockSizeUU() const
    {
        return GetChunkWorldSize() / static_cast<float>(FMath::Max(1, BlocksPerWorldTile));
    }

    // ── Build stats (valid after IsValid() == true) ──────────────
    int32 GetNumColumns()    const { return NumColumns; }
    int32 GetNumSolidCells() const { return NumSolidCells; }
    int32 GetNumWaterCells() const { return NumWaterCells; }
    int32 GetMinSurfaceZ()   const { return MinSurfaceZ; }
    int32 GetMaxSurfaceZ()   const { return MaxSurfaceZ; }
    int32 GetChunkCount()    const { return ChunkCount; }

    // Ocean surface block Z used for ocean tiles. Shared with the render actor's edge skirt.
    static int32 GetSeaLevelBlockZ();

    // ── CUBE-BLOCKGRID-01: per-block patch volume ────────────────
    //
    // The tile columns above are one cell per 32m world tile. The render actor
    // expands each tile into BlocksPerWorldTile x BlocksPerWorldTile blocks with
    // blended heights. The resulting block volume for the rendered patch is
    // stored here and is the authoritative data for everything inside the patch:
    // mining, block queries, surface queries, and chunk mesh rebuilds.
    //
    // Grid cells are the coordinates used by selection/mining/farming
    // (UVoxelWorldManager::WorldToGrid3D in CubePrototype mode). They are
    // origin-centered: GridCell = GlobalBlock - (GridWidth * BlocksPerWorldTile / 2).
    // Z is the block layer, identical to the tile column Z.

    // Called by FWorldForgeCubeMeshBuilder after it builds the patch volume.
    // Volume layout: Index = Z * BlocksX * BlocksY + LocalY * BlocksX + LocalX.
    void SetPatchBlockVolume(
        const FIntPoint& InPatchMinGlobalBlock,
        int32 InBlocksX,
        int32 InBlocksY,
        int32 InMaxZ,
        TArray<uint8>&& InVolume);

    void ClearPatchBlockVolume();

    bool HasPatchBlockVolume() const { return PatchBlocksX > 0 && PatchBlocksY > 0 && PatchVolume.Num() > 0; }
    const TArray<uint8>& GetPatchBlockVolume() const { return PatchVolume; }
    FIntPoint GetPatchMinGlobalBlock() const { return PatchMinGlobalBlock; }
    int32 GetPatchBlocksX() const { return PatchBlocksX; }
    int32 GetPatchBlocksY() const { return PatchBlocksY; }
    int32 GetPatchMaxZ() const { return PatchMaxZ; }

    // Grid cell <-> global block / world tile conversions.
    FIntPoint GetGridCellOffset() const;
    FIntPoint GridCellToTile(const FIntPoint& GridCell) const;
    FIntPoint TileToCenterGridCell(const FIntPoint& TileXY) const;
    bool GridCellToPatchLocal(const FIntPoint& GridCell, FIntPoint& OutPatchLocal) const;

    // Block queries in grid cells. Inside the patch they read the block volume;
    // outside it they fall back to the tile column that contains the cell.
    EWorldForgeCubeBlockType GetGridBlockRaw(const FIntVector& GridCell) const;
    FGameplayTag GetGridBlockTag(const FIntVector& GridCell) const;

    // Topmost non-air block Z of the column. Returns false for out-of-bounds
    // columns and for columns whose top block is Water or Lava.
    bool TryGetGridSurfaceZ(const FIntPoint& GridCell, int32& OutSurfaceZ) const;

    // Contiguous solid blocks from the column surface downward.
    int32 GetGridTerrainDepth(const FIntPoint& GridCell) const;

    // Removes one block inside the patch. Air, Water, Lava and the Z=0 floor cannot be removed.
    bool RemoveGridBlock(const FIntVector& GridCell);

    // Restores a block previously removed by RemoveGridBlock.
    bool RestoreGridBlock(const FIntVector& GridCell);

private:
    int32 PatchVolumeIndex(int32 LocalX, int32 LocalY, int32 Z) const
    {
        return (Z * PatchBlocksY * PatchBlocksX) + (LocalY * PatchBlocksX) + LocalX;
    }

    void RecomputePatchColumnSurface(int32 LocalX, int32 LocalY);

    bool  bRuntimeValid = false;
    int32 BlockHeightStep = 4;
    int32 CachedGridWidth = 0;
    int32 CachedGridHeight = 0;
    // CUBE-TERRAIN-02A: blocks per world tile, set at BuildFromTiles time.
    int32 BlocksPerWorldTile = 8;

    // CUBE-VOXEL-SCALE: true world size of one tile, stored at BuildFromTiles time.
    // GetChunkWorldSize() returns this so world->tile snapping uses the real size.
    float TileWorldSizeUU = 3200.0f;

    // Chunk storage — keyed by (TileX, TileY). One chunk per tile, 1x1xDepth.
    TMap<FWorldForgeCubeChunkKey, FWorldForgeCubeChunkData> CubeChunks;

    // O(1) surface and depth caches — indexed by TileY * GridWidth + TileX.
    // Kept in sync with CubeChunks by RemoveBlockAt.
    TArray<int32> SurfaceZCache;
    TArray<int32> TerrainDepthCache;

    // Build stats
    int32 NumColumns = 0;
    int32 NumSolidCells = 0;
    int32 NumWaterCells = 0;
    int32 MinSurfaceZ = 0;
    int32 MaxSurfaceZ = 0;
    int32 ChunkCount = 0;

    // CUBE-BLOCKGRID-01: per-block patch volume (see SetPatchBlockVolume).
    FIntPoint PatchMinGlobalBlock = FIntPoint::ZeroValue;
    int32 PatchBlocksX = 0;
    int32 PatchBlocksY = 0;
    int32 PatchMaxZ = 0;
    TArray<uint8> PatchVolume;

    // Topmost non-air Z per patch column, INDEX_NONE when the column is empty.
    TArray<int16> PatchSurfaceZ;

    // Original block type of every block removed by RemoveGridBlock, for restore.
    TMap<FIntVector, uint8> RemovedPatchBlocks;
};
