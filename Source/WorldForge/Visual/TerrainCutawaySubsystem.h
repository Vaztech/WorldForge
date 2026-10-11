// Source/WorldForge/Visual/TerrainCutawaySubsystem.h
//
// Dwarf Fortress-style Z-slice terrain visibility.
// Drives a world-space clip plane on the voxel terrain via a Material Parameter Collection (MPC).
//
// ─── ONE-TIME EDITOR SETUP ───────────────────────────────────────────────────
//
//  1. Create a Material Parameter Collection at:
//       /Game/WorldForge/Materials/MPC_WorldForge
//     (or change TerrainMPCPath in the CDO / a Blueprint subclass of this subsystem)
//
//  2. Inside that MPC, add a scalar parameter:
//       Name:    ViewLayerClipZ
//       Default: 9999999.0
//     (The large default value means "clip nothing" — full terrain visible.)
//
//  3. In your Voxel terrain material graph, add a clip node:
//       - Add a WorldPosition node and take its Z component.
//       - Subtract the MPC scalar: (WorldPosition.Z - ViewLayerClipZ)
//       - Feed the result into the material's "Pixel Depth Offset" to pull the
//         surface geometry, OR use an "If" node feeding into Opacity Mask
//         (requires the material to use Masked blend mode).
//
//     Simplest approach (Masked blend mode):
//       If (WorldPosition.Z > ViewLayerClipZ) → A=0, B=1 → Opacity Mask
//       This discards any pixel above the clip plane.
//
//  4. Tune ClipEdgeBiasUU (default 5.0) in-editor to eliminate z-fighting
//     at the cut edge for your specific voxel size.
//
// ─── RUNTIME USAGE ───────────────────────────────────────────────────────────
//
//  WorldForgePlayerController::ToggleMineMode calls EnterCutawayMode.
//  AWorldForgeCamera subscribes to OnViewLayerChanged to track the active layer.
//  Key bindings (. and ,) call AdjustViewLayer(-1 / +1).
//  OnBuildCancel calls ExitCutawayMode.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TerrainCutawaySubsystem.generated.h"

class UMaterialParameterCollection;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnViewLayerOffsetChanged, int32, NewLayerOffset);

UCLASS(BlueprintType)
class WORLDFORGE_API UTerrainCutawaySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // -------------------------------------------------------------------------
    // Mode entry / exit — called by WorldForgePlayerController
    // -------------------------------------------------------------------------

    /**
     * Enable the cutaway system. Pass the world-space Z of the terrain surface
     * at the current camera pivot — this becomes the reference for all layer offsets.
     * At offset 0 (entered state) the clip plane is effectively disabled;
     * the player presses view-layer-down (.) to start revealing underground layers.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway")
    void EnterCutawayMode(float SurfaceWorldZ);

    /** Enable cutaway using an absolute layer world Z instead of camera/surface-relative offsets. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway")
    void EnterCutawayAtWorldZ(float LayerWorldZ, bool bStartClipped = false);

    /** Set the absolute visible layer. Used by LayerViewSubsystem so tool/grid/cutaway never drift. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway")
    void SetAbsoluteClipLayerWorldZ(float LayerWorldZ, bool bEnableClip = true);

    /** Set the absolute visible layer from a voxel Z coordinate. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway")
    void SetAbsoluteClipLayerVoxelZ(int32 VoxelZ);

    /** Disable cutaway and restore full terrain visibility. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway")
    void ExitCutawayMode();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Cutaway")
    bool IsCutawayActive() const { return bCutawayActive; }

    // -------------------------------------------------------------------------
    // View layer — independent of tool layer in GridSelectionSubsystem
    // -------------------------------------------------------------------------

    /**
     * Shift view layer by Delta.
     *   Delta = -1 → go one layer underground (hide one more surface layer).
     *   Delta = +1 → come one layer back toward surface.
     * Clamped to [MinViewLayerOffset, 0]. No-ops when cutaway is not active.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway")
    void AdjustViewLayer(int32 Delta);

    /** Reset to surface view (offset 0) without exiting cutaway mode. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway")
    void ResetViewLayer();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Cutaway")
    int32 GetViewLayerOffset() const { return ActiveViewLayerOffset; }

    /** Returns the current clip world Z that is applied to the terrain material. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Cutaway")
    float GetCurrentClipWorldZ() const { return ComputeClipWorldZ(); }

    // -------------------------------------------------------------------------
    // Delegate — AWorldForgeCamera and UI subscribe here
    // -------------------------------------------------------------------------

    /** Broadcast whenever the view layer offset changes, including on Enter/Exit. */
    UPROPERTY(BlueprintAssignable, Category = "WorldForge|Cutaway")
    FOnViewLayerOffsetChanged OnViewLayerChanged;

    // -------------------------------------------------------------------------
    // Config — adjust in a Blueprint subclass CDO or via the Details panel
    // -------------------------------------------------------------------------

    /** Full content-browser path to MPC_WorldForge. Override per project. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cutaway|Config")
    FSoftObjectPath TerrainMPCPath =
        FSoftObjectPath(TEXT("/Game/WorldForge/Materials/MPC_WorldForge.MPC_WorldForge"));

    /** Scalar parameter name inside the MPC. Must match what the material reads. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cutaway|Config")
    FName ClipZParameterName = FName(TEXT("ViewLayerClipZ"));

    /**
     * World-units per voxel layer.
     * Uses VoxelWorldManager::GetRenderVoxelSize when available; this fallback must
     * match the CubePrototype/render voxel size.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cutaway|Config",
        meta = (ClampMin = "1.0"))
    float VoxelLayerHeightUU = 200.0f;

    /**
     * Small upward bias applied to the clip edge to avoid z-fighting where smooth
     * Transvoxel geometry crosses the theoretical voxel boundary. Tune in PIE.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cutaway|Config")
    float ClipEdgeBiasUU = 5.0f;

    /**
     * Maximum underground depth in view layers (always <= 0).
     * Prevents the player from looking below bedrock.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Cutaway|Config",
        meta = (ClampMax = "0"))
    int32 MinViewLayerOffset = -64;

private:
    bool  bCutawayActive       = false;
    int32 ActiveViewLayerOffset = 0;
    float SurfaceWorldZ        = 0.0f;

    /** When true, ComputeClipWorldZ uses AbsoluteClipWorldZ instead of SurfaceWorldZ + offset math. */
    bool bUseAbsoluteClipLayer = false;
    float AbsoluteClipWorldZ = 0.0f;

    // Max offset is always 0 — you cannot look above the surface in mine view.
    static constexpr int32 MaxViewLayerOffset = 0;

    // Value written to the MPC to disable clipping entirely.
    static constexpr float DisabledClipZ = 9999999.0f;

    // Loaded once in Initialize; used by ApplyClipPlane every frame.
    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> CachedMPC = nullptr;

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    float GetVoxelLayerHeightUU() const;

    /**
     * Computes the clip Z value for the current layer offset.
     *
     * Math:
     *   offset  0 → DisabledClipZ            (no clipping, full surface visible)
     *   offset -1 → SurfaceWorldZ + Bias     (surface layer hidden)
     *   offset -2 → SurfaceWorldZ - VoxelH + Bias  (two layers hidden)
     *   offset -N → SurfaceWorldZ + (LayerOffset+1)*VoxelH + Bias
     *
     * Pixels with WorldPosition.Z > ClipZ are discarded by the material.
     */
    float ComputeClipWorldZ() const;

    /** Write the current clip Z to the MPC. */
    void ApplyClipPlane() const;

    /** Attempt to load the MPC from disk and cache it. Returns true on success. */
    bool TryLoadMPC();
};
