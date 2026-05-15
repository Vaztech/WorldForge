// Source/WorldForge/Mining/MiningLayerRenderer.h
//
// Dedicated mining-mode layer renderer.
//
// Renders a Going Medieval / Dwarf Fortress-style flat workspace cap at the
// active Z layer, plus ghost tiles for selected / confirmed mining designations.
//
// ─── CAP GEOMETRY ────────────────────────────────────────────────────────────
//
// TerrainCutawaySubsystem writes ViewLayerClipZ = ActiveLayerWorldZ + 5 into
// MPC_WorldForge whenever the player is underground. Any material reading that
// MPC discards pixels above ClipZ. Full-voxel cubes would have 95% of their
// extent above the clip plane and get culled by the material — producing the
// "black void + grid + floating ghosts" symptom underground.
//
// Cap tiles are positioned so the entire tile sits below the clip plane:
//     top surface  = ActiveVoxelZ * CellSizeUU + TopZBias    (TopZBias < 5UU)
//     bottom       = top - CapThicknessUU
// This makes the cap clip-immune underground regardless of material.
//
// ─── SURFACE SUPPRESSION ─────────────────────────────────────────────────────
//
// At surface offset 0 the cutaway is inactive and the surface terrain is the
// workspace. Drawing cap tiles there would bury them inside solid voxel terrain
// (because no clipping is removing the rock above). The renderer therefore
// auto-suppresses the cap when LayerViewSubsystem reports offset >= 0.
//
// ─── DEBUG ESCAPE HATCH ──────────────────────────────────────────────────────
//
// If the cap stays invisible even underground after a proper rebuild, flip
// bUseDebugMaterial. The renderer will override every HISM (terrain + ghosts)
// with DebugMaterial (defaults to /Engine/EngineMaterials/WorldGridMaterial),
// which ignores all MPC parameters and is guaranteed to render. If the cap
// shows up with the debug material but not with the terrain materials, the
// problem is in the terrain materials, not the cap geometry.
//
// This renderer is intentionally separate from TileGridOverlayActor.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"
#include "MiningLayerRenderer.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UBoxComponent; // Invisible camera-blocking walls at diorama edge
class UStaticMesh;
class UMaterialInterface;

