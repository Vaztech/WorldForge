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
class ULayerViewSubsystem;
class UStaticMesh;
class UMaterialInterface;
class UMiningSubsystem;

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

    /**
     * When enabled, selecting a cell that is already selected removes it from
     * the active selection. When disabled, re-selecting an already-selected
     * cell keeps it selected. Exposed so this can later be wired to a player
     * preference/settings menu.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|GridSelection|Settings")
    bool bToggleSelectedCellsOnReselect = true;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Settings")
    void SetToggleSelectedCellsOnReselect(bool bEnabled) { bToggleSelectedCellsOnReselect = bEnabled; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Settings")
    bool GetToggleSelectedCellsOnReselect() const { return bToggleSelectedCellsOnReselect; }

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

    /** Keep only selected cells whose voxel coordinates are allowed by a gameplay commit filter. */
    void RetainSelectedCells(const TSet<FIntVector>& AllowedVoxels);

    /** Persist currently selected cells into the overlay's confirmed-designation layer. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void CommitSelectedCellsToConfirmedOverlay(EGridDesignationType DesignationType);

    /**
     * Commit an explicit set of world cells as confirmed Farm designation tiles.
     *
     * More robust than CommitSelectedCellsToConfirmedOverlay for farm mode because it
     * uses FarmPaintedWorldCells directly instead of GridOverlay::SelectedCells, which
     * may be empty if the selection went through the legacy paint path rather than
     * the grid drag path (e.g. when bSelectionActive was false at click time).
     *
     * Terrain Z is sampled per-cell via VoxelWorldManager. Falls back to Z=0 if VM unavailable.
     */
    void CommitFarmCellsToConfirmedOverlay(const TSet<FIntPoint>& FarmWorldCells);

    /** Visual-only: draw/clear true 3D voxel cubes for the active mining selection. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Mining")
    void SetMiningVolumePreviewCells(const TArray<FIntVector>& VoxelCoords);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Mining")
    void ClearMiningVolumePreviewCells();

    /** Configure selected mining-cell instances to match BP_MiningGhostActor. */
    void SetMiningGhostVisualAssets(
        UStaticMesh* InCubeMesh,
        UMaterialInterface* InGhostMaterial,
        const FLinearColor& InSurfaceTint,
        const FLinearColor& InUndergroundTint,
        float InSurfaceOpacity,
        float InUndergroundOpacity,
        float InCubeScale);

    /** Active settlement used for mine connectivity/depth validation. */
    void SetActiveMineSettlementID(int32 InSettlementID);

    // ---------------------------------------------------------------------
    // Tool Z-layer offsets
    // ---------------------------------------------------------------------

    /** Adjusts the active tool layer. Build uses positive values to build upward; Mine uses negative values to dig downward. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Layer")
    void AdjustActiveToolLayer(int32 Delta);

    /** Resets the active tool's layer offset to surface layer 0. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Layer")
    void ResetActiveToolLayer();

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Layer")
    int32 GetActiveToolLayerOffset() const { return ActiveToolLayerOffset; }

    /** Absolute voxel Z layer currently used by the layer-view system. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Layer")
    int32 GetActiveVoxelLayerZ() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Layer")
    int32 GetBuildLayerOffset() const { return BuildLayerOffset; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Layer")
    int32 GetMineLayerOffset() const { return MineLayerOffset; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Layer")
    int32 GetFarmLayerOffset() const { return 0; }

    /** View layer is a display/slice state for future Dwarf-Fortress-style layer viewing. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Layer")
    void AdjustActiveViewLayer(int32 Delta);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Layer")
    void ResetActiveViewLayer();

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Layer")
    int32 GetActiveViewLayerOffset() const { return ActiveViewLayerOffset; }

    // ---------------------------------------------------------------------
    // Static base grid visibility
    // ---------------------------------------------------------------------

    /** Enable/disable the large static base grid while preserving hover/preview/confirmed cells. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Overlay")
    void SetStaticBaseGridEnabled(bool bEnabled);

    /** Toggle the large static base grid. Returns the new enabled state. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Overlay")
    bool ToggleStaticBaseGrid();

    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Overlay")
    bool IsStaticBaseGridEnabled() const { return bStaticBaseGridEnabled; }

private:
    UPROPERTY(Transient)
    TObjectPtr<ATileGridOverlayActor> GridOverlay = nullptr;

    bool bSelectionActive = false;
    bool bDraggingSelection = false;
    bool bHasValidHover = false;

    /** Controls whether the static base grid layer is drawn while a grid-selection tool is active. */
    bool bStaticBaseGridEnabled = true;

    EBuildToolMode ActiveToolMode = EBuildToolMode::None;
    EGridSelectionShape ActiveShape = EGridSelectionShape::Rectangle;

    FIntVector HoveredVoxel = FIntVector::ZeroValue;
    FIntVector DragStartVoxel = FIntVector::ZeroValue;
    FIntVector LastPreviewEndVoxel = FIntVector(INT32_MIN);

    float HoveredTerrainZ = 0.0f;
    float DragStartTerrainZ = 0.0f;

    // Tool-relative Z layer offsets. Surface is 0. Positive builds upward; negative mines downward.
    int32 BuildLayerOffset = 0;
    int32 MineLayerOffset = 0;

    /** Settlement used when checking existing/completed mining designations for connectivity. */
    int32 ActiveMineSettlementID = INDEX_NONE;

    int32 ActiveToolLayerOffset = 0;
    int32 ActiveViewLayerOffset = 0;

    /** Confirmed selection cells: voxel coord -> terrain Z. */
    TMap<FIntVector, float> SelectedCells;

    /** Temporary selection while mouse is held: voxel coord -> terrain Z. */
    TMap<FIntVector, float> DragPreviewCells;

    static constexpr float CellSizeUU = 100.0f;
    static constexpr float TraceDistance = 1000000.0f;
    static constexpr int32 SurfaceStaticBaseGridRadius = 96;
    static constexpr int32 MineStaticBaseGridRadius = 28;

private:
    void SpawnOverlayIfNeeded();
    void DestroyOverlay();
    void RegisterDefaultOverlayStyles();
    void PushOverlayMode();
    void PushHoverToOverlay();
    void PushSelectionToOverlay();
    void PushPreviewToOverlay();
    void RebuildStaticBaseGridFromHover();

    bool RaycastMouseToVoxel(APlayerController* PC, FIntVector& OutVoxel, float& OutTerrainZ, FVector& OutHitLocation) const;
    ULayerViewSubsystem* GetLayerViewSubsystem() const;
    static FIntVector WorldToVoxel(const FVector& WorldPos, float VoxelSize = CellSizeUU);
    int32 GetStoredLayerOffsetForTool(EBuildToolMode ToolMode) const;
    void StoreLayerOffsetForTool(EBuildToolMode ToolMode, int32 NewOffset);

    void RebuildDragPreview();
    void BuildRectangleCells(const FIntVector& Start, const FIntVector& End, float TerrainZ, TMap<FIntVector, float>& OutCells) const;

    bool IsMineLikeTool() const;
    bool IsAdjacent6(const FIntVector& A, const FIntVector& B) const;
    void BuildMineConnectivityAnchors(TSet<FIntVector>& OutAnchors, bool bCompletedOnly) const;
    int32 ComputeMinimumAllowedMineVoxelZ() const;
    void FilterMineSelectionByConnectivity(const TMap<FIntVector, float>& CandidateCells, TMap<FIntVector, float>& OutAcceptedCells) const;
};
