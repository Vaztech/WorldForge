// WorldForgeCubeTerrainActor.h — CUBE-TERRAIN-03D
// CubePrototype render actor with chunk-mesh prototype path plus HISM fallback.
// 03D adds visual debug flags to suppress giant greedy side-face sheets and patch-border walls.
// Smooth terrain remains owned by UVoxelWorldManager and is not replaced.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CubeTerrainTypes.h"
#include "WorldForgeCubeMeshBuilder.h"
#include "WorldForgeCubeTerrainActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UCubeTerrainRuntime;
class UVoxelWorldManager;

UCLASS()
class WORLDFORGE_API AWFCubeTerrainActor : public AActor
{
    GENERATED_BODY()

public:
    AWFCubeTerrainActor();

    // Build (or rebuild) the patch. Call after CubeTerrainRuntime::IsValid() == true.
    // Patch center priority: VoxelMgr->GetConfirmedEmbarkTile() → EmbarkationManager → grid center.
    void BuildPatch(UCubeTerrainRuntime* Runtime, UVoxelWorldManager* VoxelMgr);

    // Clear all HISM instances without destroying the actor.
    void ClearPatch();

    // CUBE-BLOCKGRID-01: Re-mesh the chunk containing GridCell (and any neighbor
    // chunk whose boundary faces it touches) from the runtime's patch volume.
    // Call after UCubeTerrainRuntime::RemoveGridBlock / RestoreGridBlock.
    // Returns false when the chunk-mesh path is not active (e.g. HISM fallback).
    bool RebuildChunksForGridCell(UCubeTerrainRuntime* Runtime, const FIntVector& GridCell);

    // Patch half-radius in tiles. Full patch = PatchHalfRadius*2 x PatchHalfRadius*2 world tiles.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain")
    int32 PatchHalfRadius = 16;

    // CUBE-TERRAIN-02A: Local block resolution per world tile.
    // Each macro tile becomes BlocksPerWorldTile x BlocksPerWorldTile local block columns.
    // Default 8 → 32 tiles × 8 = 256 block columns per axis.
    // Runtime's BlocksPerWorldTile takes priority; this is the fallback if Runtime is unavailable.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain")
    int32 BlocksPerWorldTile = 8;

    // CUBE-TERRAIN-02A: World units per local block.
    // 100 UU = 1 meter = true 1x1x1 Minecraft-style cube.
    // CUBE-TERRAIN-02B: No longer drives placement. Effective block size is now derived from
    //   ChunkWorldSize / BlocksPerWorldTile (400 UU for defaults). Retained for BP compatibility.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain")
    float BlockSizeUU = 100.0f;

    // Solid blocks below surface to render per column (surface block + N below it).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain")
    int32 VisibleDepthBlocks = 4;

    // CUBE-TERRAIN-02D: Primary render path for CubePrototype.
    // When true, the actor builds chunked ProceduralMesh sections with visible faces only.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain|Debug")
    bool bUseChunkMeshPrototype = true;

    // CUBE-TERRAIN-02D: Local block chunk size in X/Y for the mesh prototype.
    // Recommended values: 16 or 32. Default 32 => 256x256 patch becomes 8x8 chunk grid.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain|Debug", meta = (ClampMin = "16"))
    int32 ChunkSizeInBlocks = 32;

    // CUBE-TERRAIN-02D: Keep the old HISM prototype available if chunk mesh creation fails.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain|Debug")
    bool bAllowHISMFallback = true;

    // CUBE-TERRAIN-01F: Debug isolation flag.
    // When true, VoxelWorldManager hides VoxelWorld_1 after BuildPatch.
    // Restored to visible on teardown. Do NOT set permanently.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain|Debug")
    bool bCubePrototypeHideSmoothTerrainForTest = true;

    // CUBE-TERRAIN-01E: Reserved debug scale option (unused in 02A).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeTerrain|Debug")
    bool bUseSmallVoxelCubesForDebug = false;

