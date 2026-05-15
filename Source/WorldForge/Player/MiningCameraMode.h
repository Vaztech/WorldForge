// Source/WorldForge/Player/MiningCameraMode.h
//
// Camera behavior overrides while Mine mode is active.
//
// Responsibilities:
//   - Save AWorldForgeCamera's pitch/zoom constraints before mine mode.
//   - Apply looser mining-specific constraints (steeper pitch, tighter zoom).
//   - Compute the recommended camera target Z as the view layer changes,
//     so the camera follows the active underground layer downward.
//   - Restore all saved constraints cleanly on exit.
//
// Non-responsibilities:
//   - Does not manage the terrain clip plane (TerrainCutawaySubsystem does that).
//   - Does not own input bindings or Enhanced Input.
//   - Does not directly call SetActorLocation — it only computes a target Z
//     that AWorldForgeCamera applies to its own TargetLocation.
//
// Owned by AWorldForgeCamera as a UPROPERTY UObject (prevents GC).
// Created in AWorldForgeCamera::BeginPlay via NewObject.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MiningCameraMode.generated.h"

class AWorldForgeCamera;

UCLASS(BlueprintType)
class WORLDFORGE_API UMiningCameraMode : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Enter mining camera mode.
     * Saves current camera constraints and applies mining overrides.
     * Safe to call when already active (updates SurfaceWorldZ only).
     */
    void Enter(AWorldForgeCamera* Camera, float InSurfaceWorldZ);

    /**
     * Exit mining camera mode.
     * Restores original camera constraints saved during Enter.
     * Safe to call when not active (no-op).
     */
    void Exit(AWorldForgeCamera* Camera);

    /**
     * Compute the recommended camera pivot Z for the given view layer offset.
     * AWorldForgeCamera calls this inside HandleViewLayerChanged and applies
     * the result to its own TargetLocation.Z.
     *
     *   offset  0 → SurfaceWorldZ            (surface layer)
     *   offset -1 → SurfaceWorldZ - VoxelH   (one layer underground)
     *   offset -N → SurfaceWorldZ - N * VoxelH
     */
    float ComputeTargetWorldZ(int32 ViewLayerOffset) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|MiningCamera")
    bool IsActive() const { return bActive; }

    // -------------------------------------------------------------------------
    // Config — adjust in Blueprint CDO or Details panel
    // -------------------------------------------------------------------------

    /**
     * World-units per layer. Must match VoxelPlugin's voxel size and
     * TerrainCutawaySubsystem::VoxelLayerHeightUU (both default 100 UU).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningCamera")
    float LayerZOffsetUU = 100.0f;

    /**
     * ZoomMax override in mining mode.
     * Tighter than the surface RTS zoom (default 20000) to keep the active
     * underground layer readable without the camera going too far away.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningCamera",
        meta = (ClampMin = "200.0"))
    float MiningZoomMax = 1200.0f;

    /** Target zoom applied on entry so mine mode never starts from a far surface zoom. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningCamera",
        meta = (ClampMin = "200.0"))
    float MiningDefaultZoom = 1000.0f;

    /** Target pitch applied on entry for a readable diorama angle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningCamera",
        meta = (ClampMin = "-89.0", ClampMax = "0.0"))
    float MiningDefaultPitch = -58.0f;

    /**
     * MinPitch override in mining mode.
     * Allows near-vertical bird's-eye for inspecting deep tunnel shapes.
     * More negative = more vertical.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningCamera",
        meta = (ClampMin = "-89.0", ClampMax = "0.0"))
    float MiningMinPitch = -65.0f;

    /**
     * MaxPitch override in mining mode.
     * Less negative than MiningMinPitch. Keeps some angle so the player
     * can see the layer they are on, not just terrain below them.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiningCamera",
        meta = (ClampMin = "-89.0", ClampMax = "0.0"))
    float MiningMaxPitch = -45.0f;

private:
    bool  bActive       = false;
    float SurfaceWorldZ = 0.0f;

    // Saved original camera values to restore on Exit.
    float SavedMinPitch = -85.0f;
    float SavedMaxPitch = -15.0f;
    float SavedZoomMax  = 20000.0f;
};
