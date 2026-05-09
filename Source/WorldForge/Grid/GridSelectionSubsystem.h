// Source/WorldForge/Grid/GridSelectionSubsystem.h
//
// Dedicated shared grid-selection system for WorldForge tools.
//
// Responsibilities:
// - Raycast under the mouse BEFORE drawing overlay.
// - Convert the raycast hit to true voxel/grid coordinates.
// - Maintain hover, drag preview, and selected-cell state.
// - Feed TileGridOverlayActor with visual-only data.
//
// Non-responsibilities:
// - Does not create farm zones.
// - Does not create build tasks.
// - Does not create mining designations.
// - Does not modify terrain.
//
// BuildModeManager should call this subsystem, then convert selected cells
// into the active tool's gameplay data on Confirm.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Player/BuildModeTypes.h"
#include "GridSelectionTypes.h"
#include "Grid/GridDesignationTypes.h"
#include "GridSelectionSubsystem.generated.h"

class APlayerController;
class ATileGridOverlayActor;
class UTexture2D;

UCLASS(BlueprintType)
class WORLDFORGE_API UGridSelectionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ---------------------------------------------------------------------
    // Tool/session lifecycle
    // ---------------------------------------------------------------------

    /** Start a shared grid-selection session for a tool. Spawns/shows overlay. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void BeginSelectionTool(EBuildToolMode ToolMode, EGridSelectionShape Shape = EGridSelectionShape::Rectangle);

    /** End the active selection session. Hides overlay and clears preview. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void EndSelectionTool(bool bClearSelectedCells = true);

    /** Update hover from mouse raycast. Call from PlayerController/BuildModeManager tick while a tool is active. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    bool TickSelectionFromMouse(APlayerController* PC);

    // ---------------------------------------------------------------------
    // Mouse events
    // ---------------------------------------------------------------------

    /** Mouse/button down: captures drag start from the current raycast-backed hover. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    bool BeginDragSelection();

    /** Mouse/button up: commits preview into selected cells once. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    bool EndDragSelection(bool bCommitPreview = true);

    /** Single-click toggle using current hovered cell. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    bool ToggleHoveredSelection();

    // ---------------------------------------------------------------------
    // State / queries
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection")
    bool IsSelectionActive() const { return bSelectionActive; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection")
    bool IsDraggingSelection() const { return bDraggingSelection; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection")
    bool HasValidHover() const { return bHasValidHover; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection")
    FIntVector GetHoveredVoxel() const { return HoveredVoxel; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection")
    float GetHoveredTerrainZ() const { return HoveredTerrainZ; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection")
    int32 GetSelectedCount() const { return SelectedCells.Num(); }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection")
    int32 GetPreviewCount() const { return DragPreviewCells.Num(); }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void GetSelectedCells(TArray<FGridSelectionCell>& OutCells) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void ClearSelectedCells();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void ClearPreviewCells();

    /** Persist currently selected cells into the overlay's confirmed-designation layer. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void CommitSelectedCellsToConfirmedOverlay(EGridDesignationType DesignationType);

private:
    UPROPERTY(Transient)
    TObjectPtr<ATileGridOverlayActor> GridOverlay = nullptr;

    bool bSelectionActive = false;
    bool bDraggingSelection = false;
    bool bHasValidHover = false;

    EBuildToolMode ActiveToolMode = EBuildToolMode::None;
    EGridSelectionShape ActiveShape = EGridSelectionShape::Rectangle;

    FIntVector HoveredVoxel = FIntVector::ZeroValue;
    FIntVector DragStartVoxel = FIntVector::ZeroValue;
    FIntVector LastPreviewEndVoxel = FIntVector(INT32_MIN);

    float HoveredTerrainZ = 0.0f;
    float DragStartTerrainZ = 0.0f;

    /** Confirmed selection cells: voxel coord -> terrain Z. */
    TMap<FIntVector, float> SelectedCells;

    /** Temporary selection while mouse is held: voxel coord -> terrain Z. */
    TMap<FIntVector, float> DragPreviewCells;

    static constexpr float CellSizeUU = 100.0f;
    static constexpr float TraceDistance = 1000000.0f;

private:
    void SpawnOverlayIfNeeded();
    void DestroyOverlay();
    void RegisterDefaultOverlayStyles();
    void PushOverlayMode();
    void PushHoverToOverlay();
    void PushSelectionToOverlay();
    void PushPreviewToOverlay();

    bool RaycastMouseToVoxel(APlayerController* PC, FIntVector& OutVoxel, float& OutTerrainZ, FVector& OutHitLocation) const;
    static FIntVector WorldToVoxel(const FVector& WorldPos, float VoxelSize = CellSizeUU);

    void RebuildDragPreview();
    void BuildRectangleCells(const FIntVector& Start, const FIntVector& End, float TerrainZ, TMap<FIntVector, float>& OutCells) const;
};
