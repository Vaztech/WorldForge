// Source/WorldForge/Grid/MineLayerCoordinator.h
//
// Single authority for mine-mode layer changes.
//
// All mine layer movements — keyboard, UI button, reset — go through this
// subsystem. It guarantees that LayerViewSubsystem, TerrainCutawaySubsystem,
// and GridSelectionSubsystem are updated atomically so they can never drift.
//
// CALL PATTERN (WorldForgePlayerController):
//
//   EnterMineMode:
//     MineLayerCoord->InitializeForMineMode(surfaceWorldZ);
//
//   IA_ToolLayerDown / IA_ViewLayerDown:
//     MineLayerCoord->MoveLayer(-1);
//
//   IA_ToolLayerUp / IA_ViewLayerUp:
//     MineLayerCoord->MoveLayer(+1);
//
//   IA_ToolLayerReset / IA_ViewLayerReset:
//     MineLayerCoord->ResetToSurface();
//
// IMPORTANT: Do NOT call LayerViewSubsystem::MoveLayerBy or
// TerrainCutawaySubsystem::SetAbsoluteClipLayerWorldZ directly from the
// PlayerController for mine mode. Use this coordinator instead.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MineLayerCoordinator.generated.h"

class ULayerViewSubsystem;
class UTerrainCutawaySubsystem;
class UGridSelectionSubsystem;

UCLASS(BlueprintType)
class WORLDFORGE_API UMineLayerCoordinator : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // -------------------------------------------------------------------------
    // Mine mode lifecycle — called by WorldForgePlayerController
    // -------------------------------------------------------------------------

    /**
     * Call when entering mine mode to initialize the cutaway system.
     * SurfaceWorldZ should be the LayerView's current active layer world Z,
     * or the camera Z as a fallback.
     * Starts unclipped — the player must scroll down to reveal underground layers.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MineLayerCoordinator")
    void InitializeForMineMode(float SurfaceWorldZ);

    // -------------------------------------------------------------------------
    // Layer movement — the single entry point for all mine layer changes
    // -------------------------------------------------------------------------

    /**
     * Move the active mine layer by Delta voxels.
     *   Delta = -1  →  go one layer deeper (underground)
     *   Delta = +1  →  go one layer back toward surface
     *
     * Internally:
     *   1. Calls GridSelectionSubsystem::AdjustActiveToolLayer (moves LayerView).
     *   2. Syncs TerrainCutawaySubsystem to new LayerView state.
     *   3. Triggers grid rebuild at the new layer.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MineLayerCoordinator")
    void MoveLayer(int32 Delta);

    /**
     * Reset the active mine layer to the surface (relative offset 0).
     * Disables terrain clipping and rebuilds the surface grid.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MineLayerCoordinator")
    void ResetToSurface();

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /** Absolute voxel Z of the currently active mine layer. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|MineLayerCoordinator")
    int32 GetActiveVoxelLayerZ() const;

    /** True when the active mine layer is below the surface (clip plane enabled). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|MineLayerCoordinator")
    bool IsUnderground() const;

    /** Relative layer offset from the surface (0 = surface, -1 = first underground, ...). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|MineLayerCoordinator")
    int32 GetRelativeLayerOffset() const;

private:
    /**
     * Sync TerrainCutawaySubsystem clip plane to the current LayerView state.
     * Called after every layer move. Uses absolute voxel Z so camera position
     * and stale view offsets can never drift the clip plane.
     */
    void SyncCutawayToLayerView() const;

    ULayerViewSubsystem*      GetLayerView() const;
    UTerrainCutawaySubsystem* GetCutaway()   const;
    UGridSelectionSubsystem*  GetGrid()      const;
};