    // CUBE-TERRAIN-03C: Flat block surface toggle.
    // true  = all sub-blocks in a tile use the parent tile's SurfaceZ (Minecraft-style flat tops).
    // false = BiLerp height blending is applied across tile boundaries.
    // CUBE-TERRAIN-04J: Default false enables intra-tile height blending so the patch
    //   does not render as a single giant flat plate of greedy-merged top quads.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cube Terrain|Debug")
    bool bFlatBlockSurface = false;

    // CUBE-TERRAIN-03D: Prevents vertical side faces (±X, ±Y) from being greedy-merged into
    // giant single quads that make cliff walls look like smooth SDF sheets.
    // true  = each visible side face is emitted individually (blocky look, more draw calls).
    // false = original optimized greedy behavior (may produce large merged sheets).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cube Terrain|Debug")
    bool bDebugDisableGreedySideFaces = true;

    // CUBE-TERRAIN-03D: Suppresses the artificial outer wall faces at the patch boundary.
    // Outside-patch neighbors return Air, so every border block emits a visible outward side face.
    // These merge into a single enormous wall per edge when greedy is active.
    // true  = those outer border vertical faces are suppressed (patch edges are open).
    // false = current behavior (outer walls visible).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cube Terrain|Debug")
    bool bDebugSuppressOuterPatchBorderFaces = true;

    // CUBE-TERRAIN-04M: Number of patch-edge tile rings ramped down toward sea level.
    // The outermost ring approaches SeaLevelZ; inner tiles retain their 04L blended height.
    // Set to 0 to disable the shoreline skirt entirely.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cube Terrain|Debug", meta = (ClampMin = "0"))
    int32 EdgeSkirtTiles = 2;

    // CUBE-TERRAIN-04N: Per-block-type material override map.
    // When a block type has an entry, that material is used in place of the debug base MID
    // for both the chunk-mesh sections and the HISM fallback components.
    // Unmapped types fall back to the legacy debug color path.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cube Terrain|Materials")
    TMap<EWorldForgeCubeBlockType, TObjectPtr<UMaterialInterface>> BlockTypeMaterials;

private:
    void ClearHISMInstances();
    void DestroyChunkMeshComponents();
    void BuildPatchWithHISM(UCubeTerrainRuntime* Runtime, const FWorldForgeCubePatchMeshSettings& MeshSettings);
    UHierarchicalInstancedStaticMeshComponent* GetHISMForType(EWorldForgeCubeBlockType BlockType) const;
    UMaterialInstanceDynamic* CreateDebugMaterialInstance(
        UMaterialInterface* DebugBaseMat,
        EWorldForgeCubeBlockType BlockType) const;
    static FLinearColor GetDebugColorForBlockType(EWorldForgeCubeBlockType BlockType);

    // CUBE-TERRAIN-04N: Returns the mapped material for BlockType, or nullptr if unmapped.
    UMaterialInterface* GetMaterialForBlockType(EWorldForgeCubeBlockType BlockType) const;

    // CUBE-BLOCKGRID-01: chunk mesh component helpers shared by BuildPatch and chunk rebuilds.
    UProceduralMeshComponent* CreateChunkMeshComponent(const FIntPoint& ChunkCoord);
    void ApplyChunkMeshData(
        UProceduralMeshComponent* ChunkMeshComponent,
        const FWorldForgeCubeChunkMeshData& ChunkData,
        TSet<EWorldForgeCubeBlockType>* LoggedBlockMatTypes,
        int32& OutAssignedCount,
        int32& OutDebugFallbackCount);

    UPROPERTY(Transient)
    TArray<TObjectPtr<UProceduralMeshComponent>> ChunkMeshComponents;

    // CUBE-BLOCKGRID-01: index into ChunkMeshComponents per chunk coord, and the
    // settings the patch was built with, so single chunks can be re-meshed.
    TMap<FIntPoint, int32> ChunkComponentIndexByCoord;
    FWorldForgeCubePatchMeshSettings LastMeshSettings;
    bool bHasLastMeshSettings = false;

    // One HISM per non-Air block type. Created in constructor; mesh/instances set in BuildPatch.
    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Grass    = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Dirt     = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Sand     = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Rock     = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Snow     = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Water    = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_DeepRock = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Lava     = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "CubeTerrain")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM_Basalt   = nullptr;
};
