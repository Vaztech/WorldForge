// WorldForgeCubeMeshBuilder.h — CUBE-TERRAIN-03D
// Builds greedy-meshed chunk mesh data for the CubePrototype render actor.
// 03D adds debug flags to isolate giant side-face greedy sheets and patch-border walls.

#pragma once

#include "CoreMinimal.h"
#include "CubeTerrainTypes.h"
#include "ProceduralMeshComponent.h"

class UCubeTerrainRuntime;

struct FWorldForgeCubePatchMeshSettings
{
    FIntPoint TerrainGridSize = FIntPoint::ZeroValue;
    FIntPoint PatchMinTile = FIntPoint::ZeroValue;
    FIntPoint PatchTileCount = FIntPoint::ZeroValue;
    FVector TerrainOrigin = FVector::ZeroVector;
    int32 BlocksPerWorldTile = 8;
    int32 ChunkSizeInBlocks = 32;
    float TileWorldSizeUU = 3200.0f;
    float BlockHeightUU = 100.0f;
    bool bFlatBlockSurface = true;

    // CUBE-TERRAIN-03D: Debug flags for visual isolation of giant side-face sheets.
    // true = vertical side faces (FaceDir 0–3, ±X/±Y) are emitted per-block, no greedy merging.
    // false = current optimized greedy behavior.
    bool bDebugDisableGreedySideFaces = true;

    // CUBE-TERRAIN-03D: Suppress the artificial outer wall faces at the 32-tile patch boundary.
    // Outside-patch neighbors return Air, causing every border block to emit a visible side face.
    // true = those border-edge vertical faces are suppressed; false = current behavior.
    bool bDebugSuppressOuterPatchBorderFaces = true;

    // CUBE-TERRAIN-04M: Shoreline edge skirt — presentation-only mesh shaping.
    // Lowers the outermost N tile rings of the patch toward SeaLevelZ so the patch
    // boundary blends into the surrounding (non-rendered) ocean instead of a hard
    // rectangular cliff. Does NOT affect runtime SurfaceZ data, block type
    // classification, or fluid tiles.
    //   SeaLevelZ       = target floor height of the outermost tile ring.
    //   EdgeSkirtTiles  = number of tile rings to ramp from SeaLevelZ up to interior height.
    //                     Set to 0 to disable.
    int32 SeaLevelZ = 8;
    int32 EdgeSkirtTiles = 2;
};

struct FWorldForgeCubeMeshSectionData
{
    EWorldForgeCubeBlockType BlockType = EWorldForgeCubeBlockType::Air;
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    bool HasGeometry() const
    {
        return Triangles.Num() > 0;
    }

    int32 GetTriangleCount() const
    {
        return Triangles.Num() / 3;
    }
};

struct FWorldForgeCubeChunkMeshData
{
    FIntPoint ChunkCoord = FIntPoint::ZeroValue;
    int32 BlocksChecked = 0;
    int32 RawFaces = 0;     // individual block-face quads before merging
    int32 Faces = 0;        // greedy quads actually emitted (== GreedyQuads)
    int32 Sections = 0;
    int32 Triangles = 0;
    TArray<FWorldForgeCubeMeshSectionData> SectionData;

    bool HasGeometry() const
    {
        return Sections > 0;
    }
};

struct FWorldForgeCubePatchMeshData
{
    int32 LocalGridWidth = 0;
    int32 LocalGridHeight = 0;
    int32 ChunkSizeInBlocks = 0;
    int32 BlocksChecked = 0;
    int32 RawFaces = 0;     // patch-total individual block-face count
    int32 Faces = 0;        // patch-total greedy quads emitted
    int32 Triangles = 0;
    int32 Sections = 0;
    TArray<FWorldForgeCubeChunkMeshData> Chunks;

    // 03A terrain shaping stats
    int32 MinZ = 0;
    int32 MaxZ = 0;
    float AvgZ = 0.0f;
    float LocalDeltaAvg = 0.0f;
    int32 LocalDeltaMax = 0;
    int32 MixedCells = 0;
    int32 SnowBlend = 0;
    int32 SandBlend = 0;
    int32 RockBlend = 0;
};

class FWorldForgeCubeMeshBuilder
{
public:
    // Builds the patch block volume, stores it on the runtime
    // (UCubeTerrainRuntime::SetPatchBlockVolume), then meshes every chunk from it.
    static bool BuildPatchMesh(
        UCubeTerrainRuntime* Runtime,
        const FWorldForgeCubePatchMeshSettings& Settings,
        FWorldForgeCubePatchMeshData& OutPatchData);

    // CUBE-BLOCKGRID-01: Re-meshes one chunk from the runtime's current patch volume.
    // Used after mining edits the volume. Settings must be the ones used by BuildPatchMesh.
    // OutChunkData may legitimately have no geometry if the chunk was dug out.
    static bool BuildChunkMesh(
        const UCubeTerrainRuntime* Runtime,
        const FWorldForgeCubePatchMeshSettings& Settings,
        const FIntPoint& ChunkCoord,
        FWorldForgeCubeChunkMeshData& OutChunkData);
};