UCLASS()
class WORLDFORGE_API AMiningLayerRenderer : public AActor
{
    GENERATED_BODY()

public:
    AMiningLayerRenderer();

    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningLayer")
    void SetLayerState(
        const FIntVector& CenterVoxel,
        int32 ActiveVoxelZ,
        const TArray<FIntVector>& SelectedMineCells,
        const TArray<FIntVector>& ConfirmedMineCells);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningLayer")
    void ClearLayer();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningLayer")
    void SetRendererVisible(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningLayer")
    void SetCellSizeUU(float InCellSizeUU);

    /**
     * Force the next SetLayerState call to do a full rebuild and dump diagnostics,
     * even if the deduplication check would otherwise skip it. Call from PIE if
     * you change a property at runtime and want to see the effect immediately.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningLayer|Debug")
    void ForceNextRebuild();

public:
    // -------------------------------------------------------------------------
    // General config
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer")
    int32 VisibleRadius = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer")
    int32 LayersAbove = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer")
    int32 LayersBelow = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer", meta = (ClampMin = "1.0"))
    float CellSizeUU = 100.0f;

    // -------------------------------------------------------------------------
    // Cap geometry
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap", meta = (ClampMin = "1.0"))
    float CapThicknessUU = 90.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap", meta = (ClampMin = "0.0", ClampMax = "4.5"))
    float TerrainCapTopBiasUU = -2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap", meta = (ClampMin = "0.0", ClampMax = "4.5"))
    float SelectedGhostTopBiasUU = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap", meta = (ClampMin = "0.0", ClampMax = "4.5"))
    float ConfirmedGhostTopBiasUU = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float TerrainBlockScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float GhostBlockScale = 0.92f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap")
    bool bUseTerrainLayerColors = true;

    /**
     * When true (default), SetLayerState is a no-op when LayerViewSubsystem reports
     * relative offset >= 0 (player is at or above surface). This prevents cap tiles
     * from being buried inside solid voxel terrain at the surface, where the cutaway
     * isn't clipping anything.
     *
     * Set to false ONLY if you want the cap to draw at surface for debugging.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Cap")
    bool bSuppressAtSurface = true;

    /** Large dark backing slab drawn behind underground slices so sky/water/actors do not read as the cave background. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Presentation", meta = (ClampMin = "16", ClampMax = "256"))
    int32 UndergroundBackdropRadius = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Presentation")
    FLinearColor UndergroundBackdropColor = FLinearColor(0.015f, 0.025f, 0.045f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Presentation", meta = (ClampMin = "1.0", ClampMax = "200.0"))
    float UndergroundBackdropThicknessUU = 100.0f;

    /** Height of the dark underground wall box used to hide sky/water/above-ground actors while underground. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Presentation", meta = (ClampMin = "500.0", ClampMax = "20000.0"))
    float UndergroundBackdropWallHeightUU = 700.0f;

    /**
     * Number of voxel layers the terrain-colored diorama walls stack upward
     * from ActiveVoxelZ. Each layer queries GetTerrainMaterialLayerAtVoxel so
     * the wall shows real rock/dirt/sand composition at the excavation boundary.
     *
     * 4–6 is enough to sell the illusion without the wall consuming the camera.
     * The wall base aligns with the cap floor (ActiveVoxelZ), so layer 0 is the
     * floor-level ring and the top layer peeks above the clip plane into "ground."
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Presentation", meta = (ClampMin = "1", ClampMax = "32"))
    int32 DioramaWallHeightLayers = 16;

    /**
     * Extra voxels of dark-earth floor tile ring placed OUTSIDE the diorama walls.
     * Covers the smooth voxel terrain's floor/edge mesh faces that bleed through the
     * clip plane as grass-colored geometry. Default 15 covers the full camera view at
     * MiningZoomMax=1200. Set 0 to disable (not recommended underground).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Presentation", meta = (ClampMin = "0", ClampMax = "40"))
    int32 OuterCapExtendRadius = 15;

    // -------------------------------------------------------------------------
    // Debug / diagnostics — toggle these in the actor's Details panel in PIE
    // -------------------------------------------------------------------------

    /**
     * Replace every terrain-layer and ghost material with DebugMaterial. Use to
     * verify the cap geometry is in the right place independently of whether the
     * terrain materials are rendering correctly. If the cap shows up with this
     * on but disappears with it off, the bug is in your terrain materials.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Debug")
    bool bUseDebugMaterial = false;

    /** Defaults to /Engine/EngineMaterials/WorldGridMaterial — ignores all MPC params. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Debug")
    TObjectPtr<UMaterialInterface> DebugMaterial = nullptr;

    /**
     * Dump actor/HISM positions, visibility, and a sample instance transform on
     * the first SetLayerState call after each ClearLayer. Auto-disables itself
     * after one dump per session to avoid log spam.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Debug")
    bool bDumpDiagnosticsOnce = true;

    // -------------------------------------------------------------------------
    // Assets
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Assets")
    TObjectPtr<UStaticMesh> CubeMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Materials")
    FSoftObjectPath TerrainMaterialCollectionPath = FSoftObjectPath(TEXT("/Game/WorldForge/Materials/MC_WorldForge_Terrain.MC_WorldForge_Terrain"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Materials")
    TArray<TObjectPtr<UMaterialInterface>> TerrainLayerMaterials;

    /**
     * Dedicated unlit/emissive cap tile material used by the mining layer HISMs.
     *
     * Keep this separate from MC_WorldForge_Terrain. The material collection is
     * still valid for the real voxel terrain, but those lit/PBR terrain materials
     * are wrong for underground cap tiles because they render black without light.
     *
     * Expected material graph:
     *   PerInstanceCustomData[0] = R
     *   PerInstanceCustomData[1] = G
     *   PerInstanceCustomData[2] = B
     *   PerInstanceCustomData[3] = Opacity
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Materials")
    TObjectPtr<UMaterialInterface> CapTileMaterial = nullptr;

    /** Dark unlit material used only for the 20x20 underground diorama walls. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Materials")
    TObjectPtr<UMaterialInterface> DioramaWallMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Materials")
    TObjectPtr<UMaterialInterface> FallbackTerrainMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningLayer|Materials")
    TObjectPtr<UMaterialInterface> MiningGhostMaterial = nullptr;

private:
    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot = nullptr;

    UPROPERTY()
    TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TerrainLayerHISMs;

    UPROPERTY()
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SelectedGhostHISM = nullptr;

    UPROPERTY()
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ConfirmedGhostHISM = nullptr;

    UPROPERTY()
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> UndergroundBackdropHISM = nullptr;

    // ── Invisible camera-blocking walls ──────────────────────────────────────
    // Four thin UBoxComponent planes at the N/S/E/W diorama edges.
    // They respond ONLY to ECC_Camera so the spring arm (bDoCollisionTest=true
    // by default) shortens before the camera eye crosses the wall boundary.
    // No mesh, no shadow, no physics interaction with colonists or projectiles.
    UPROPERTY() TObjectPtr<UBoxComponent> CameraWallNorth;
    UPROPERTY() TObjectPtr<UBoxComponent> CameraWallSouth;
    UPROPERTY() TObjectPtr<UBoxComponent> CameraWallEast;
    UPROPERTY() TObjectPtr<UBoxComponent> CameraWallWest;

    // Dedupe state — suppresses redundant rebuilds when the caller polls every tick.
    FIntVector LastCenterVoxel              = FIntVector(MIN_int32, MIN_int32, MIN_int32);
    int32      LastActiveVoxelZ             = MIN_int32;
    int32      LastSelectedCount            = MIN_int32;
    int32      LastConfirmedCount           = MIN_int32;
    bool       bLastWasSuppressed           = false;
    bool       bForceNextRebuild            = false;
    bool       bDiagnosticsDumpedThisSession = false;

private:
    // Asset loading / material discovery
    void EnsureAssets();
    void TryAutoLoadTerrainLayerMaterialsFromCollection();
    UHierarchicalInstancedStaticMeshComponent* GetOrCreateTerrainLayerHISM(int32 MaterialLayer);
    UMaterialInterface* ResolveTerrainMaterialForLayer(int32 MaterialLayer) const;
    UMaterialInterface* ResolveGhostMaterial() const;

    // HISM management
    void ClearTerrainLayerHISMs();

    /** Lazily create the 4 invisible camera-blocking box components (safe to call multiple times). */
    void EnsureCameraWalls();

    /**
     * Position the 4 camera walls at the diorama boundary and activate or
     * deactivate their collision.  Call with bActive=true when underground,
     * false when suppressed (surface) or when mine mode exits entirely.
     */
    void UpdateCameraWalls(const FIntVector& CenterVoxel, int32 ActiveVoxelZ, bool bActive);
    void ConfigureHISM(UHierarchicalInstancedStaticMeshComponent* HISM) const;
    void SetInstanceColor(UHierarchicalInstancedStaticMeshComponent* HISM, int32 InstanceIndex, const FLinearColor& Color) const;

    FTransform MakeCapTileTransform(const FIntVector& VoxelCoord, float TopZBiasUU, float XYScaleMultiplier) const;
    FTransform MakeUndergroundBackdropTransform(const FIntVector& CenterVoxel, int32 ActiveVoxelZ) const;
    void AddUndergroundBackdropInstances(const FIntVector& CenterVoxel, int32 ActiveVoxelZ);

    // Color helpers
    FLinearColor GetTerrainColorForLayer(int32 MaterialLayer) const;
    FLinearColor GetSelectedGhostColor() const;
    FLinearColor GetConfirmedGhostColor() const;

    // Diagnostics
    void DumpDiagnostics(const FIntVector& CenterVoxel, int32 ActiveVoxelZ, int32 TerrainInstancesAdded) const;
};
