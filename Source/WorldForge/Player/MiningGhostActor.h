// Source/WorldForge/Player/MiningGhostActor.h
//
// Displays a translucent grey cube at the hovered voxel position in Mine mode.
// The cube face shows a mining icon indicating that voxel is designated for removal.
//
// Lifecycle:
//   BuildModeManager::EnterMineMode()  → spawns this actor
//   BuildModeManager::UpdateFromMouse()→ calls UpdateHoverPosition() each tick
//   BuildModeManager::HandleCancel()   → destroys this actor
//
// Material setup (one-time editor work):
//   Create M_WF_MiningGhost (Translucent blend mode, Two Sided = true).
//   Parameters needed:
//     - Scalar "GhostOpacity" (default 0.35) → feeds into Opacity
//     - Vector "GhostTint"    (default 0.6,0.6,0.6,1) → feeds into Base Color
//     - Texture "MiningIcon"  (your pickaxe/crossed-pick texture) → overlaid on Base Color
//   Assign to MiningGhostMaterial in BP_MiningGhostActor Class Defaults.
//
// Cube mesh:
//   Use Engine's built-in cube (1m³): /Engine/BasicShapes/Cube.Cube
//   Assign to GhostCubeMesh in BP_MiningGhostActor Class Defaults.
//   The cube is scaled to match VoxelSizeUU (default 100 UU) automatically.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MiningGhostActor.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable)
class WORLDFORGE_API AMiningGhostActor : public AActor
{
    GENERATED_BODY()

public:
    AMiningGhostActor();

    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Runtime API — called by BuildModeManager every tick
    // -------------------------------------------------------------------------

    /**
     * Move the ghost cube to the given voxel grid coordinate.
     * The coordinate is in voxel space (integer grid); this converts to world space.
     * Automatically shows the actor if it was hidden.
     *
     * @param VoxelCoord  Hovered voxel in integer voxel coordinates.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningGhost")
    void UpdateHoverPosition(const FIntVector& VoxelCoord);

    /**
     * Hide the ghost cube (e.g. when mouse is off-terrain).
     * Does not destroy the actor.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningGhost")
    void HideGhost();

    /**
     * Show the ghost cube at its last position.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningGhost")
    void ShowGhost();

    /**
     * Change the ghost tint to indicate confirmed/selected vs just hovered.
     *   bConfirmed = false → neutral grey (hovering, not yet selected)
     *   bConfirmed = true  → yellow/amber tint (designated for mining)
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiningGhost")
    void SetConfirmedState(bool bConfirmed);

    UFUNCTION(BlueprintPure, Category = "WorldForge|MiningGhost")
    FIntVector GetCurrentVoxel() const { return CurrentVoxel; }

    /** Mesh actually used by the ghost component after Blueprint/default fallback resolution. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|MiningGhost")
    UStaticMesh* GetResolvedGhostCubeMesh() const;

    /** Material actually used by the ghost component after Blueprint/default fallback resolution. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|MiningGhost")
    UMaterialInterface* GetResolvedGhostMaterial() const;

    // -------------------------------------------------------------------------
    // Config — assign in BP_MiningGhostActor Class Defaults
    // -------------------------------------------------------------------------

    /** Static mesh for the preview cube. Assign the engine cube (BasicShapes/Cube). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldForge|MiningGhost")
    TObjectPtr<UStaticMesh> GhostCubeMesh = nullptr;

    /**
     * Translucent material with GhostOpacity, GhostTint, and MiningIcon parameters.
     * See file header for material setup instructions.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldForge|MiningGhost")
    TObjectPtr<UMaterialInterface> MiningGhostMaterial = nullptr;

    /**
     * World-unit size of one voxel. Must match VoxelWorldManager::VoxelSize (100 UU).
     * The ghost cube is scaled to this value automatically.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WorldForge|MiningGhost",
        meta = (ClampMin = "1.0"))
    float VoxelSizeUU = 100.0f;

    /** Opacity while hovering (not yet selected). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WorldForge|MiningGhost",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HoverOpacity = 0.35f;

    /** Opacity once the cell is confirmed/designated. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WorldForge|MiningGhost",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ConfirmedOpacity = 0.55f;

    /** Base colour while hovering. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WorldForge|MiningGhost")
    FLinearColor HoverTint = FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);

    /** Base colour once designated. Yellow-amber to match pickaxe icon. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WorldForge|MiningGhost")
    FLinearColor ConfirmedTint = FLinearColor(0.9f, 0.7f, 0.1f, 1.0f);

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|MiningGhost",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMeshComponent> CubeMesh = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> GhostMID = nullptr;

    FIntVector CurrentVoxel = FIntVector::ZeroValue;
    bool bGhostVisible = false;
    bool bConfirmed    = false;

    /** Convert integer voxel coordinate to world-space centre of that voxel. */
    FVector VoxelToWorldCenter(const FIntVector& VoxelCoord) const;

    void ApplyMaterialState();
};
