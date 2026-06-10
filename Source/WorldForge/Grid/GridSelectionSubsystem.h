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

    // CUBE-MINING-V3-FLOOD: counts that let the UI show the multi-layer mining plan
    // without ambiguity. GetSelectedCount() above is the CURRENT active layer only;
    // these expose the whole persistent plan and the per-layer breakdown so a widget
    // can render e.g. "Current layer: 9  /  Total plan: 30".

    /** Total planned mining anchor cells across ALL layers (the full vertical plan). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Mining")
    int32 GetTotalMinePlanCount() const { return PersistentMineAnchorVoxels.Num(); }

    /** Planned mining anchor cells on one specific voxel Z layer. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Mining")
    int32 GetMinePlanCountForLayer(int32 VoxelZ) const;

    /** Number of distinct voxel Z layers that currently hold planned mining cells. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|GridSelection|Mining")
    int32 GetMinePlanLayerCount() const;

    /**
     * Return EVERY cell in the persistent multi-layer mine plan (all layers), not just
     * the current active layer's SelectedCells. This is what Confirm should iterate so
     * the full painted stack becomes mining designations — committing SelectedCells
     * alone only ever creates the one layer the player happens to be viewing.
     * VoxelCoord carries the authoritative grid position; TerrainZ is best-effort
     * (sampled per-column when a VoxelWorldManager is available, else 0).
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Mining")
    void GetPersistentMinePlanCells(TArray<FGridSelectionCell>& OutCells) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void GetSelectedCells(TArray<FGridSelectionCell>& OutCells) const;

    // Mine selection is stored in true 200uu voxel space and is passed directly
    // to the MiningLayerRenderer. Legacy non-mine callers return selected cells as-is.
    void GetSelectedVoxelsForRender(TArray<FIntVector>& OutVoxels) const;

    // Legacy helper for older macro-index callers. Mine no longer uses this path.
    FIntVector MacroIndexToCenterVoxel(const FIntVector& MacroIndex) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void ClearSelectedCells();

    /**
     * Clear the persistent multi-layer mine plan (all carried anchors across layers).
     * Call after a successful Confirm so the next mining session starts clean — the
     * cells are now backend designations and no longer need to live as planning anchors.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection|Mining")
    void ClearMinePlan();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void ClearPreviewCells();

    /** Keep only selected cells whose voxel coordinates are allowed by a gameplay commit filter. */
    void RetainSelectedCells(const TSet<FIntVector>& AllowedVoxels);

    /** Persist currently selected cells into the overlay's confirmed-designation layer. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    void CommitSelectedCellsToConfirmedOverlay(EGridDesignationType DesignationType);

    /**
     * Remove a single confirmed designation cell from the persistent overlay layer.
     * Forwards to GridSelectionSubsystem::GridOverlay (not BuildModeManager::GridOverlay).
     * Safe to call from FarmingSubsystem, MiningSubsystem, or VoxelConstructionSystem
     * when a job completes and the visual designation tile should disappear.
     * @return true if the cell was found and removed; false if it did not exist for that type.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|GridSelection")
    bool RemoveConfirmedDesignationCell(const FIntVector& VoxelCoord, EGridDesignationType DesignationType);

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

    /** Visual-only: draw mining ghost cubes where each cube carries its own tint
     *  (parallel arrays, matched by index). Used for the depth-faded above-layer
     *  ghost stack. */
    void SetMiningVolumePreviewCellsTinted(const TArray<FIntVector>& VoxelCoords, const TArray<FLinearColor>& Tints);

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

    // CUBE-VOXEL-SCALE: fallback only. The live cell size comes from GetCellSizeUU(),
    // which reads the VoxelWorldManager's VoxelSize so selection aligns with the
    // configured grid (200uu by default). This constant is used only when the manager
    // is unavailable (early init) and as the WorldToVoxel default-arg fallback.
    static constexpr float CellSizeUUFallback = 200.0f;
    static constexpr float TraceDistance = 1000000.0f;

    // ── Legacy macro-cell helpers ─────────────────────────────────────────────
    // Non-mine legacy paths may still use 1600uu macro cells. Mine selection/hover/drag
    // now stays in true 200uu voxel space at every layer.
    static constexpr int32 MacroCellVoxelSpan = 8; // 1600uu / 200uu

    // Floored division — REQUIRED: world is origin-centered, voxel coords go negative,
    // and integer / truncates toward zero, creating a half-cell seam at the origin.
    static FORCEINLINE int32 FloorDivMacro(int32 A, int32 B)
    {
        const int32 Q = A / B;
        const int32 R = A % B;
        return (R != 0 && ((R < 0) != (B < 0))) ? Q - 1 : Q;
    }

    // Real voxel -> owning macro INDEX (Z preserved). This is the stored selection key.
    static FIntVector VoxelToMacroIndex(const FIntVector& Voxel)
    {
        return FIntVector(
            FloorDivMacro(Voxel.X, MacroCellVoxelSpan),
            FloorDivMacro(Voxel.Y, MacroCellVoxelSpan),
            Voxel.Z);
    }

    // Macro INDEX -> its 64 real voxel coords on layer VoxelZ.
    static void ExpandMacroIndexToVoxels(
        const FIntVector& MacroIndex, int32 VoxelZ, TArray<FIntVector>& OutVoxels)
    {
        OutVoxels.Reset(MacroCellVoxelSpan * MacroCellVoxelSpan);
        const int32 BaseX = MacroIndex.X * MacroCellVoxelSpan;
        const int32 BaseY = MacroIndex.Y * MacroCellVoxelSpan;
        for (int32 dy = 0; dy < MacroCellVoxelSpan; ++dy)
            for (int32 dx = 0; dx < MacroCellVoxelSpan; ++dx)
                OutVoxels.Emplace(BaseX + dx, BaseY + dy, VoxelZ);
    }

    // Player-facing macro cell size (1600uu). Gameplay grid stays GetCellSizeUU() (200uu).
    float GetMacroCellSizeUU() const;

    // Converts true voxel coords to the active selection key space. Mine stays in
    // voxel space at every layer; legacy non-mine paths keep the macro key path.
    FIntVector MakeSelectionKeyFromVoxel(const FIntVector& Voxel) const;

    // Per-macro validity tint for the drag preview (decision: NoneValid -> red). Built in
    // RebuildDragPreview, consumed by PushPreviewToOverlay. Keyed by macro index, parallel
    // to DragPreviewCells. Harmless if the overlay tinted path is not yet wired.
    TMap<FIntVector, FLinearColor> PreviewMacroTints;

    // Legacy macro expansion/filter helper retained for non-mine callers.
    enum class EMacroValidity : uint8 { AllValid, Partial, NoneValid };
    void ExpandMacroAndFilter(
        const FIntVector& MacroIndex, int32 VoxelZ, float CellTerrainZ,
        TMap<FIntVector, float>& OutValidVoxels,
        int32& OutValidCount, int32& OutTotalCount) const;
    EMacroValidity SummarizeMacroValidity(int32 ValidCount, int32 TotalCount) const;
    static constexpr int32 SurfaceStaticBaseGridRadius = 96;
    static constexpr int32 MineStaticBaseGridRadius = 28;
    static constexpr int32 MaxMinePreviewDepthBelowSurface = 3;
    static constexpr int32 MineLayerStepSize = 3;

    /** Live voxel cell size in UU, sourced from VoxelWorldManager::VoxelSize. */
    float GetCellSizeUU() const;

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
    static FIntVector WorldToVoxel(const FVector& WorldPos, float VoxelSize = CellSizeUUFallback);
    int32 GetStoredLayerOffsetForTool(EBuildToolMode ToolMode) const;
    void StoreLayerOffsetForTool(EBuildToolMode ToolMode, int32 NewOffset);

    void RebuildDragPreview();
    void BuildRectangleCells(const FIntVector& Start, const FIntVector& End, float TerrainZ, TMap<FIntVector, float>& OutCells) const;

    bool IsMineLikeTool() const;

    // True when Mine tool is active at surface layer (offset == 0).
    // Surface Mine uses true 200uu voxel keys for hover/selection/preview.
    bool IsSurfaceMineMode() const;

    // CUBE-MINING-V3-FLOOD: true only when the active layer is an UNDERGROUND mine layer
    // (relative offset < 0). Surface-layer cells (offset 0) are the exposed top of the
    // column, not underground mine targets — they must never enter the persistent mine
    // plan, or Confirm sends them to MiningSubsystem where they resolve as
    // RejectAboveSurface (the 45-of-81 invalid skips). They can still seed the first
    // underground layer via the surface-ceiling rule; they just aren't commit targets.
    bool IsActiveLayerUndergroundMine() const;
    bool IsAdjacent6(const FIntVector& A, const FIntVector& B) const;
    void BuildMineConnectivityAnchors(TSet<FIntVector>& OutAnchors, bool bCompletedOnly) const;
    int32 ComputeMinimumAllowedMineVoxelZ() const;
    void FilterMineSelectionByConnectivity(const TMap<FIntVector, float>& CandidateCells, TMap<FIntVector, float>& OutAcceptedCells) const;

    // CUBE-MINING-V3-ACCESS
    // True if the voxel is open/air (mined out or never solid) OR a completed mine
    // designation has removed it. This is the access primitive rules 3 & 7 depend on.
    // Backed by VoxelWorldManager::GetBlockTypeAt — an invalid tag means air/void.
    bool IsVoxelOpen(const FIntVector& Voxel) const;

    // True if the voxel is a solid block the player is forbidden to mine
    // (lava / water / other liquids). Currently no voxel block-type tags exist for
    // liquids — terrain blocks are Dirt/Stone/Gravel/Clay/Sand/Bedrock and water is
    // a surface-actor concern, not a voxel block type. This is a forward hook: it
    // returns false today and starts rejecting cells the moment liquid block tags
    // are added to WorldForgeTags.h and wired into GetBlockTypeAt.
    bool IsVoxelForbiddenLiquid(const FIntVector& Voxel) const;

    // CUBE-MINING-V3-FLOOD: true if Voxel sits strictly ABOVE its own column surface
    // (air/sky, not solid) and is therefore NOT a valid mine target — the backend would
    // reject it as RejectAboveSurface. Uses the same world->tile->surface resolution as
    // IsVoxelOpen / the confirm resolver, so this filter and MiningSubsystem agree by
    // construction. Used to exclude above-surface cells from the Confirm commit list
    // while still allowing them to exist as ghost/anchors.
    bool IsVoxelAboveColumnSurface(const FIntVector& Voxel) const;

    // Completed mine voxels tracked locally so newly-mined tunnel cells become
    // valid access anchors/frontier even before the next terrain query (rule 7).
    // mutable: refreshed inside const connectivity/depth queries.
    mutable TSet<FIntVector> CompletedMineVoxels;

    // CUBE-MINING-V3-ANCHOR-PERSIST
    // Confirmed mine cells, in the SAME per-voxel grid space as SelectedCells
    // (WorldToGrid3D / 100uu). Unlike SelectedCells this set is NOT cleared on a
    // layer change, so a Z=N cell the player confirmed continues to act as the
    // "Above" access anchor when planning Z=N-1 — even before the MiningSubsystem
    // designation round-trips and regardless of whether ActiveMineSettlementID was
    // ever set. Stored as voxel coord only; Z is meaningful. Populated at confirm
    // time and cleared when the whole mine session ends.
    TSet<FIntVector> PersistentMineAnchorVoxels;

    // CUBE-MINING-V3-ANCHOR-PERSIST
    // Push faint above-layer anchor markers to the overlay while underground so the
    // player can see where the planned shaft/tunnel above them is, and therefore
    // where they may legally continue digging downward.
    void PushAboveLayerAnchorGhost() const;
};