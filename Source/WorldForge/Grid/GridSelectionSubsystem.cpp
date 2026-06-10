// Source/WorldForge/Grid/GridSelectionSubsystem.cpp

#include "Grid/GridSelectionSubsystem.h"
#include "Grid/LayerViewSubsystem.h"

#include "Player/TileGridOverlayActor.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Voxel/VoxelWorldManager.h"
#include "Voxel/WorldForgeCubeTerrainActor.h" // CUBE-TERRAIN-04W-STEP3
#include "World/WorldMapSubsystem.h"          // CUBE-TERRAIN-04W-STEP4
#include "Mining/MiningSubsystem.h"
#include "Colony/SettlementManager.h" // CUBE-MINING-V3-ANCHOR-PERSIST: player-settlement fallback

DEFINE_LOG_CATEGORY_STATIC(LogGridSelection, Log, All);

void UGridSelectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogGridSelection, Log, TEXT("GridSelectionSubsystem initialized."));
}

void UGridSelectionSubsystem::Deinitialize()
{
    DestroyOverlay();
    SelectedCells.Reset();
    DragPreviewCells.Reset();
    PreviewMacroTints.Reset();

    Super::Deinitialize();
}

void UGridSelectionSubsystem::BeginSelectionTool(EBuildToolMode ToolMode, EGridSelectionShape Shape)
{
    ActiveToolMode = ToolMode;
    ActiveShape = Shape;

    // Mining/digging starts from the current hovered surface each time the tool opens.
    // This prevents stale negative offsets from previous mine sessions from drifting
    // the layer/cutaway state on re-entry.
    if (ToolMode == EBuildToolMode::Mine || ToolMode == EBuildToolMode::Dig)
    {
        MineLayerOffset = 0;
        ActiveToolLayerOffset = 0;
    }
    else
    {
        ActiveToolLayerOffset = GetStoredLayerOffsetForTool(ToolMode);
    }

    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        LayerView->EnterLayerView(ToolMode);

    }
    bSelectionActive = true;
    bDraggingSelection = false;
    bHasValidHover = false;
    LastPreviewEndVoxel = FIntVector(INT32_MIN);

    // A new tool session must always start with a clean active selection.
    // Persistent confirmed designation visuals live inside TileGridOverlayActor
    // and are intentionally NOT cleared here.
    SelectedCells.Reset();
    DragPreviewCells.Reset();
    PreviewMacroTints.Reset();

    SpawnOverlayIfNeeded();
    RegisterDefaultOverlayStyles();
    PushOverlayMode();

    if (GridOverlay)
    {
        GridOverlay->ShowOverlay();
        GridOverlay->ClearHover();
        GridOverlay->ClearDragPreview();
        GridOverlay->ClearSelection();
        GridOverlay->SetSelectedCells(SelectedCells);
    }

    // Force one immediate raycast/update when the tool starts so the player sees
    // the neutral/base grid without needing to move the mouse first. This is
    // visual-only and keeps selection empty.
    if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        TickSelectionFromMouse(PC);

        if (ToolMode == EBuildToolMode::Mine)
        {
            TickSelectionFromMouse(PC);
            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-01] Mine BeginSelectionTool hover refresh. HoverValid=%s Hover=(%d,%d,%d)"),
                bHasValidHover ? TEXT("true") : TEXT("false"),
                HoveredVoxel.X,
                HoveredVoxel.Y,
                HoveredVoxel.Z);

            const ULayerViewSubsystem* LV = GetLayerViewSubsystem();
            const int32 VisibleToolLayer = LV ? LV->GetActiveVoxelLayerZ() : HoveredVoxel.Z;
            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-V2] MineGridLayerLock SurfaceZ=%d VisibleToolLayer=%d HoverZ=%d"),
                HoveredVoxel.Z,
                VisibleToolLayer,
                HoveredVoxel.Z);
        }
    }

    if (bStaticBaseGridEnabled)
    {
        RebuildStaticBaseGridFromHover();
    }
    else if (GridOverlay)
    {
        GridOverlay->ClearBaseGrid();
    }

    UE_LOG(LogGridSelection, Log,
        TEXT("BeginSelectionTool Tool=%d Shape=%d ActiveSelected=%d Layer=%d HoverValid=%s Hover=(%d,%d,%d)"),
        static_cast<int32>(ToolMode),
        static_cast<int32>(Shape),
        SelectedCells.Num(),
        ActiveToolLayerOffset,
        bHasValidHover ? TEXT("true") : TEXT("false"),
        HoveredVoxel.X,
        HoveredVoxel.Y,
        HoveredVoxel.Z);

    // [CUBE-SCALE-AUDIT] One-shot scale-contract dump on tool entry, so render/overlay/
    // selection/gameplay cell sizes can be compared at a glance instead of reconstructed
    // from scattered logs. RenderVoxelSize is the authoritative 200uu gameplay grid;
    // Mine now keeps selection keys in true voxel space at every layer.
    {
        float RenderVoxelSize = -1.0f;
        if (const UWorld* World = GetWorld())
        {
            if (const UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
            {
                RenderVoxelSize = VM->GetRenderVoxelSize();
            }
        }
        UE_LOG(LogGridSelection, Log,
            TEXT("[CUBE-SCALE-AUDIT] RenderVoxelSize(gameplay)=%.1f GridSelection.CellSize=%.1f MacroCellSize=%.1f MacroSpan=%d OutVoxelMode=%s DesignationSpace=Voxel"),
            RenderVoxelSize,
            GetCellSizeUU(),
            GetMacroCellSizeUU(),
            MacroCellVoxelSpan,
            ToolMode == EBuildToolMode::Mine ? TEXT("Voxel") : TEXT("MacroIndex"));
    }
}

void UGridSelectionSubsystem::EndSelectionTool(bool bClearSelectedCells)
{
    bSelectionActive = false;
    bDraggingSelection = false;
    bHasValidHover = false;

    // EndSelectionTool ends the *active* tool session. Even when
    // bClearSelectedCells is false, selected cells should not leak into the
    // next tool. Confirmed cells are already copied into the overlay's
    // persistent layer before this function is called.
    SelectedCells.Reset();
    DragPreviewCells.Reset();
    PreviewMacroTints.Reset();
    LastPreviewEndVoxel = FIntVector(INT32_MIN);

    // CUBE-MINING-V3-ANCHOR-PERSIST: the persistent mine-anchor carry is scoped to a
    // single mine-tool session. Clear it here so a later, unrelated dig elsewhere does
    // not inherit stale "planned above" anchors from a previous shaft. Confirmed cells
    // that still matter are re-sourced from MiningSubsystem designations on next use.
    PersistentMineAnchorVoxels.Reset();

    if (GridOverlay)
    {
        GridOverlay->ClearHover();
        GridOverlay->ClearDragPreview();
        GridOverlay->ClearSelection();

        // The base grid is only an active targeting layer. Once the selection
        // session ends, remove the large neutral grid but preserve confirmed
        // designation cells/icons if they exist.
        GridOverlay->ClearBaseGrid();

        if (GridOverlay->HasConfirmedDesignationCells())
        {
            GridOverlay->ShowOverlay();
            GridOverlay->ClearHover();
            GridOverlay->ClearDragPreview();
            GridOverlay->ClearSelection();
            GridOverlay->ClearMiningVolumeCells();
            GridOverlay->ClearBaseGrid();
        }
        else
        {
            GridOverlay->HideOverlay();
        }
    }

    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        LayerView->ExitLayerView();
    }

    UE_LOG(LogGridSelection, Log,
        TEXT("EndSelectionTool RequestedClear=%s ActiveSelectionCleared=true"),
        bClearSelectedCells ? TEXT("true") : TEXT("false"));
}

bool UGridSelectionSubsystem::TickSelectionFromMouse(APlayerController* PC)
{
    if (!bSelectionActive || !PC)
    {
        return false;
    }

    FIntVector NewHoverVoxel = FIntVector::ZeroValue;
    float NewTerrainZ = 0.0f;
    FVector HitLocation = FVector::ZeroVector;

    const bool bHit = RaycastMouseToVoxel(PC, NewHoverVoxel, NewTerrainZ, HitLocation);
    if (!bHit)
    {
        bHasValidHover = false;
        if (GridOverlay)
        {
            GridOverlay->ClearHover();
        }
        return false;
    }

    const bool bHoverChanged = !bHasValidHover || NewHoverVoxel != HoveredVoxel;

    HoveredVoxel = NewHoverVoxel;
    HoveredTerrainZ = NewTerrainZ;
    bHasValidHover = true;

    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        if (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig || ActiveToolMode == EBuildToolMode::Place)
        {
            if (!LayerView->HasActiveVoxelLayer())
            {
                LayerView->SetActiveVoxelLayerZ(HoveredVoxel.Z);
            }

            if (ActiveToolMode == EBuildToolMode::Mine && LayerView->HasActiveVoxelLayer() && LayerView->GetRelativeLayerOffset() < 0)
            {
                const int32 ActiveZ = LayerView->GetActiveVoxelLayerZ();
                UE_LOG(LogGridSelection, Log,
                    TEXT("[CUBE-MINING-V2-UG] PaintZLock ActiveZ=%d Relative=%d HoverZ=%d CellZ=%d"),
                    ActiveZ,
                    LayerView->GetRelativeLayerOffset(),
                    NewHoverVoxel.Z,
                    HoveredVoxel.Z);
            }
        }
    }

    if (bHoverChanged)
    {
        PushHoverToOverlay();

        if (bDraggingSelection && HoveredVoxel != LastPreviewEndVoxel)
        {
            RebuildDragPreview();
            PushPreviewToOverlay();
        }
    }

    return true;
}

bool UGridSelectionSubsystem::BeginDragSelection()
{
    if (!bSelectionActive || !bHasValidHover)
    {
        return false;
    }

    bDraggingSelection = true;
    DragStartVoxel = HoveredVoxel;
    DragStartTerrainZ = HoveredTerrainZ;
    LastPreviewEndVoxel = FIntVector(INT32_MIN);

    RebuildDragPreview();
    PushPreviewToOverlay();

    UE_LOG(LogGridSelection, Log,
        TEXT("BeginDragSelection Start=(%d,%d,%d)"),
        DragStartVoxel.X,
        DragStartVoxel.Y,
        DragStartVoxel.Z);

    return true;
}


bool UGridSelectionSubsystem::EndDragSelection(bool bCommitPreview)
{
    if (!bDraggingSelection)
    {
        return false;
    }

    bDraggingSelection = false;
    const int32 CommittedCount = DragPreviewCells.Num();

    if (bCommitPreview)
    {
        TMap<FIntVector, float>& CellsToCommit = DragPreviewCells;

        if (ActiveToolMode == EBuildToolMode::Mine)
        {
            const bool bUnderground = IsActiveLayerUndergroundMine();
            int32 Added = 0;
            int32 Removed = 0;
            int32 Duplicate = 0;

            for (const TPair<FIntVector, float>& Pair : CellsToCommit)
            {
                const FIntVector& VoxelKey = Pair.Key;
                const bool bDeselect =
                    bToggleSelectedCellsOnReselect && SelectedCells.Contains(VoxelKey);

                if (bDeselect)
                {
                    SelectedCells.Remove(VoxelKey);
                    if (bUnderground)
                    {
                        PersistentMineAnchorVoxels.Remove(VoxelKey);
                    }
                    ++Removed;
                }
                else
                {
                    SelectedCells.FindOrAdd(VoxelKey) = Pair.Value;
                    if (bUnderground)
                    {
                        bool bAlready = false;
                        PersistentMineAnchorVoxels.Add(VoxelKey, &bAlready);
                        if (bAlready) ++Duplicate; else ++Added;
                    }
                    else
                    {
                        ++Added;
                    }
                }
            }

            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-VOXEL] Commit VoxelCells=%d Selected=%d Added=%d Removed=%d Duplicate=%d PersistentAnchors=%d"),
                CellsToCommit.Num(), SelectedCells.Num(), Added, Removed, Duplicate, PersistentMineAnchorVoxels.Num());
        }
        else
        {
            for (const TPair<FIntVector, float>& Pair : CellsToCommit)
            {
                const FIntVector& SelectionKey = Pair.Key;
                const bool bDeselect =
                    bToggleSelectedCellsOnReselect && SelectedCells.Contains(SelectionKey);

                if (bDeselect)
                {
                    SelectedCells.Remove(SelectionKey);
                }
                else
                {
                    SelectedCells.FindOrAdd(SelectionKey) = Pair.Value;
                }
            }
        }

        PushSelectionToOverlay();
    }

    DragPreviewCells.Reset();
    PreviewMacroTints.Reset();
    LastPreviewEndVoxel = FIntVector(INT32_MIN);
    PushPreviewToOverlay();

    // CUBE-MINING-V3-FLOOD: log the THREE distinct counts side by side so they can never
    // be confused again. They legitimately differ:
    //   ThisDrag        = cells the player just dragged and that passed the filter
    //   CurrentLayerSel = all cells selected on the layer currently being viewed
    //                     (accumulates across multiple drags until the layer changes)
    //   TotalMinePlan   = the full persistent multi-layer plan (what Confirm commits)
    UE_LOG(LogGridSelection, Log,
        TEXT("[CUBE-MINING-V3-FLOOD] SelectionCounts ThisDrag=%d CurrentLayerSel=%d TotalMinePlan=%d"),
        CommittedCount, SelectedCells.Num(), PersistentMineAnchorVoxels.Num());

    UE_LOG(LogGridSelection, Log,
        TEXT("EndDragSelection Commit=%s PreviewCells=%d Selected=%d"),
        bCommitPreview ? TEXT("true") : TEXT("false"),
        CommittedCount,
        SelectedCells.Num());

    return true;
}


bool UGridSelectionSubsystem::ToggleHoveredSelection()
{
    if (!bSelectionActive || !bHasValidHover)
    {
        return false;
    }

    const bool bMineTool = ActiveToolMode == EBuildToolMode::Mine;
    const bool bUnderground = bMineTool && IsActiveLayerUndergroundMine();

    if (SelectedCells.Contains(HoveredVoxel))
    {
        if (bToggleSelectedCellsOnReselect)
        {
            SelectedCells.Remove(HoveredVoxel);
            if (bUnderground)
            {
                PersistentMineAnchorVoxels.Remove(HoveredVoxel);
                UE_LOG(LogGridSelection, Log,
                    TEXT("[CUBE-MINING-VOXEL] ClickRetract Voxel=(%d,%d,%d) PersistentAnchors=%d"),
                    HoveredVoxel.X, HoveredVoxel.Y, HoveredVoxel.Z, PersistentMineAnchorVoxels.Num());
            }
        }
        else
        {
            SelectedCells.FindOrAdd(HoveredVoxel) = HoveredTerrainZ;
        }
    }
    else
    {
        if (bUnderground)
        {
            TMap<FIntVector, float> Candidate;
            Candidate.Add(HoveredVoxel, HoveredTerrainZ);

            TMap<FIntVector, float> Accepted;
            FilterMineSelectionByConnectivity(Candidate, Accepted);

            if (!Accepted.Contains(HoveredVoxel) ||
                IsVoxelForbiddenLiquid(HoveredVoxel) ||
                IsVoxelAboveColumnSurface(HoveredVoxel))
            {
                UE_LOG(LogGridSelection, Verbose,
                    TEXT("[CUBE-MINING-VOXEL] Click rejected; invalid voxel=(%d,%d,%d)"),
                    HoveredVoxel.X, HoveredVoxel.Y, HoveredVoxel.Z);
                return false;
            }
        }

        SelectedCells.Add(HoveredVoxel, HoveredTerrainZ);

        if (bUnderground)
        {
            bool bAlready = false;
            PersistentMineAnchorVoxels.Add(HoveredVoxel, &bAlready);
            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-VOXEL] ClickCommit Voxel=(%d,%d,%d) Added=%s PersistentAnchors=%d"),
                HoveredVoxel.X, HoveredVoxel.Y, HoveredVoxel.Z,
                bAlready ? TEXT("false") : TEXT("true"),
                PersistentMineAnchorVoxels.Num());
        }
    }

    PushSelectionToOverlay();

    UE_LOG(LogGridSelection, Log,
        TEXT("ToggleHoveredSelection Hover=(%d,%d,%d) Selected=%d"),
        HoveredVoxel.X,
        HoveredVoxel.Y,
        HoveredVoxel.Z,
        SelectedCells.Num());

    return true;
}
void UGridSelectionSubsystem::GetSelectedCells(TArray<FGridSelectionCell>& OutCells) const
{
    OutCells.Reset();
    OutCells.Reserve(SelectedCells.Num());

    for (const TPair<FIntVector, float>& Pair : SelectedCells)
    {
        FGridSelectionCell Cell;
        Cell.VoxelCoord = Pair.Key;
        Cell.TerrainZ = Pair.Value;
        OutCells.Add(Cell);
    }
}

int32 UGridSelectionSubsystem::GetMinePlanCountForLayer(int32 VoxelZ) const
{
    int32 Count = 0;
    for (const FIntVector& Anchor : PersistentMineAnchorVoxels)
    {
        if (Anchor.Z == VoxelZ)
        {
            ++Count;
        }
    }
    return Count;
}

int32 UGridSelectionSubsystem::GetMinePlanLayerCount() const
{
    TSet<int32> Layers;
    for (const FIntVector& Anchor : PersistentMineAnchorVoxels)
    {
        Layers.Add(Anchor.Z);
    }
    return Layers.Num();
}

void UGridSelectionSubsystem::GetPersistentMinePlanCells(TArray<FGridSelectionCell>& OutCells) const
{
    OutCells.Reset();
    OutCells.Reserve(PersistentMineAnchorVoxels.Num());

    const UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;

    int32 ExcludedAboveSurface = 0;

    for (const FIntVector& Voxel : PersistentMineAnchorVoxels)
    {
        // CUBE-MINING-V3-FLOOD: exclude cells that sit above their own column surface.
        // The layer-relative model assumes a flat surface, but terrain isn't flat: a
        // cell painted on "first underground layer" (e.g. Z=17) can be over a column
        // whose real surface is lower (Z=16), making it air above solid. Those cells
        // are kept as planning anchors/ghosts (they still live in PersistentMineAnchorVoxels
        // for the ghost stack) but must NOT be sent to MiningSubsystem, which would
        // reject them as RejectAboveSurface. This makes the commit list match what the
        // backend will actually accept, so SkippedInvalid drops to 0.
        if (IsVoxelAboveColumnSurface(Voxel))
        {
            ++ExcludedAboveSurface;
            continue;
        }

        FGridSelectionCell Cell;
        Cell.VoxelCoord = Voxel;

        // TerrainZ is best-effort: the designation path recomputes geometry from the
        // grid position via ResolveMineTarget, so an exact world Z is not required here.
        // We still provide the voxel-center world Z when possible for overlay reuse.
        if (VM)
        {
            const FVector WorldPos = VM->GridToWorld(FIntPoint(Voxel.X, Voxel.Y), Voxel.Z);
            Cell.TerrainZ = WorldPos.Z;
        }
        else
        {
            Cell.TerrainZ = 0.0f;
        }

        OutCells.Add(Cell);
    }

    UE_LOG(LogGridSelection, Log,
        TEXT("[CUBE-MINING-V3-FLOOD] GetPersistentMinePlanCells PlanAnchors=%d CommitEligible=%d AboveSurfaceExcluded=%d Layers=%d"),
        PersistentMineAnchorVoxels.Num(), OutCells.Num(), ExcludedAboveSurface, GetMinePlanLayerCount());
}

void UGridSelectionSubsystem::ClearSelectedCells()
{
    SelectedCells.Reset();
    if (GridOverlay)
    {
        GridOverlay->ClearSelection();
        GridOverlay->ClearMiningVolumeCells();
    }
}

void UGridSelectionSubsystem::ClearMinePlan()
{
    const int32 Cleared = PersistentMineAnchorVoxels.Num();
    PersistentMineAnchorVoxels.Reset();
    UE_LOG(LogGridSelection, Log,
        TEXT("[CUBE-MINING-V3-FLOOD] ClearMinePlan ClearedAnchors=%d"),
        Cleared);
}


void UGridSelectionSubsystem::RetainSelectedCells(const TSet<FIntVector>& AllowedVoxels)
{
    TArray<FIntVector> CellsToRemove;

    for (const TPair<FIntVector, float>& Pair : SelectedCells)
    {
        bool bKeepCell = false;

        if (ActiveToolMode == EBuildToolMode::Mine)
        {
            bKeepCell = AllowedVoxels.Contains(Pair.Key);
        }
        else
        {
            // Legacy macro selection keys may be retained by any allowed voxel they cover.
            TArray<FIntVector> Voxels;
            ExpandMacroIndexToVoxels(Pair.Key, Pair.Key.Z, Voxels);
            for (const FIntVector& V : Voxels)
            {
                if (AllowedVoxels.Contains(V))
                {
                    bKeepCell = true;
                    break;
                }
            }
        }

        if (!bKeepCell)
        {
            CellsToRemove.Add(Pair.Key);
        }
    }

    for (const FIntVector& Cell : CellsToRemove)
    {
        SelectedCells.Remove(Cell);
    }

    PushSelectionToOverlay();

    UE_LOG(LogGridSelection, Log,
        TEXT("RetainSelectedCells Allowed=%d Removed=%d Remaining=%d"),
        AllowedVoxels.Num(),
        CellsToRemove.Num(),
        SelectedCells.Num());
}


void UGridSelectionSubsystem::ClearPreviewCells()
{
    DragPreviewCells.Reset();
    PreviewMacroTints.Reset();
    if (GridOverlay)
    {
        GridOverlay->ClearDragPreview();
    }
}

void UGridSelectionSubsystem::CommitSelectedCellsToConfirmedOverlay(EGridDesignationType DesignationType)
{
    if (!GridOverlay)
    {
        return;
    }

    GridOverlay->CommitSelectedCellsAsConfirmed(DesignationType);

    // CUBE-MINING-V3-ANCHOR-PERSIST: when a Mine selection is confirmed, copy its
    // cells into the layer-independent anchor set BEFORE the next layer change wipes
    // SelectedCells. These cells then anchor the layer directly below them, which is
    // what lets the player keep digging 14 -> 13 -> 12 ... without waiting for any
    // colonist to physically remove a voxel.
    //
    // CUBE-MINING-V3-FLOOD: gated to underground layers — surface cells (offset 0) are
    // the exposed column top, not mine targets, and must not enter the commit plan.
    if (DesignationType == EGridDesignationType::Mine && IsActiveLayerUndergroundMine())
    {
        int32 SumAdded = 0;
        int32 SumDuplicate = 0;
        for (const TPair<FIntVector, float>& Pair : SelectedCells)
        {
            bool bAlready = false;
            PersistentMineAnchorVoxels.Add(Pair.Key, &bAlready);
            if (bAlready) ++SumDuplicate; else ++SumAdded;
        }

        UE_LOG(LogGridSelection, Log,
            TEXT("[CUBE-MINING-VOXEL] ConfirmPersist Voxels=%d Added=%d Duplicate=%d PersistentAnchors=%d"),
            SelectedCells.Num(), SumAdded, SumDuplicate, PersistentMineAnchorVoxels.Num());
    }

    UE_LOG(LogGridSelection, Log,
        TEXT("Committed selected cells to confirmed overlay. Designation=%d Selected=%d"),
        static_cast<int32>(DesignationType),
        SelectedCells.Num());
}

void UGridSelectionSubsystem::CommitFarmCellsToConfirmedOverlay(const TSet<FIntPoint>& FarmWorldCells)
{
    if (!GridOverlay)
    {
        UE_LOG(LogGridSelection, Warning,
            TEXT("[GridOverlay] CommitFarmCellsToConfirmedOverlay: GridOverlay not available. Farm markers will not be visible."));
        return;
    }

    if (FarmWorldCells.Num() == 0)
    {
        UE_LOG(LogGridSelection, Warning,
            TEXT("[GridOverlay] CommitFarmCellsToConfirmedOverlay: FarmWorldCells is empty — nothing to confirm."));
        return;
    }

    UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;
    int32 AddedCount = 0;

    for (const FIntPoint& WorldCell : FarmWorldCells)
    {
        int32 SurfaceZ = 0;
        float TerrainZ = 0.0f;

        if (VM)
        {
            VM->TryGetTerrainSurfaceVoxelZ(WorldCell, SurfaceZ);
            const FVector WorldPos = VM->GridToWorld(WorldCell, SurfaceZ);
            // GridToWorld returns the voxel center. Add half a voxel to reach the
            // top face of the surface voxel so the confirmed farm overlay sits on
            // the terrain rather than clipping into it.
            TerrainZ = WorldPos.Z + GetCellSizeUU() * 0.5f;
        }

        const FIntVector VoxelCoord(WorldCell.X, WorldCell.Y, SurfaceZ);
        GridOverlay->AddConfirmedDesignationCell(VoxelCoord, TerrainZ, EGridDesignationType::Farm);
        ++AddedCount;
    }

    if (AddedCount > 0)
    {
        GridOverlay->ShowOverlay();
    }

    UE_LOG(LogGridSelection, Log,
        TEXT("[GridOverlay] Confirmed farm markers visible. Count=%d"),
        AddedCount);
}

bool UGridSelectionSubsystem::RemoveConfirmedDesignationCell(const FIntVector& VoxelCoord, EGridDesignationType DesignationType)
{
    UE_LOG(LogGridSelection, Log,
        TEXT("[GridSelection] RemoveConfirmedDesignationCell: Requested. Cell=(%d,%d,%d) Type=%d GridOverlay=%s"),
        VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z,
        static_cast<int32>(DesignationType),
        GridOverlay ? TEXT("valid") : TEXT("null"));

    if (!GridOverlay)
    {
        UE_LOG(LogGridSelection, Warning,
            TEXT("[GridSelection] RemoveConfirmedDesignationCell: GridOverlay not available — cannot remove Cell=(%d,%d,%d) Type=%d"),
            VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z, static_cast<int32>(DesignationType));
        return false;
    }

    const bool bRemoved = GridOverlay->RemoveConfirmedDesignationCell(VoxelCoord, DesignationType);

    UE_LOG(LogGridSelection, Log,
        TEXT("[GridSelection] RemoveConfirmedDesignationCell: Result=%s Cell=(%d,%d,%d) Type=%d RemainingConfirmed=%d"),
        bRemoved ? TEXT("removed") : TEXT("not found"),
        VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z,
        static_cast<int32>(DesignationType),
        GridOverlay->HasConfirmedDesignationCells() ? -1 : 0);

    return bRemoved;
}

void UGridSelectionSubsystem::SetMiningVolumePreviewCells(const TArray<FIntVector>& VoxelCoords)
{
    if (GridOverlay)
    {
        // Mine selections now render as flat 200uu tile overlays on both surface
        // and underground layers. Do not rely on the static grid rebuild to set
        // this flag; selection preview must stay correct even if the grid is
        // hidden/toggled or has not rebuilt this frame.
        bool bUndergroundMineLayer = false;
        if (const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
        {
            bUndergroundMineLayer = LayerView->GetRelativeLayerOffset() < 0;
        }
        else
        {
            bUndergroundMineLayer = ActiveToolLayerOffset < 0;
        }

        GridOverlay->SetUseUndergroundWhiteGrid(bUndergroundMineLayer);
        GridOverlay->SetMiningVolumeCells(VoxelCoords);
    }
}

void UGridSelectionSubsystem::ClearMiningVolumePreviewCells()
{
    if (GridOverlay)
    {
        GridOverlay->ClearMiningVolumeCells();
    }
}

void UGridSelectionSubsystem::SetMiningVolumePreviewCellsTinted(
    const TArray<FIntVector>& VoxelCoords,
    const TArray<FLinearColor>& Tints)
{
    if (GridOverlay)
    {
        bool bUndergroundMineLayer = false;
        if (const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
        {
            bUndergroundMineLayer = LayerView->GetRelativeLayerOffset() < 0;
        }
        else
        {
            bUndergroundMineLayer = ActiveToolLayerOffset < 0;
        }

        GridOverlay->SetUseUndergroundWhiteGrid(bUndergroundMineLayer);
        GridOverlay->SetMiningVolumeCellsTinted(VoxelCoords, Tints);
    }
}

void UGridSelectionSubsystem::SetMiningGhostVisualAssets(
    UStaticMesh* InCubeMesh,
    UMaterialInterface* InGhostMaterial,
    const FLinearColor& InSurfaceTint,
    const FLinearColor& InUndergroundTint,
    float InSurfaceOpacity,
    float InUndergroundOpacity,
    float InCubeScale)
{
    if (GridOverlay)
    {
        GridOverlay->ConfigureMiningGhostVisual(
            InCubeMesh,
            InGhostMaterial,
            InSurfaceTint,
            InUndergroundTint,
            InSurfaceOpacity,
            InUndergroundOpacity,
            InCubeScale);
    }
}


void UGridSelectionSubsystem::SetActiveMineSettlementID(int32 InSettlementID)
{
    ActiveMineSettlementID = InSettlementID;
}

void UGridSelectionSubsystem::SetStaticBaseGridEnabled(bool bEnabled)
{
    if (bStaticBaseGridEnabled == bEnabled)
    {
        return;
    }

    bStaticBaseGridEnabled = bEnabled;

    if (!GridOverlay)
    {
        return;
    }

    if (bStaticBaseGridEnabled)
    {
        RebuildStaticBaseGridFromHover();
    }
    else
    {
        GridOverlay->ClearBaseGrid();
    }

    UE_LOG(LogGridSelection, Log, TEXT("Static base grid enabled=%s"), bStaticBaseGridEnabled ? TEXT("true") : TEXT("false"));
}

bool UGridSelectionSubsystem::ToggleStaticBaseGrid()
{
    SetStaticBaseGridEnabled(!bStaticBaseGridEnabled);
    return bStaticBaseGridEnabled;
}

void UGridSelectionSubsystem::SpawnOverlayIfNeeded()
{
    if (GridOverlay || !GetWorld())
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    GridOverlay = GetWorld()->SpawnActor<ATileGridOverlayActor>(
        ATileGridOverlayActor::StaticClass(),
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        Params);

    if (GridOverlay)
    {
        // Default to the legacy macro grid until a tool session pushes its mode-specific scale.
        GridOverlay->SetCellSizeUU(GetMacroCellSizeUU());
        GridOverlay->HideOverlay();
        UE_LOG(LogGridSelection, Log, TEXT("Grid overlay actor spawned."));
    }
    else
    {
        UE_LOG(LogGridSelection, Warning, TEXT("Failed to spawn grid overlay actor."));
    }
}

void UGridSelectionSubsystem::DestroyOverlay()
{
    if (GridOverlay)
    {
        GridOverlay->Destroy();
        GridOverlay = nullptr;
    }
}

void UGridSelectionSubsystem::RegisterDefaultOverlayStyles()
{
    if (!GridOverlay)
    {
        return;
    }

    FGridOverlayStyle BuildStyle;
    BuildStyle.TileColor = FLinearColor(0.15f, 0.45f, 1.0f, 0.35f);
    BuildStyle.BorderColor = FLinearColor(0.2f, 0.7f, 1.0f, 0.90f);
    BuildStyle.HoverColor = FLinearColor(0.5f, 0.9f, 1.0f, 0.65f);
    BuildStyle.DragPreviewColor = FLinearColor(0.35f, 0.65f, 1.0f, 0.25f);
    GridOverlay->RegisterModeStyle(EBuildToolMode::Place, BuildStyle);

    FGridOverlayStyle FarmStyle;
    FarmStyle.TileColor = FLinearColor(0.1f, 0.75f, 0.25f, 0.35f);
    FarmStyle.BorderColor = FLinearColor(0.25f, 1.0f, 0.35f, 0.90f);
    FarmStyle.HoverColor = FLinearColor(0.6f, 1.0f, 0.55f, 0.65f);
    FarmStyle.DragPreviewColor = FLinearColor(0.35f, 1.0f, 0.35f, 0.25f);
    GridOverlay->RegisterModeStyle(EBuildToolMode::Farm, FarmStyle);

    FGridOverlayStyle MineStyle;
    MineStyle.TileColor = FLinearColor(0.18f, 0.18f, 0.18f, 0.55f);
    MineStyle.BorderColor = FLinearColor(0.70f, 0.70f, 0.70f, 1.00f);
    MineStyle.HoverColor = FLinearColor(0.35f, 0.35f, 0.35f, 0.60f);
    MineStyle.DragPreviewColor = FLinearColor(0.24f, 0.24f, 0.24f, 0.45f);
    GridOverlay->RegisterModeStyle(EBuildToolMode::Mine, MineStyle);

    FGridOverlayStyle DigStyle;
    DigStyle.TileColor = FLinearColor(0.75f, 0.3f, 1.0f, 0.35f);
    DigStyle.BorderColor = FLinearColor(0.9f, 0.5f, 1.0f, 0.90f);
    DigStyle.HoverColor = FLinearColor(1.0f, 0.6f, 1.0f, 0.65f);
    DigStyle.DragPreviewColor = FLinearColor(0.8f, 0.4f, 1.0f, 0.25f);
    GridOverlay->RegisterModeStyle(EBuildToolMode::Dig, DigStyle);
}

void UGridSelectionSubsystem::PushOverlayMode()
{
    if (GridOverlay)
    {
        GridOverlay->SetActiveMode(ActiveToolMode);
        // Mine uses the true 200uu voxel scale at every layer. Legacy non-mine tools
        // keep the macro footprint used by their existing overlay paths.
        GridOverlay->SetCellSizeUU(ActiveToolMode == EBuildToolMode::Mine ? GetCellSizeUU() : GetMacroCellSizeUU());
    }
}


void UGridSelectionSubsystem::PushHoverToOverlay()
{
    if (!GridOverlay || !bHasValidHover)
    {
        return;
    }

    GridOverlay->SetHoveredCell(HoveredVoxel, HoveredTerrainZ);

    if (ActiveToolMode == EBuildToolMode::Mine && !bDraggingSelection)
    {
        GridOverlay->ClearDragPreview();
    }
}


void UGridSelectionSubsystem::PushSelectionToOverlay()
{
    if (!GridOverlay)
    {
        return;
    }

    GridOverlay->SetSelectedCells(SelectedCells);

    const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    const bool bIsUndergroundMine =
        ActiveToolMode == EBuildToolMode::Mine &&
        LayerView && LayerView->HasActiveVoxelLayer() &&
        LayerView->GetRelativeLayerOffset() < 0;

    if (bIsUndergroundMine)
    {
        const int32 ActiveZ = LayerView->GetActiveVoxelLayerZ();
        TArray<FIntVector> UGVoxels;
        UGVoxels.Reserve(SelectedCells.Num());
        for (const TPair<FIntVector, float>& Pair : SelectedCells)
        {
            if (Pair.Key.Z == ActiveZ)
            {
                UGVoxels.Add(Pair.Key);
            }
        }
        SetMiningVolumePreviewCells(UGVoxels);
        UE_LOG(LogGridSelection, Log,
            TEXT("[CUBE-MINING-VOXEL] UndergroundFlatOverlay Voxels=%d ActiveZ=%d Relative=%d"),
            UGVoxels.Num(), ActiveZ, LayerView->GetRelativeLayerOffset());
    }
    else if (ActiveToolMode == EBuildToolMode::Mine)
    {
        const int32 ActiveZ = LayerView ? LayerView->GetActiveVoxelLayerZ() : (bHasValidHover ? HoveredVoxel.Z : 0);
        const int32 RelOffset = LayerView ? LayerView->GetRelativeLayerOffset() : 0;
        UE_LOG(LogGridSelection, Log,
            TEXT("[CUBE-MINING-V2-UG] SurfaceMinePath Preserved=true ActiveZ=%d Relative=%d"),
            ActiveZ, RelOffset);
    }
}


void UGridSelectionSubsystem::PushPreviewToOverlay()
{
    if (!GridOverlay)
    {
        return;
    }

    // Mine preview cells are true 200uu voxels. PreviewMacroTints is retained only
    // for legacy non-mine macro callers.
    if (PreviewMacroTints.Num() == DragPreviewCells.Num() && DragPreviewCells.Num() > 0)
    {
        GridOverlay->SetDragPreviewCellsTinted(DragPreviewCells, PreviewMacroTints);
    }
    else
    {
        GridOverlay->SetDragPreviewCells(DragPreviewCells);
    }
}

bool UGridSelectionSubsystem::RaycastMouseToVoxel(
    APlayerController* PC,
    FIntVector& OutVoxel,
    float& OutTerrainZ,
    FVector& OutHitLocation) const
{
    if (!PC || !PC->GetWorld())
    {
        return false;
    }

    FVector RayOrigin = FVector::ZeroVector;
    FVector RayDirection = FVector::ForwardVector;
    if (!PC->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
    {
        return false;
    }

    ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    // Mine mode ALWAYS uses the voxel-layer plane cast, even at surface.
    // Removed the "ActiveToolLayerOffset < 0" guard that caused surface mine to
    // fall through to SDF terrain collision, producing terrain-contoured hover Z.
    const bool bUseSyntheticLayerPlane =
        LayerView &&
        LayerView->IsLayerViewActive() &&
        LayerView->HasActiveVoxelLayer() &&
        (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig);

    const bool bIsSurfaceMineSelection =
        ActiveToolMode == EBuildToolMode::Mine &&
        ActiveToolLayerOffset == 0 &&
        (!LayerView || !LayerView->HasActiveVoxelLayer() || LayerView->GetRelativeLayerOffset() == 0);

    if (bIsSurfaceMineSelection && FMath::Abs(RayDirection.Z) > KINDA_SMALL_NUMBER)
    {
        UWorld* World = PC->GetWorld();
        UWorldMapSubsystem* WorldMap = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
        UVoxelWorldManager* VM = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;

        if (WorldMap && VM)
        {
            auto TryProjectToTile = [&](float PlaneZ, FVector& OutProjectedPoint, FIntPoint& OutTileXY) -> bool
                {
                    const float T = (PlaneZ - RayOrigin.Z) / RayDirection.Z;
                    if (T < 0.0f)
                    {
                        return false;
                    }

                    OutProjectedPoint = RayOrigin + (RayDirection * T);
                    OutProjectedPoint.Z = PlaneZ;

                    int32 TileX = INDEX_NONE;
                    int32 TileY = INDEX_NONE;
                    if (!WorldMap->WorldPositionToTileXY(OutProjectedPoint, TileX, TileY))
                    {
                        return false;
                    }

                    OutTileXY = FIntPoint(TileX, TileY);
                    return true;
                };

            auto ResolveSurfaceWorldZ = [&](const FIntPoint& TileXY, int32 SurfaceZ, float& OutSurfaceWorldZ) -> bool
                {
                    FVector SurfaceWorldLocation = FVector::ZeroVector;
                    if (VM->TryGetSurfaceWorldLocation(TileXY, SurfaceWorldLocation, 0.0f))
                    {
                        OutSurfaceWorldZ = SurfaceWorldLocation.Z;
                        return true;
                    }

                    OutSurfaceWorldZ = static_cast<float>(SurfaceZ) * VM->GetRenderVoxelSize();
                    return false;
                };

            // OPTION A (SURFACE-MINE-Z-PARITY):
            // Build/Farm render their selected tiles on the literal SDF mesh surface
            // because their Z comes straight from the visibility raycast hit
            // (Hit.ImpactPoint.Z). Surface mine previously used a voxel-derived Z
            // (TryGetSurfaceWorldLocation / SurfaceZ * RenderVoxelSize), which lands on
            // the voxel base face — below the interpolated SDF surface — so the tile
            // rendered buried under terrain/water/grass.
            //
            // Fix: do the same visibility trace Build/Farm do FIRST. If it hits real
            // terrain, take Hit.ImpactPoint.Z as the authoritative surface Z (identical
            // to Build/Farm), while keeping the absolute-tile XY from the WorldMap
            // projection below for claim validation. Only if the trace misses (occlusion
            // or off-mesh) do we fall through to the legacy plane/voxel Z path.
            //
            // This block only runs when bIsSurfaceMineSelection is true (Mine tool,
            // ActiveToolLayerOffset == 0, relative layer offset 0). Underground mining
            // (relative offset < 0) never enters here and is unaffected.
            float MeshHitSurfaceZ = 0.0f;
            bool  bHasMeshHitSurfaceZ = false;
            {
                FCollisionQueryParams SurfaceMineParams(SCENE_QUERY_STAT(GridSelectionSurfaceMineZ), true);
                SurfaceMineParams.bReturnPhysicalMaterial = false;
                SurfaceMineParams.AddIgnoredActor(PC->GetPawn());
                if (GridOverlay)
                {
                    SurfaceMineParams.AddIgnoredActor(GridOverlay);
                }

                FHitResult SurfaceMineHit;
                const bool bSurfaceMineHit = World->LineTraceSingleByChannel(
                    SurfaceMineHit,
                    RayOrigin,
                    RayOrigin + (RayDirection * TraceDistance),
                    ECC_Visibility,
                    SurfaceMineParams);

                if (bSurfaceMineHit)
                {
                    MeshHitSurfaceZ = SurfaceMineHit.ImpactPoint.Z;
                    bHasMeshHitSurfaceZ = true;
                }
            }

            float ProjectionPlaneZ = 0.0f;
            if (LayerView && LayerView->HasActiveVoxelLayer())
            {
                ProjectionPlaneZ = LayerView->GetActiveLayerWorldZ();
            }
            else if (FMath::Abs(HoveredTerrainZ) > KINDA_SMALL_NUMBER)
            {
                ProjectionPlaneZ = HoveredTerrainZ;
            }

            FVector ProjectedPoint = FVector::ZeroVector;
            FIntPoint TileXY(INDEX_NONE, INDEX_NONE);
            if (TryProjectToTile(ProjectionPlaneZ, ProjectedPoint, TileXY))
            {
                int32 SurfaceZ = 0;
                if (VM->TryGetTerrainSurfaceVoxelZ(TileXY, SurfaceZ))
                {
                    float SurfaceWorldZ = 0.0f;
                    ResolveSurfaceWorldZ(TileXY, SurfaceZ, SurfaceWorldZ);

                    FVector RefinedProjectedPoint = FVector::ZeroVector;
                    FIntPoint RefinedTileXY(INDEX_NONE, INDEX_NONE);
                    if (!FMath::IsNearlyEqual(SurfaceWorldZ, ProjectionPlaneZ) &&
                        TryProjectToTile(SurfaceWorldZ, RefinedProjectedPoint, RefinedTileXY))
                    {
                        int32 RefinedSurfaceZ = 0;
                        if (VM->TryGetTerrainSurfaceVoxelZ(RefinedTileXY, RefinedSurfaceZ))
                        {
                            TileXY = RefinedTileXY;
                            ProjectedPoint = RefinedProjectedPoint;
                            SurfaceZ = RefinedSurfaceZ;
                            ResolveSurfaceWorldZ(TileXY, SurfaceZ, SurfaceWorldZ);
                        }
                    }

                    if (LayerView)
                    {
                        LayerView->InitializeFromSurfaceVoxelZ(SurfaceZ, 0);
                    }

                    const FIntVector VisualGridCell = VM->WorldToGrid3D(ProjectedPoint);

                    // OPTION A: prefer the real SDF mesh-hit Z (identical to Build/Farm).
                    // Fall back to the voxel-derived SurfaceWorldZ only when the
                    // visibility trace missed (occlusion / off-mesh).
                    const float FinalSurfaceZ = bHasMeshHitSurfaceZ ? MeshHitSurfaceZ : SurfaceWorldZ;

                    OutVoxel = FIntVector(VisualGridCell.X, VisualGridCell.Y, SurfaceZ);
                    OutVoxel = MakeSelectionKeyFromVoxel(OutVoxel);
                    OutTerrainZ = FinalSurfaceZ;
                    OutHitLocation = ProjectedPoint;
                    OutHitLocation.Z = FinalSurfaceZ;

                    UE_LOG(LogGridSelection, Log,
                        TEXT("[CUBE-MINING-V2] MineCoordSplit World=(%.1f,%.1f,%.1f) VisualGrid=(%d,%d,%d) TerrainTile=(%d,%d) SurfaceZ=%d SurfaceWorldZ=%.1f MeshHitZ=%.1f ZSource=%s FinalZ=%.1f"),
                        ProjectedPoint.X, ProjectedPoint.Y, ProjectedPoint.Z,
                        VisualGridCell.X, VisualGridCell.Y, SurfaceZ,
                        TileXY.X, TileXY.Y,
                        SurfaceZ,
                        SurfaceWorldZ,
                        bHasMeshHitSurfaceZ ? MeshHitSurfaceZ : 0.0f,
                        bHasMeshHitSurfaceZ ? TEXT("MeshHit") : TEXT("VoxelPlane"),
                        FinalSurfaceZ);
                    UE_LOG(LogGridSelection, Log,
                        TEXT("[CUBE-MINING-V2] MinePlaneSelect Tile=(%d,%d) SurfaceZ=%d World=(%.1f,%.1f,%.1f) Source=PlaneFirst"),
                        TileXY.X, TileXY.Y, SurfaceZ,
                        OutHitLocation.X, OutHitLocation.Y, OutHitLocation.Z);

                    return true;
                }
            }
        }
    }

    // CUBE-TERRAIN-04W-STEP3: CubePrototype path — use the real cube/terrain hit XY
    // instead of synthetic layer-plane math. The synthetic-plane block below converts
    // a flat-plane intersection through the SDF voxel origin (GridToWorld(0,0)) which
    // does not match the cube terrain world. The result was huge invalid cells like
    // (2163,-10898) being fed into mining designation. When the real ray hits the
    // AWFCubeTerrainActor, take XY from WorldToTerrainCell(Hit.ImpactPoint) and
    // preserve the active mining layer Z. The legacy synthetic-plane path remains
    // intact below for the underground-clipped case and for non-cube terrain.
    if (bUseSyntheticLayerPlane)
    {
        // [CUBE-MINING-V2-UG-PLANE-RAYCAST] For underground Mine the cube terrain mesh sits
        // at surface level — its XY impact point stays fixed as the mouse moves across the
        // underground layer, causing OutVoxel XY to never change. Fix: intersect the mouse
        // ray with the active layer horizontal plane first and derive XY from that world point.
        // Falls through to the CubeMeshHit path only if the plane intersection fails.
        const bool bIsUndergroundMinePlane =
            ActiveToolMode == EBuildToolMode::Mine &&
            LayerView != nullptr &&
            LayerView->HasActiveVoxelLayer() &&
            LayerView->GetRelativeLayerOffset() < 0;

        if (bIsUndergroundMinePlane && FMath::Abs(RayDirection.Z) > KINDA_SMALL_NUMBER)
        {
            const float LayerWorldZ = LayerView->GetActiveLayerWorldZ();
            const float T = (LayerWorldZ - RayOrigin.Z) / RayDirection.Z;
            if (T >= 0.0f)
            {
                FVector PlaneHitWorld = RayOrigin + RayDirection * T;
                PlaneHitWorld.Z = LayerWorldZ;

                // [CUBE-MINING-V3-ACCESS] The underground plane hit must resolve to the
                // SAME per-voxel visual-grid space the surface MeshHit path produces (see
                // the WorldToGrid3D call ~line 908), because that is the space mining
                // designations, SelectedCells, IsVoxelOpen/CompletedMineVoxels, and the
                // connectivity anchors all live in.
                //
                // History of this block:
                //   * Original: floor((hit - VoxOrigin)/CellSize) — correct per-voxel scale
                //     but derived its own origin; produced off-map garbage on the synthetic
                //     plane (the .bak named CUBE-MINING-V2-UG-PLANE-RAYCAST).
                //   * Interim: WorldPositionToTileXY — resolved to ABSOLUTE WORLD-MAP TILES
                //     (3200uu each). That let the filter seed (tiles matched anchors) but
                //     folded all 32x32 voxels in a tile to one coordinate, so a whole drag
                //     collapsed to a single cell — you could only ever select 1 voxel.
                //   * Now: WorldToGrid3D for X/Y (true 200uu voxel resolution, same as the
                //     surface path) with Z overridden by the active layer. This is per-voxel
                //     AND in the anchor space, so seeding works and each voxel is distinct.
                UVoxelWorldManager* VM_UG =
                    PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;

                if (VM_UG)
                {
                    const FIntVector VisualGridCell = VM_UG->WorldToGrid3D(PlaneHitWorld);
                    const int32 ActiveZ = LayerView->GetActiveVoxelLayerZ();

                    OutVoxel = FIntVector(VisualGridCell.X, VisualGridCell.Y, ActiveZ);
                    OutVoxel = MakeSelectionKeyFromVoxel(OutVoxel);
                    OutTerrainZ = LayerWorldZ;
                    OutHitLocation = PlaneHitWorld;

                    UE_LOG(LogGridSelection, Log,
                        TEXT("[CUBE-MINING-V3-ACCESS] PlaneRaycast(Voxel) ActiveZ=%d Relative=%d HitWorld=(%.1f,%.1f,%.1f) OutVoxel=(%d,%d,%d)"),
                        ActiveZ,
                        LayerView->GetRelativeLayerOffset(),
                        PlaneHitWorld.X, PlaneHitWorld.Y, PlaneHitWorld.Z,
                        OutVoxel.X, OutVoxel.Y, OutVoxel.Z);

                    return true;
                }
                // VM unavailable — fall through to the CubeMeshHit path below,
                // which itself falls through to PlaneFallback.
            }
            // Plane intersection failed or VM unavailable — fall through to CubeMeshHit path.
        }

        FCollisionQueryParams CubeParams(SCENE_QUERY_STAT(GridSelectionRaycastCubeAudit), true);
        CubeParams.bReturnPhysicalMaterial = false;
        CubeParams.AddIgnoredActor(PC->GetPawn());
        if (GridOverlay)
        {
            CubeParams.AddIgnoredActor(GridOverlay);
        }

        FHitResult CubeHit;
        const bool bCubeHit = PC->GetWorld()->LineTraceSingleByChannel(
            CubeHit,
            RayOrigin,
            RayOrigin + (RayDirection * TraceDistance),
            ECC_Visibility,
            CubeParams);

        AActor* CubeHitActor = bCubeHit ? CubeHit.GetActor() : nullptr;
        const bool bIsCubeTerrainHit =
            CubeHitActor != nullptr &&
            CubeHitActor->IsA(AWFCubeTerrainActor::StaticClass());

        if (bIsCubeTerrainHit)
        {
            // CUBE-TERRAIN-04W-STEP4: WorldToTerrainCell returns center-relative
            // cells (e.g., (-4, 3)) that fail claim validation (WorldTileID=-1).
            // Use the authoritative WorldMapSubsystem::WorldPositionToTileXY to
            // get absolute map tile coordinates. The center-relative result is
            // still computed and logged for diagnostics.
            UVoxelWorldManager* VM_Cube = PC->GetWorld()->GetSubsystem<UVoxelWorldManager>();
            FIntPoint RelativeTerrainCell(0, 0);
            if (VM_Cube)
            {
                RelativeTerrainCell = VM_Cube->WorldToTerrainCell(CubeHit.ImpactPoint);
            }

            int32 AbsoluteTileX = INDEX_NONE;
            int32 AbsoluteTileY = INDEX_NONE;
            bool bAbsoluteTileValid = false;
            if (UWorldMapSubsystem* WorldMap = PC->GetWorld()->GetSubsystem<UWorldMapSubsystem>())
            {
                bAbsoluteTileValid = WorldMap->WorldPositionToTileXY(
                    CubeHit.ImpactPoint, AbsoluteTileX, AbsoluteTileY);
            }

            OutHitLocation = CubeHit.ImpactPoint;
            const FIntVector VisualGridCell = VM_Cube
                ? VM_Cube->WorldToGrid3D(CubeHit.ImpactPoint)
                : FIntVector(
                    bAbsoluteTileValid ? AbsoluteTileX : RelativeTerrainCell.X,
                    bAbsoluteTileValid ? AbsoluteTileY : RelativeTerrainCell.Y,
                    LayerView->GetActiveVoxelLayerZ());
            OutVoxel = FIntVector(VisualGridCell.X, VisualGridCell.Y, LayerView->GetActiveVoxelLayerZ());
            OutVoxel = MakeSelectionKeyFromVoxel(OutVoxel);
            OutTerrainZ = (LayerView->GetRelativeLayerOffset() < 0)
                ? LayerView->GetActiveLayerWorldZ()
                : CubeHit.ImpactPoint.Z;

            UE_LOG(LogGridSelection, Log,
                TEXT("[CubeSelectionRayAudit] Mode=CubePrototype HitWorld=(%.1f,%.1f,%.1f) RelativeTerrainCell=(%d,%d) AbsoluteTile=(%d,%d) OutVoxel=(%d,%d,%d)"),
                CubeHit.ImpactPoint.X, CubeHit.ImpactPoint.Y, CubeHit.ImpactPoint.Z,
                RelativeTerrainCell.X, RelativeTerrainCell.Y,
                AbsoluteTileX, AbsoluteTileY,
                OutVoxel.X, OutVoxel.Y, OutVoxel.Z);

            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-01] MineHoverResolve Source=CubeMeshHit Hit=true Tile=(%d,%d) Z=%d World=(%.1f,%.1f,%.1f)"),
                OutVoxel.X, OutVoxel.Y, OutVoxel.Z,
                OutHitLocation.X, OutHitLocation.Y, OutHitLocation.Z);

            return true;
        }

        // CUBE-MINING-01-STEP1I: Surface plane fallback when cube terrain mesh hit fails.
        // At normal camera distance other geometry may occlude the cube terrain actor.
        // Project onto the active surface layer plane using WorldMapSubsystem for absolute tile XY.
        {
            float FallbackPlaneZ = 0.0f;
            bool bHasFallbackZ = false;

            if (LayerView->HasActiveVoxelLayer())
            {
                FallbackPlaneZ = LayerView->GetActiveLayerWorldZ();
                bHasFallbackZ = true;
            }
            else if (FMath::Abs(HoveredTerrainZ) > KINDA_SMALL_NUMBER)
            {
                FallbackPlaneZ = HoveredTerrainZ;
                bHasFallbackZ = true;
            }

            if (bHasFallbackZ && FMath::Abs(RayDirection.Z) > KINDA_SMALL_NUMBER)
            {
                const float T = (FallbackPlaneZ - RayOrigin.Z) / RayDirection.Z;
                if (T >= 0.0f)
                {
                    FVector ProjectedPoint = RayOrigin + (RayDirection * T);
                    ProjectedPoint.Z = FallbackPlaneZ;

                    int32 PlaneAbsTileX = INDEX_NONE;
                    int32 PlaneAbsTileY = INDEX_NONE;
                    bool bPlaneTileValid = false;
                    if (UWorldMapSubsystem* WorldMap_Plane = PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UWorldMapSubsystem>() : nullptr)
                    {
                        bPlaneTileValid = WorldMap_Plane->WorldPositionToTileXY(ProjectedPoint, PlaneAbsTileX, PlaneAbsTileY);
                    }

                    UVoxelWorldManager* VM_Plane = PC->GetWorld()
                        ? PC->GetWorld()->GetSubsystem<UVoxelWorldManager>()
                        : nullptr;

                    if (bPlaneTileValid || VM_Plane)
                    {
                        const int32 PlaneVoxelZ = LayerView->HasActiveVoxelLayer()
                            ? LayerView->GetActiveVoxelLayerZ()
                            : HoveredVoxel.Z;

                        const FIntVector PlaneGridCell = VM_Plane
                            ? VM_Plane->WorldToGrid3D(ProjectedPoint)
                            : FIntVector(PlaneAbsTileX, PlaneAbsTileY, PlaneVoxelZ);

                        OutHitLocation = ProjectedPoint;
                        OutVoxel = FIntVector(PlaneGridCell.X, PlaneGridCell.Y, PlaneVoxelZ);
                        OutVoxel = MakeSelectionKeyFromVoxel(OutVoxel);
                        OutTerrainZ = FallbackPlaneZ;

                        UE_LOG(LogGridSelection, Log,
                            TEXT("[CUBE-MINING-01] MineHoverResolve Source=PlaneFallback Hit=false Tile=(%d,%d) Z=%d World=(%.1f,%.1f,%.1f)"),
                            PlaneAbsTileX, PlaneAbsTileY, PlaneVoxelZ,
                            ProjectedPoint.X, ProjectedPoint.Y, ProjectedPoint.Z);

                        return true;
                    }
                }
            }
        }
    }

    // Underground mining must not depend on visible terrain ray hits.
    // Once the terrain is clipped away, the mouse ray may miss all geometry.
    // Project onto the active layer plane instead so the grid remains usable underground.
    if (bUseSyntheticLayerPlane && LayerView->GetRelativeLayerOffset() < 0 && FMath::Abs(RayDirection.Z) > KINDA_SMALL_NUMBER)
    {
        const float PlaneZ = LayerView->GetActiveLayerWorldZ();
        const float T = (PlaneZ - RayOrigin.Z) / RayDirection.Z;
        if (T >= 0.0f)
        {
            OutHitLocation = RayOrigin + (RayDirection * T);
            OutHitLocation.Z = PlaneZ;

            // Apply origin-corrected floor conversion (same convention as MakeCellTransform).
            UVoxelWorldManager* VM_Layer = PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;
            if (VM_Layer)
            {
                const float SafeSize = FMath::Max(1.0f, GetCellSizeUU());
                // GetVoxelWorldOrigin() is private on UVoxelWorldManager.
                // Derive the corner origin from the public GridToWorld(0,0,0) center.
                const FVector VoxelZeroCenter = VM_Layer->GridToWorld(FIntPoint(0, 0), 0);
                const FVector VoxOrigin = VoxelZeroCenter - FVector(SafeSize * 0.5f, SafeSize * 0.5f, SafeSize * 0.5f);
                OutVoxel = FIntVector(
                    FMath::FloorToInt((OutHitLocation.X - VoxOrigin.X) / SafeSize),
                    FMath::FloorToInt((OutHitLocation.Y - VoxOrigin.Y) / SafeSize),
                    0);
            }
            else
            {
                OutVoxel = WorldToVoxel(OutHitLocation, GetCellSizeUU());
            }
            OutVoxel.Z = LayerView->GetActiveVoxelLayerZ();
            OutVoxel = MakeSelectionKeyFromVoxel(OutVoxel);
            OutTerrainZ = PlaneZ;
            return true;
        }
    }

    const FVector TraceStart = RayOrigin;
    const FVector TraceEnd = RayOrigin + (RayDirection * TraceDistance);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(GridSelectionRaycast), true);
    Params.bReturnPhysicalMaterial = false;
    Params.AddIgnoredActor(PC->GetPawn());

    // Critical: do not allow mouse raycasts to hit the overlay itself.
    if (GridOverlay)
    {
        Params.AddIgnoredActor(GridOverlay);
    }

    FHitResult Hit;
    const bool bHit = PC->GetWorld()->LineTraceSingleByChannel(
        Hit,
        TraceStart,
        TraceEnd,
        ECC_Visibility,
        Params);

    if (!bHit)
    {
        return false;
    }

    OutHitLocation = Hit.ImpactPoint;

    if (UWorld* World = PC->GetWorld())
    {
        if (UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
        {
            // Use floor-based (corner-aligned) XY conversion to be consistent with
            // MakeCellTransform which uses (VoxelCoord + 0.5) * GetCellSizeUU().
            // VM->WorldToGrid3D uses round-to-nearest (+0.5*RVS bias) which diverges
            // from the corner convention near cell edges — causing the hover tile to
            // appear in the wrong cell for mouse positions in the lower 50% of a voxel.
            const float SafeSize = FMath::Max(1.0f, GetCellSizeUU());
            // GetVoxelWorldOrigin() is private on UVoxelWorldManager.
            // Derive the corner origin from the public GridToWorld(0,0,0) center.
            const FVector VoxelZeroCenter = VM->GridToWorld(FIntPoint(0, 0), 0);
            const FVector VoxOrigin = VoxelZeroCenter - FVector(SafeSize * 0.5f, SafeSize * 0.5f, SafeSize * 0.5f);
            FIntVector HitVoxelFloor(
                FMath::FloorToInt((Hit.ImpactPoint.X - VoxOrigin.X) / SafeSize),
                FMath::FloorToInt((Hit.ImpactPoint.Y - VoxOrigin.Y) / SafeSize),
                FMath::FloorToInt((Hit.ImpactPoint.Z - VoxOrigin.Z) / SafeSize));

            int32 SurfaceZ = HitVoxelFloor.Z;
            VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(HitVoxelFloor.X, HitVoxelFloor.Y), SurfaceZ);

            int32 TargetZ = SurfaceZ + ActiveToolLayerOffset;

            if (LayerView && (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig))
            {
                if (!LayerView->HasActiveVoxelLayer())
                {
                    LayerView->InitializeFromSurfaceVoxelZ(SurfaceZ, ActiveToolLayerOffset);
                }
                TargetZ = LayerView->GetActiveVoxelLayerZ();
            }

            OutVoxel = FIntVector(HitVoxelFloor.X, HitVoxelFloor.Y, TargetZ);
            OutVoxel = MakeSelectionKeyFromVoxel(OutVoxel);

            // Surface mode: use the actual raycast hit Z so the overlay sits exactly
            // on the SDF terrain surface mesh without clipping.
            // Elevated/layer mode: compute from voxel top face (center + half-voxel).
            if (ActiveToolLayerOffset == 0)
            {
                OutTerrainZ = Hit.ImpactPoint.Z;
            }
            else
            {
                if (ActiveToolMode == EBuildToolMode::Mine)
                {
                    const FVector TargetWorld = VM->GridToWorld(FIntPoint(OutVoxel.X, OutVoxel.Y), OutVoxel.Z);
                    OutTerrainZ = TargetWorld.Z + GetCellSizeUU() * 0.5f;
                }
                else
                {
                    const int32 CenterVX = OutVoxel.X * MacroCellVoxelSpan + (MacroCellVoxelSpan / 2);
                    const int32 CenterVY = OutVoxel.Y * MacroCellVoxelSpan + (MacroCellVoxelSpan / 2);
                    const FVector TargetWorld = VM->GridToWorld(FIntPoint(CenterVX, CenterVY), OutVoxel.Z);
                    OutTerrainZ = TargetWorld.Z + GetCellSizeUU() * 0.5f;
                }
            }

            UE_LOG(LogGridSelection, Verbose,
                TEXT("[MouseGrid] HitWorld=(%.1f,%.1f,%.1f) VoxOrigin=(%.1f,%.1f,%.1f) FloorVoxel=(%d,%d,%d) SurfaceZ=%d TargetZ=%d OutTerrainZ=%.1f"),
                Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z,
                VoxOrigin.X, VoxOrigin.Y, VoxOrigin.Z,
                HitVoxelFloor.X, HitVoxelFloor.Y, HitVoxelFloor.Z,
                SurfaceZ, TargetZ, OutTerrainZ);

            return true;
        }
    }

    const FIntVector SurfaceVoxel = WorldToVoxel(Hit.ImpactPoint, GetCellSizeUU());
    int32 TargetZ = SurfaceVoxel.Z + ActiveToolLayerOffset;

    if (LayerView && (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig))
    {
        if (!LayerView->HasActiveVoxelLayer())
        {
            LayerView->InitializeFromSurfaceVoxelZ(SurfaceVoxel.Z, ActiveToolLayerOffset);
        }
        TargetZ = LayerView->GetActiveVoxelLayerZ();
    }

    OutVoxel = FIntVector(SurfaceVoxel.X, SurfaceVoxel.Y, TargetZ);
    OutVoxel = MakeSelectionKeyFromVoxel(OutVoxel);
    // Use the actual terrain hit Z (not a voxel-quantized Z) so the overlay
    // does not clip into the SDF terrain surface in the VM-unavailable fallback path.
    OutTerrainZ = (LayerView && LayerView->HasActiveVoxelLayer())
        ? LayerView->GetActiveLayerWorldZ()
        : Hit.ImpactPoint.Z;
    return true;
}

float UGridSelectionSubsystem::GetMacroCellSizeUU() const
{
    return GetCellSizeUU() * static_cast<float>(MacroCellVoxelSpan);
}

FIntVector UGridSelectionSubsystem::MakeSelectionKeyFromVoxel(const FIntVector& Voxel) const
{
    return ActiveToolMode == EBuildToolMode::Mine ? Voxel : VoxelToMacroIndex(Voxel);
}

FIntVector UGridSelectionSubsystem::MacroIndexToCenterVoxel(const FIntVector& MacroIndex) const
{
    return FIntVector(
        MacroIndex.X * MacroCellVoxelSpan + (MacroCellVoxelSpan / 2),
        MacroIndex.Y * MacroCellVoxelSpan + (MacroCellVoxelSpan / 2),
        MacroIndex.Z);
}


void UGridSelectionSubsystem::GetSelectedVoxelsForRender(TArray<FIntVector>& OutVoxels) const
{
    OutVoxels.Reset();
    OutVoxels.Reserve(SelectedCells.Num());

    for (const TPair<FIntVector, float>& Pair : SelectedCells)
    {
        OutVoxels.Add(Pair.Key);
    }
}

void UGridSelectionSubsystem::ExpandMacroAndFilter(
    const FIntVector& MacroIndex, int32 VoxelZ, float CellTerrainZ,
    TMap<FIntVector, float>& OutValidVoxels,
    int32& OutValidCount, int32& OutTotalCount) const
{
    TArray<FIntVector> Voxels;
    ExpandMacroIndexToVoxels(MacroIndex, VoxelZ, Voxels);
    OutTotalCount = Voxels.Num(); // 64

    TMap<FIntVector, float> Candidate;
    Candidate.Reserve(Voxels.Num());
    for (const FIntVector& V : Voxels)
    {
        Candidate.Add(V, CellTerrainZ);
    }

    // Reuse the existing per-voxel filter (depth window, open/liquid, rule-3/4
    // connectivity). Candidates here are REAL voxels, satisfying the guardrail that
    // macro keys never enter voxel-space functions.
    TMap<FIntVector, float> Accepted;
    FilterMineSelectionByConnectivity(Candidate, Accepted);

    OutValidVoxels.Reset();
    for (const TPair<FIntVector, float>& P : Accepted)
    {
        if (IsVoxelForbiddenLiquid(P.Key)) continue;
        if (IsVoxelAboveColumnSurface(P.Key)) continue;
        OutValidVoxels.Add(P.Key, P.Value);
    }
    OutValidCount = OutValidVoxels.Num();
}

UGridSelectionSubsystem::EMacroValidity
UGridSelectionSubsystem::SummarizeMacroValidity(int32 ValidCount, int32 TotalCount) const
{
    if (ValidCount <= 0)          return EMacroValidity::NoneValid;
    if (ValidCount >= TotalCount) return EMacroValidity::AllValid;
    return EMacroValidity::Partial;
}

float UGridSelectionSubsystem::GetCellSizeUU() const
{
    // CUBE-VOXEL-SCALE: live cell size from the voxel authority so selection aligns with
    // the configured grid (200uu by default). Falls back to the constant during early
    // init before the manager exists.
    if (const UWorld* World = GetWorld())
    {
        if (const UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
        {
            const float VS = VM->GetRenderVoxelSize();
            if (VS > KINDA_SMALL_NUMBER)
            {
                return VS;
            }
        }
    }
    return CellSizeUUFallback;
}

FIntVector UGridSelectionSubsystem::WorldToVoxel(const FVector& WorldPos, float VoxelSize)
{
    const float SafeVoxelSize = FMath::Max(1.0f, VoxelSize);
    return FIntVector(
        FMath::FloorToInt(WorldPos.X / SafeVoxelSize),
        FMath::FloorToInt(WorldPos.Y / SafeVoxelSize),
        FMath::FloorToInt(WorldPos.Z / SafeVoxelSize));
}

int32 UGridSelectionSubsystem::GetStoredLayerOffsetForTool(EBuildToolMode ToolMode) const
{
    switch (ToolMode)
    {
    case EBuildToolMode::Place:
        return BuildLayerOffset;

    case EBuildToolMode::Mine:
    case EBuildToolMode::Dig:
        return MineLayerOffset;

    case EBuildToolMode::Farm:
        return 0;

    default:
        return 0;
    }
}

void UGridSelectionSubsystem::StoreLayerOffsetForTool(EBuildToolMode ToolMode, int32 NewOffset)
{
    switch (ToolMode)
    {
    case EBuildToolMode::Place:
        BuildLayerOffset = FMath::Max(0, NewOffset);
        ActiveToolLayerOffset = BuildLayerOffset;
        break;

    case EBuildToolMode::Mine:
    case EBuildToolMode::Dig:
        MineLayerOffset = FMath::Min(0, NewOffset);
        ActiveToolLayerOffset = MineLayerOffset;
        break;

    case EBuildToolMode::Farm:
        ActiveToolLayerOffset = 0;
        break;

    default:
        ActiveToolLayerOffset = 0;
        break;
    }
}

void UGridSelectionSubsystem::AdjustActiveToolLayer(int32 Delta)
{
    if (Delta == 0)
    {
        return;
    }

    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        if (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig)
        {
            if (!LayerView->HasActiveVoxelLayer() && bHasValidHover)
            {
                LayerView->InitializeFromSurfaceVoxelZ(HoveredVoxel.Z, 0);
            }

            // Capture pre-move relative offset so we can detect a slice change below.
            const int32 OldRelative = LayerView->GetRelativeLayerOffset();
            const int32 OldVoxelZ = LayerView->HasActiveVoxelLayer()
                ? LayerView->GetActiveVoxelLayerZ()
                : LayerView->GetSurfaceVoxelLayerZ();

            LayerView->MoveLayerBy(Delta);

            // Mine/dig depth is limited to MaxMinePreviewDepthBelowSurface layers below the
            // captured surface. This is a preview/planning window — actual mining depth is
            // governed by MiningSubsystem connectivity once designations complete.
            if (LayerView->HasActiveVoxelLayer())
            {
                const int32 SurfaceZ = LayerView->GetSurfaceVoxelLayerZ();
                const int32 RequestedZ = LayerView->GetActiveVoxelLayerZ();
                const int32 MinAllowedZ = ComputeMinimumAllowedMineVoxelZ();
                const int32 ClampedZ = FMath::Clamp(RequestedZ, MinAllowedZ, SurfaceZ);
                if (ClampedZ != RequestedZ)
                {
                    LayerView->SetActiveVoxelLayerZ(ClampedZ);
                }
                UE_LOG(LogGridSelection, Log,
                    TEXT("[CUBE-MINING-V2-UG] DepthClamp SurfaceZ=%d RequestedZ=%d MinAllowedZ=%d FinalZ=%d MaxDepth=%d"),
                    SurfaceZ, RequestedZ, MinAllowedZ, ClampedZ, MaxMinePreviewDepthBelowSurface);
            }

            ActiveToolLayerOffset = LayerView->GetRelativeLayerOffset();
            MineLayerOffset = ActiveToolLayerOffset;

            if (bHasValidHover)
            {
                HoveredVoxel.Z = LayerView->GetActiveVoxelLayerZ();
                HoveredTerrainZ = LayerView->GetActiveLayerWorldZ();
            }

            // [CUBE-MINING-V2-UG] Clear active (temporary) selection when Mine moves to a
            // different slice. Confirmed designation cells live in the overlay's confirmed
            // layer and are NOT touched here.
            if (ActiveToolMode == EBuildToolMode::Mine)
            {
                const int32 NewRelative = LayerView->GetRelativeLayerOffset();
                const int32 NewVoxelZ = LayerView->GetActiveVoxelLayerZ();
                if (NewRelative != OldRelative)
                {
                    // CUBE-MINING-V3-ANCHOR-PERSIST: snapshot the cells painted on the
                    // layer we are LEAVING into the persistent anchor set BEFORE wiping
                    // SelectedCells. This is what makes the "paint many layers, confirm
                    // once at the end" workflow work: an un-confirmed drag on Z=31
                    // becomes a valid Above-anchor for Z=30 the instant the player
                    // descends, with no designation round-trip required. Confirmed cells
                    // already feed the same set via CommitSelectedCellsToConfirmedOverlay,
                    // so both workflows converge on one anchor source.
                    int32 SnapshotAdded = 0;
                    // Only deposit anchors when descending (NewRelative deeper than Old).
                    // Anchors propagate strictly downward; scrolling back UP must not mint
                    // new permanent anchors from whatever happens to be selected.
                    //
                    // CUBE-MINING-V3-FLOOD: additionally require that the layer being LEFT
                    // is already underground (OldRelative < 0). The surface layer (offset 0)
                    // is the exposed column top, not a mine target; snapshotting its cells
                    // is what put Z=18 surface cells into the plan and caused the
                    // RejectAboveSurface invalid skips at confirm. Surface cells still seed
                    // the first underground layer via the surface-ceiling rule.
                    const bool bDescending = NewRelative < OldRelative;
                    const bool bLeavingUnderground = OldRelative < 0;
                    if (bDescending && bLeavingUnderground)
                    {
                        for (const TPair<FIntVector, float>& Pair : SelectedCells)
                        {
                            bool bAlready = false;
                            PersistentMineAnchorVoxels.Add(Pair.Key, &bAlready);
                            if (!bAlready)
                            {
                                ++SnapshotAdded;
                            }
                        }
                    }

                    SelectedCells.Reset();
                    DragPreviewCells.Reset();
                    PreviewMacroTints.Reset();
                    LastPreviewEndVoxel = FIntVector(INT32_MIN);
                    PushSelectionToOverlay();
                    PushPreviewToOverlay();
                    UE_LOG(LogGridSelection, Log,
                        TEXT("[CUBE-MINING-V2-UG] LayerSelectionReset OldRelative=%d NewRelative=%d OldZ=%d NewZ=%d SnapshotedAnchors=%d TotalPersistentAnchors=%d"),
                        OldRelative, NewRelative, OldVoxelZ, NewVoxelZ,
                        SnapshotAdded, PersistentMineAnchorVoxels.Num());
                }
            }
        }
        else
        {
            StoreLayerOffsetForTool(ActiveToolMode, ActiveToolLayerOffset + Delta);
        }
    }
    else
    {
        StoreLayerOffsetForTool(ActiveToolMode, ActiveToolLayerOffset + Delta);
    }

    // Mine uses true 200uu voxel tiles at every layer.
    // Must be set BEFORE RebuildStaticBaseGridFromHover so base-grid cells use the right scale.
    if (GridOverlay && ActiveToolMode == EBuildToolMode::Mine)
    {
        GridOverlay->SetCellSizeUU(GetCellSizeUU());
    }

    // Rebuild the static base grid to the new target layer when possible.
    RebuildStaticBaseGridFromHover();
    LastPreviewEndVoxel = FIntVector(INT32_MIN);

    if ((ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig))
    {
        if (ULayerViewSubsystem* LVLog = GetLayerViewSubsystem())
        {
            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-V2-UG] LayerMove ActiveZ=%d SurfaceZ=%d Relative=%d"),
                LVLog->GetActiveVoxelLayerZ(),
                LVLog->GetSurfaceVoxelLayerZ(),
                LVLog->GetRelativeLayerOffset());
        }
    }

    // CUBE-MINING-V3-ANCHOR-PERSIST: refresh the faint "planned above me" ghost so
    // the player can see where the shaft/tunnel on the layer above sits and therefore
    // where they may continue digging downward on this new layer.
    PushAboveLayerAnchorGhost();

    UE_LOG(LogGridSelection, Log,
        TEXT("Adjusted active tool layer. Tool=%d Layer=%d ActiveVoxelZ=%d"),
        static_cast<int32>(ActiveToolMode),
        ActiveToolLayerOffset,
        bHasValidHover ? HoveredVoxel.Z : 0);
}

void UGridSelectionSubsystem::ResetActiveToolLayer()
{
    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        if (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig)
        {
            LayerView->ResetToSurfaceLayer();
            ActiveToolLayerOffset = LayerView->GetRelativeLayerOffset();
            MineLayerOffset = ActiveToolLayerOffset;

            if (bHasValidHover)
            {
                HoveredVoxel.Z = LayerView->GetActiveVoxelLayerZ();
                HoveredTerrainZ = LayerView->GetActiveLayerWorldZ();
            }
        }
        else
        {
            StoreLayerOffsetForTool(ActiveToolMode, 0);
        }
    }
    else
    {
        StoreLayerOffsetForTool(ActiveToolMode, 0);
    }

    // Resetting to surface: mine remains on true 200uu voxel tiles.
    // Must be set BEFORE RebuildStaticBaseGridFromHover.
    if (GridOverlay && ActiveToolMode == EBuildToolMode::Mine)
    {
        GridOverlay->SetCellSizeUU(GetCellSizeUU());
    }

    RebuildStaticBaseGridFromHover();
    LastPreviewEndVoxel = FIntVector(INT32_MIN);

    UE_LOG(LogGridSelection, Log,
        TEXT("Reset active tool layer. Tool=%d Layer=%d"),
        static_cast<int32>(ActiveToolMode),
        ActiveToolLayerOffset);
}


void UGridSelectionSubsystem::AdjustActiveViewLayer(int32 Delta)
{
    if (Delta == 0)
    {
        return;
    }

    ActiveViewLayerOffset += Delta;

    UE_LOG(LogGridSelection, Log,
        TEXT("Adjusted active view layer. ViewLayer=%d"),
        ActiveViewLayerOffset);
}

void UGridSelectionSubsystem::ResetActiveViewLayer()
{
    ActiveViewLayerOffset = 0;

    UE_LOG(LogGridSelection, Log,
        TEXT("Reset active view layer. ViewLayer=0"));
}


int32 UGridSelectionSubsystem::GetActiveVoxelLayerZ() const
{
    if (const ULayerViewSubsystem* LayerView = GetWorld() ? GetWorld()->GetSubsystem<ULayerViewSubsystem>() : nullptr)
    {
        if (LayerView->HasActiveVoxelLayer())
        {
            return LayerView->GetActiveVoxelLayerZ();
        }
    }

    return bHasValidHover ? HoveredVoxel.Z : 0;
}

ULayerViewSubsystem* UGridSelectionSubsystem::GetLayerViewSubsystem() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<ULayerViewSubsystem>() : nullptr;
}

void UGridSelectionSubsystem::RebuildStaticBaseGridFromHover()
{
    if (!bStaticBaseGridEnabled)
    {
        if (GridOverlay)
        {
            GridOverlay->ClearBaseGrid();
        }
        UE_LOG(LogGridSelection, Verbose, TEXT("Static base grid disabled; skipping rebuild."));
        return;
    }

    if (!GridOverlay || !bHasValidHover)
    {
        return;
    }

    ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    const bool bIsMineLikeTool = (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig);

    // Surface mining should stay one-to-one with the actual terrain surface.
    // Underground mining should lock to the selected voxel Z slice.
    const bool bIsUndergroundLayer =
        LayerView &&
        LayerView->HasActiveVoxelLayer() &&
        bIsMineLikeTool &&
        LayerView->GetRelativeLayerOffset() < 0;

    const bool bUseFlatActiveLayer = bIsUndergroundLayer;

    UWorld* World = GetWorld();
    UVoxelWorldManager* VM = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;

    const int32 EffectiveGridRadius = bIsMineLikeTool ? MineStaticBaseGridRadius : SurfaceStaticBaseGridRadius;

    TMap<FIntVector, float> BaseCells;
    BaseCells.Reserve((EffectiveGridRadius * 2 + 1) * (EffectiveGridRadius * 2 + 1));

    const int32 CenterZ = bUseFlatActiveLayer ? LayerView->GetActiveVoxelLayerZ() : HoveredVoxel.Z;
    // In mine/dig mode the editor grid represents the active voxel layer plane.
    // Do not offset to CenterZ + 1 here: doing so draws the underground helper
    // slab one full voxel above the active cutaway layer, which makes the grid/floor
    // appear at the surface instead of at the layer the player is viewing.
    const float CenterWorldZ = bUseFlatActiveLayer
        ? LayerView->VoxelLayerToWorldZ(CenterZ) + 2.0f
        : HoveredTerrainZ;

    if (IsSurfaceMineMode())
    {
        // Overlay is at 200uu voxel scale for surface Mine: build the base grid in
        // real voxel space centered on the hovered voxel.
        const FIntVector CenterVoxel(HoveredVoxel.X, HoveredVoxel.Y, CenterZ);

        for (int32 X = CenterVoxel.X - EffectiveGridRadius; X <= CenterVoxel.X + EffectiveGridRadius; ++X)
        {
            for (int32 Y = CenterVoxel.Y - EffectiveGridRadius; Y <= CenterVoxel.Y + EffectiveGridRadius; ++Y)
            {
                int32 SurfaceZ = CenterZ;
                float CellWorldZ = HoveredTerrainZ;
                if (VM)
                {
                    VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(X, Y), SurfaceZ);
                    const FVector TargetWorld = VM->GridToWorld(FIntPoint(X, Y), SurfaceZ);
                    CellWorldZ = TargetWorld.Z + GetCellSizeUU() * 0.5f;
                }
                BaseCells.Add(FIntVector(X, Y, SurfaceZ), CellWorldZ);
            }
        }
    }
    else
    {
        for (int32 X = HoveredVoxel.X - EffectiveGridRadius; X <= HoveredVoxel.X + EffectiveGridRadius; ++X)
        {
            for (int32 Y = HoveredVoxel.Y - EffectiveGridRadius; Y <= HoveredVoxel.Y + EffectiveGridRadius; ++Y)
            {
                if (bUseFlatActiveLayer)
                {
                    BaseCells.Add(FIntVector(X, Y, CenterZ), CenterWorldZ);
                    continue;
                }

                int32 SurfaceZ = HoveredVoxel.Z - ActiveToolLayerOffset;
                float CellWorldZ = HoveredTerrainZ;

                if (VM)
                {
                    VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(X, Y), SurfaceZ);
                    const int32 TargetZ = SurfaceZ + ActiveToolLayerOffset;
                    const FVector TargetWorld = VM->GridToWorld(FIntPoint(X, Y), TargetZ);
                    CellWorldZ = TargetWorld.Z + GetCellSizeUU() * 0.5f;
                    BaseCells.Add(FIntVector(X, Y, TargetZ), CellWorldZ);
                }
                else
                {
                    BaseCells.Add(FIntVector(X, Y, CenterZ), CellWorldZ);
                }
            }
        }
    }

    const bool bUndergroundWhiteGrid = bUseFlatActiveLayer;
    GridOverlay->SetUseUndergroundWhiteGrid(bUndergroundWhiteGrid);
    GridOverlay->SetBaseGridCells(BaseCells);

    if (bIsMineLikeTool)
    {
        if (const ULayerViewSubsystem* LVLog = GetLayerViewSubsystem())
        {
            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-V2-UG] UndergroundGrid Enabled=%s ActiveZ=%d Relative=%d"),
                bUndergroundWhiteGrid ? TEXT("true") : TEXT("false"),
                LVLog->GetActiveVoxelLayerZ(),
                LVLog->GetRelativeLayerOffset());
        }
    }

    // The dedicated MiningLayerRenderer owns the solid underground slice.
    // The overlay grid is only a light targeting guide.
    GridOverlay->ClearUndergroundFloorBlocks();

    if (ActiveToolMode == EBuildToolMode::Mine && !bUseFlatActiveLayer)
    {
        const int32 VisibleToolLayer = LayerView ? LayerView->GetActiveVoxelLayerZ() : CenterZ;
        UE_LOG(LogGridSelection, Log,
            TEXT("[CUBE-MINING-V2] MineGridLayerLock SurfaceZ=%d VisibleToolLayer=%d HoverZ=%d"),
            CenterZ,
            VisibleToolLayer,
            HoveredVoxel.Z);
    }

    UE_LOG(LogGridSelection, Log,
        TEXT("Static base grid rebuilt from hover. Center=(%d,%d,%d) ToolLayer=%d Radius=%d Cells=%d FlatLayer=%s UndergroundWhite=%s"),
        HoveredVoxel.X,
        HoveredVoxel.Y,
        CenterZ,
        CenterZ,
        EffectiveGridRadius,
        BaseCells.Num(),
        bUseFlatActiveLayer ? TEXT("true") : TEXT("false"),
        bUndergroundWhiteGrid ? TEXT("true") : TEXT("false"));
}

void UGridSelectionSubsystem::RebuildDragPreview()
{
    DragPreviewCells.Reset();
    PreviewMacroTints.Reset();

    if (!bDraggingSelection || !bHasValidHover)
    {
        return;
    }

    static constexpr int32 MaxSurfaceMineSelectionCells = 256 * MacroCellVoxelSpan * MacroCellVoxelSpan;

    const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    const bool bSurfaceMineMVP = ActiveToolMode == EBuildToolMode::Mine &&
        ((LayerView && LayerView->HasActiveVoxelLayer())
            ? LayerView->GetRelativeLayerOffset() == 0
            : ActiveToolLayerOffset == 0);

    auto BuildRectangleOrClampMine = [&](const FIntVector& Start, const FIntVector& End)
        {
            const int64 DeltaX = static_cast<int64>(Start.X) - static_cast<int64>(End.X);
            const int64 DeltaY = static_cast<int64>(Start.Y) - static_cast<int64>(End.Y);
            const int64 RequestedCells = (FMath::Abs(DeltaX) + 1) * (FMath::Abs(DeltaY) + 1);

            if (bSurfaceMineMVP && RequestedCells > MaxSurfaceMineSelectionCells)
            {
                UE_LOG(LogGridSelection, Warning,
                    TEXT("[CUBE-MINING-01] Mine selection clamped. Requested=%lld Max=%d Start=(%d,%d,%d) End=(%d,%d,%d)"),
                    static_cast<long long>(RequestedCells),
                    MaxSurfaceMineSelectionCells,
                    Start.X,
                    Start.Y,
                    Start.Z,
                    End.X,
                    End.Y,
                    End.Z);
                DragPreviewCells.Add(HoveredVoxel, HoveredTerrainZ);
                return;
            }

            BuildRectangleCells(Start, End, HoveredTerrainZ, DragPreviewCells);
        };

    switch (ActiveShape)
    {
    case EGridSelectionShape::Rectangle:
        BuildRectangleOrClampMine(DragStartVoxel, HoveredVoxel);
        break;

    case EGridSelectionShape::Single:
        DragPreviewCells.Add(HoveredVoxel, HoveredTerrainZ);
        break;

    case EGridSelectionShape::Paint:
        // First pass: paint behaves as accumulated rectangle preview only after follow-up patch.
        DragPreviewCells.Add(HoveredVoxel, HoveredTerrainZ);
        break;

    case EGridSelectionShape::Line:
        // First pass: line behaves as rectangle when dragged horizontally/vertically.
        BuildRectangleOrClampMine(DragStartVoxel, HoveredVoxel);
        break;

    default:
        DragPreviewCells.Add(HoveredVoxel, HoveredTerrainZ);
        break;
    }

    if (ActiveToolMode == EBuildToolMode::Mine && IsActiveLayerUndergroundMine())
    {
        const int32 RequestedVoxels = DragPreviewCells.Num();

        TMap<FIntVector, float> Accepted;
        FilterMineSelectionByConnectivity(DragPreviewCells, Accepted);

        TMap<FIntVector, float> Filtered;
        Filtered.Reserve(Accepted.Num());
        for (const TPair<FIntVector, float>& Pair : Accepted)
        {
            if (IsVoxelForbiddenLiquid(Pair.Key)) continue;
            if (IsVoxelAboveColumnSurface(Pair.Key)) continue;
            Filtered.Add(Pair.Key, Pair.Value);
        }

        DragPreviewCells = MoveTemp(Filtered);

        UE_LOG(LogGridSelection, Log,
            TEXT("[CUBE-MINING-VOXEL] Preview RequestedVoxels=%d AcceptedVoxels=%d Rejected=%d"),
            RequestedVoxels, DragPreviewCells.Num(), RequestedVoxels - DragPreviewCells.Num());
    }

    LastPreviewEndVoxel = HoveredVoxel;
}

void UGridSelectionSubsystem::BuildRectangleCells(
    const FIntVector& Start,
    const FIntVector& End,
    float TerrainZ,
    TMap<FIntVector, float>& OutCells) const
{
    const int32 MinX = FMath::Min(Start.X, End.X);
    const int32 MaxX = FMath::Max(Start.X, End.X);
    const int32 MinY = FMath::Min(Start.Y, End.Y);
    const int32 MaxY = FMath::Max(Start.Y, End.Y);

    const int32 Z = Start.Z;

    const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    const bool bSurfaceMineSelection = ActiveToolMode == EBuildToolMode::Mine &&
        ((LayerView && LayerView->HasActiveVoxelLayer())
            ? LayerView->GetRelativeLayerOffset() == 0
            : ActiveToolLayerOffset == 0);

    UWorld* World = bSurfaceMineSelection ? GetWorld() : nullptr;
    UVoxelWorldManager* VM = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;
    UWorldMapSubsystem* WorldMap = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;

    auto ResolveSurfaceWorldZForVisualCell = [&](const FIntVector& VisualCell, float& OutSurfaceWorldZ, int32& OutSurfaceVoxelZ) -> bool
        {
            if (!VM)
            {
                return false;
            }

            FIntPoint TerrainTile(VisualCell.X, VisualCell.Y);
            if (WorldMap)
            {
                const FVector VisualCellWorld = VM->GridToWorld(FIntPoint(VisualCell.X, VisualCell.Y), VisualCell.Z);
                int32 TileX = INDEX_NONE;
                int32 TileY = INDEX_NONE;
                if (!WorldMap->WorldPositionToTileXY(VisualCellWorld, TileX, TileY))
                {
                    return false;
                }
                TerrainTile = FIntPoint(TileX, TileY);
            }

            int32 SurfaceVoxelZ = VisualCell.Z;
            if (!VM->TryGetTerrainSurfaceVoxelZ(TerrainTile, SurfaceVoxelZ))
            {
                return false;
            }

            FVector SurfaceWorldLocation = FVector::ZeroVector;
            if (VM->TryGetSurfaceWorldLocation(TerrainTile, SurfaceWorldLocation, 0.0f))
            {
                OutSurfaceWorldZ = SurfaceWorldLocation.Z;
            }
            else
            {
                // Z only: keep overlay X/Y in visual-grid space, but derive height from the resolved surface voxel.
                OutSurfaceWorldZ = VM->GridToWorld(FIntPoint(VisualCell.X, VisualCell.Y), SurfaceVoxelZ).Z;
            }

            OutSurfaceVoxelZ = SurfaceVoxelZ;
            return true;
        };

    // For surface mine, lock all cells to the visible grid layer Z so selected-cell
    // voxel coords match the base grid exactly. Per-cell terrain height (CellTerrainZ)
    // is still resolved for accurate visual rendering but does not affect the voxel key.
    const int32 VisibleGridLayerZ = (bSurfaceMineSelection && LayerView) ? LayerView->GetActiveVoxelLayerZ() : Z;

    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            FIntVector Cell(X, Y, bSurfaceMineSelection ? VisibleGridLayerZ : Z);
            float CellTerrainZ = TerrainZ;

            if (bSurfaceMineSelection)
            {
                // CUBE-MINING-V2-PRESERVE-MESH-HIT-Z:
                // RaycastMouseToVoxel now resolves surface Mine hover/drag TerrainZ from
                // the real visibility hit when possible. Do not replace that value with
                // the voxel-derived surface height here; doing so pushes selected/preview
                // Mine tiles back under the visible CubePrototype terrain surface.
                // Keep Cell.Z locked to the visible grid layer, but keep CellTerrainZ
                // equal to the input TerrainZ used by the hover/base grid.
                float VoxelSurfaceWorldZ = TerrainZ;
                int32 SurfaceVoxelZ = VisibleGridLayerZ;
                const bool bResolvedVoxelSurfaceZ = ResolveSurfaceWorldZForVisualCell(Cell, VoxelSurfaceWorldZ, SurfaceVoxelZ);

                UE_LOG(LogGridSelection, Log,
                    TEXT("[CUBE-MINING-V2] MinePreserveMeshHitZ Cell=(%d,%d,%d) InputTerrainZ=%.1f VoxelSurfaceWorldZ=%.1f UsedTerrainZ=%.1f Resolved=%s SurfaceVoxelZ=%d VisibleToolLayer=%d"),
                    Cell.X, Cell.Y, Cell.Z,
                    TerrainZ,
                    VoxelSurfaceWorldZ,
                    CellTerrainZ,
                    bResolvedVoxelSurfaceZ ? TEXT("true") : TEXT("false"),
                    SurfaceVoxelZ,
                    VisibleGridLayerZ);

                UE_LOG(LogGridSelection, Log,
                    TEXT("[CUBE-MINING-V2] MineGridLayerCell Cell=(%d,%d,%d) VisibleToolLayer=%d"),
                    Cell.X, Cell.Y, Cell.Z,
                    VisibleGridLayerZ);
            }

            OutCells.Add(Cell, CellTerrainZ);
        }
    }
}

bool UGridSelectionSubsystem::IsMineLikeTool() const
{
    return ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig;
}

bool UGridSelectionSubsystem::IsActiveLayerUndergroundMine() const
{
    if (ActiveToolMode != EBuildToolMode::Mine)
    {
        return false;
    }

    if (const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        if (LayerView->HasActiveVoxelLayer())
        {
            return LayerView->GetRelativeLayerOffset() < 0;
        }
    }

    // Fallback when LayerView has no active voxel layer yet.
    return ActiveToolLayerOffset < 0;
}

bool UGridSelectionSubsystem::IsSurfaceMineMode() const
{
    if (ActiveToolMode != EBuildToolMode::Mine) return false;
    if (const ULayerViewSubsystem* LV = GetLayerViewSubsystem())
        return !LV->HasActiveVoxelLayer() || LV->GetRelativeLayerOffset() == 0;
    return ActiveToolLayerOffset == 0;
}

bool UGridSelectionSubsystem::IsAdjacent6(const FIntVector& A, const FIntVector& B) const
{
    const FIntVector Delta = A - B;
    return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z) == 1;
}

bool UGridSelectionSubsystem::IsVoxelOpen(const FIntVector& Voxel) const
{
    // Locally-tracked completed mine cells count as open immediately (rule 7),
    // even if the terrain backlog hasn't re-meshed the removal yet.
    if (CompletedMineVoxels.Contains(Voxel))
    {
        return true;
    }

    const UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;
    if (!VM)
    {
        // No terrain authority available — fall back to designation-only openness.
        return false;
    }

    // CUBE-MINING-V3-ACCESS FIX:
    // GetBlockTypeAt is NOT a reliable solid/air oracle on un-modified terrain in
    // this build — it returns an invalid (air) tag for pristine solid stone that
    // hasn't been queried/meshed yet. Using it made EVERY underground candidate
    // look "already open", so the access filter rejected all of them (InWindow=0).
    //
    // The reliable signal is the per-column surface height. A voxel is OPEN if it
    // sits strictly above the surface of its column (it's air/sky), and SOLID if it
    // is at or below the surface — unless a completed mine designation removed it,
    // which is already handled above via CompletedMineVoxels.
    //
    // The column surface MUST be resolved through the same world->tile chain the
    // surface MeshHit path uses (GridToWorld -> WorldPositionToTileXY -> preferred
    // branch of TryGetTerrainSurfaceVoxelZ). Passing the raw per-voxel XY would hit
    // that function's grid->tile fallback, whose divisor (cube-runtime BlocksPerTile)
    // is wrong, resolving a different column and giving a bogus surface height.
    int32 SurfaceZ = 0;
    bool bResolved = false;

    const FVector ColumnWorld = VM->GridToWorld(FIntPoint(Voxel.X, Voxel.Y), Voxel.Z);
    int32 AbsTileX = INDEX_NONE;
    int32 AbsTileY = INDEX_NONE;
    if (UWorldMapSubsystem* WorldMap_Open =
        GetWorld() ? GetWorld()->GetSubsystem<UWorldMapSubsystem>() : nullptr)
    {
        if (WorldMap_Open->WorldPositionToTileXY(ColumnWorld, AbsTileX, AbsTileY))
        {
            bResolved = VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(AbsTileX, AbsTileY), SurfaceZ);
        }
    }
    if (!bResolved)
    {
        bResolved = VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(Voxel.X, Voxel.Y), SurfaceZ);
    }

    if (bResolved)
    {
        return Voxel.Z > SurfaceZ;
    }

    // No surface data for this column — treat as solid (not open) so we never
    // wrongly seed access from an unknown column.
    return false;
}

bool UGridSelectionSubsystem::IsVoxelAboveColumnSurface(const FIntVector& Voxel) const
{
    const UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;
    if (!VM)
    {
        // No terrain authority — cannot prove it's above surface, so don't exclude it;
        // the backend remains the final guard.
        return false;
    }

    // Same world->tile->surface resolution chain as IsVoxelOpen and the confirm
    // resolver. Raw per-voxel XY would hit the grid->tile fallback with the wrong
    // divisor and resolve a different column, so go through WorldPositionToTileXY.
    int32 SurfaceZ = 0;
    bool bResolved = false;

    const FVector ColumnWorld = VM->GridToWorld(FIntPoint(Voxel.X, Voxel.Y), Voxel.Z);
    int32 AbsTileX = INDEX_NONE;
    int32 AbsTileY = INDEX_NONE;
    if (UWorldMapSubsystem* WorldMap =
        GetWorld() ? GetWorld()->GetSubsystem<UWorldMapSubsystem>() : nullptr)
    {
        if (WorldMap->WorldPositionToTileXY(ColumnWorld, AbsTileX, AbsTileY))
        {
            bResolved = VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(AbsTileX, AbsTileY), SurfaceZ);
        }
    }
    if (!bResolved)
    {
        bResolved = VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(Voxel.X, Voxel.Y), SurfaceZ);
    }

    if (bResolved)
    {
        // Strictly above the column surface = air/sky = not a valid mine target.
        return Voxel.Z > SurfaceZ;
    }

    // No surface data — don't exclude; let the backend decide.
    return false;
}

bool UGridSelectionSubsystem::IsVoxelForbiddenLiquid(const FIntVector& Voxel) const
{
    // Forward hook. No voxel block-type tags exist for lava/water yet:
    // terrain blocks are Dirt/Stone/Gravel/Clay/Sand/Bedrock, and water is a
    // surface-actor concern rather than a voxel block type. When liquid block
    // tags are added to WorldForgeTags.h and returned by GetBlockTypeAt, match
    // them here, e.g.:
    //
    //   const UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;
    //   if (!VM) return false;
    //   const FGameplayTag BlockType = VM->GetBlockTypeAt(Voxel);
    //   return BlockType.MatchesTag(TAG_Terrain_Lava) || BlockType.MatchesTag(TAG_Terrain_Water);
    //
    // Until then this is a no-op so no valid stone/dirt cell is wrongly rejected.
    (void)Voxel;
    return false;
}


void UGridSelectionSubsystem::BuildMineConnectivityAnchors(TSet<FIntVector>& OutAnchors, bool bCompletedOnly) const
{
    OutAnchors.Reset();

    // Current active selection is always a valid planning anchor.
    for (const TPair<FIntVector, float>& Pair : SelectedCells)
    {
        OutAnchors.Add(Pair.Key);
    }

    // CUBE-MINING-V3-ANCHOR-PERSIST: confirmed cells from PREVIOUS layers survive
    // the SelectedCells.Reset() that AdjustActiveToolLayer performs on every slice
    // change. Without this, a cell the player confirmed at Z=N vanished from the
    // anchor set the instant they scrolled to Z=N-1, so the seed check's
    // UGAnchors.Contains(Above) returned false and AbovePlanned was always false —
    // the deeper layer could never connect. This set is the authoritative carry
    // of planned access across layers and does not depend on the MiningSubsystem
    // designation round-trip or on ActiveMineSettlementID being correct.
    if (!bCompletedOnly)
    {
        OutAnchors.Append(PersistentMineAnchorVoxels);
    }

    // CUBE-MINING-V3-ANCHOR-PERSIST: resolve a usable settlement ID even when
    // SetActiveMineSettlementID was never called. The confirm path (CreateMiningDesignation)
    // trusts USettlementManager::GetPlayerSettlementID(), so we fall back to the same
    // source here instead of bailing with an empty designation contribution.
    int32 SettlementID = ActiveMineSettlementID;
    if (SettlementID == INDEX_NONE)
    {
        if (const UWorld* World = GetWorld())
        {
            if (const USettlementManager* SettlementMgr = World->GetSubsystem<USettlementManager>())
            {
                SettlementID = SettlementMgr->GetPlayerSettlementID();
            }
        }
    }

    if (SettlementID == INDEX_NONE)
    {
        // No designations available; SelectedCells + PersistentMineAnchorVoxels already
        // populated above are sufficient to anchor freshly-planned layers.
        return;
    }

    const UMiningSubsystem* Mining = GetWorld() ? GetWorld()->GetSubsystem<UMiningSubsystem>() : nullptr;
    if (!Mining)
    {
        return;
    }

    TArray<FMiningDesignation> Designations;
    Mining->GetAllDesignations(SettlementID, Designations);

    int32 DesignationAnchorCount = 0;
    for (const FMiningDesignation& Designation : Designations)
    {
        if (Designation.State == EMiningDesignationState::Cancelled ||
            Designation.State == EMiningDesignationState::Failed)
        {
            continue;
        }

        if (bCompletedOnly && Designation.State != EMiningDesignationState::Complete)
        {
            continue;
        }

        // CUBE-MINING-V3-ANCHOR-PERSIST: register BOTH coordinates. ResolveMineTarget
        // may snap the clicked Z to solid terrain before storing GridPosition, while
        // the seed check computes Above in the player's clicked/grid space. Adding
        // OriginalGridPosition (the pre-snap clicked coord) as well guarantees the
        // Above lookup matches whichever space the candidate came from.
        OutAnchors.Add(Designation.GridPosition);
        OutAnchors.Add(Designation.OriginalGridPosition);
        ++DesignationAnchorCount;
    }

    UE_LOG(LogGridSelection, Log,
        TEXT("[CUBE-MINING-V3-ACCESS] AnchorBuild SettlementID=%d CompletedOnly=%s Designations=%d Persistent=%d Selected=%d TotalAnchors=%d"),
        SettlementID,
        bCompletedOnly ? TEXT("true") : TEXT("false"),
        DesignationAnchorCount,
        bCompletedOnly ? 0 : PersistentMineAnchorVoxels.Num(),
        SelectedCells.Num(),
        OutAnchors.Num());
}

void UGridSelectionSubsystem::PushAboveLayerAnchorGhost() const
{
    // CUBE-MINING-V3-ANCHOR-PERSIST
    // Render the planned cells on the layer directly ABOVE the current one as faint
    // ghost markers, projected onto the current layer's Z so they read as "you can
    // continue digging here." Only meaningful underground in Mine mode.
    if (!GridOverlay || ActiveToolMode != EBuildToolMode::Mine)
    {
        return;
    }

    const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    if (!LayerView || !LayerView->HasActiveVoxelLayer() || LayerView->GetRelativeLayerOffset() >= 0)
    {
        // At/above surface there is no "layer above" shaft to advertise.
        return;
    }

    // Do not stomp a live selection: once the player starts painting this layer,
    // PushSelectionToOverlay owns the mining-volume channel. The ghost only fills the
    // gap between arriving on a fresh layer and beginning to paint it.
    if (SelectedCells.Num() > 0 || DragPreviewCells.Num() > 0)
    {
        return;
    }

    const int32 ActiveZ = LayerView->GetActiveVoxelLayerZ();
    const int32 AboveZ = ActiveZ + 1;

    TSet<FIntVector> Anchors;
    BuildMineConnectivityAnchors(Anchors, /*bCompletedOnly*/ false);

    // CUBE-MINING-V3-FLOOD: project EVERY anchor above the active layer (the whole
    // planned vertical stack), not only the immediate layer above, so the player can
    // see the full shaft/tunnel plan they can dig toward. Each above-anchor's XY is
    // projected down to the current slice Z so it renders on the layer being viewed.
    // De-duplicated by XY; when a column has planned cells on several layers we keep the
    // NEAREST one (smallest distance above) and tint by that distance — closer planned
    // layers render brighter/stronger, deeper ones fade — so the stack reads as depth.
    TMap<FIntPoint, int32> NearestDistByXY; // XY -> smallest (AnchorZ - ActiveZ)
    TMap<int32, int32> PerLayerCount;       // AnchorZ -> count, for diagnostics

    for (const FIntVector& Anchor : Anchors)
    {
        if (Anchor.Z <= ActiveZ)
        {
            continue; // only layers ABOVE the one being viewed
        }

        PerLayerCount.FindOrAdd(Anchor.Z)++;

        const FIntPoint XY(Anchor.X, Anchor.Y);
        const int32 Dist = Anchor.Z - ActiveZ;
        if (int32* Existing = NearestDistByXY.Find(XY))
        {
            *Existing = FMath::Min(*Existing, Dist);
        }
        else
        {
            NearestDistByXY.Add(XY, Dist);
        }
    }

    // Base ghost tint comes from the overlay's mining-designation color via the MID;
    // here we only vary ALPHA by distance so the cube fades with depth without fighting
    // the configured hue. Distance 1 (immediately above) = strongest; each layer deeper
    // drops alpha by a fixed step down to a floor so even deep layers stay faintly visible.
    constexpr float NearAlpha = 0.55f;
    constexpr float AlphaStep = 0.12f;
    constexpr float AlphaFloor = 0.12f;

    TArray<FIntVector> GhostCells;
    TArray<FLinearColor> GhostTints;
    GhostCells.Reserve(NearestDistByXY.Num());
    GhostTints.Reserve(NearestDistByXY.Num());

    for (const TPair<FIntPoint, int32>& Pair : NearestDistByXY)
    {
        const int32 Dist = Pair.Value; // >= 1
        const float Alpha = FMath::Max(AlphaFloor, NearAlpha - AlphaStep * static_cast<float>(Dist - 1));

        // White-ish ghost; the underground white-grid material + per-instance alpha give
        // the faint outline look. Hue is left to the configured ghost material.
        GhostCells.Add(FIntVector(Pair.Key.X, Pair.Key.Y, ActiveZ));
        GhostTints.Add(FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
    }

    const_cast<UGridSelectionSubsystem*>(this)->SetMiningVolumePreviewCellsTinted(GhostCells, GhostTints);

    UE_LOG(LogGridSelection, Log,
        TEXT("[CUBE-MINING-V3-FLOOD] AboveLayerGhost ActiveZ=%d AboveZ=%d StackLayers=%d GhostCells=%d"),
        ActiveZ, AboveZ, PerLayerCount.Num(), GhostCells.Num());

    for (const TPair<int32, int32>& LayerPair : PerLayerCount)
    {
        UE_LOG(LogGridSelection, Verbose,
            TEXT("[CUBE-MINING-V3-FLOOD] AboveLayerGhostLayer AnchorZ=%d DistanceAbove=%d Cells=%d"),
            LayerPair.Key, LayerPair.Key - ActiveZ, LayerPair.Value);
    }
}

int32 UGridSelectionSubsystem::ComputeMinimumAllowedMineVoxelZ() const
{
    const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    if (!LayerView || !LayerView->HasActiveVoxelLayer())
    {
        return MIN_int32 / 4;
    }

    const int32 SurfaceZ = LayerView->GetSurfaceVoxelLayerZ();
    int32 DeepestOpenZ = SurfaceZ;

    // Only completed/removed mine blocks extend the playable depth window.
    // Pending designations count for connectivity, but they do not unlock deeper
    // mining until colonists actually remove those voxels.
    //
    // CUBE-MINING-V3-ANCHOR-PERSIST: resolve a settlement even if SetActiveMineSettlementID
    // was never called, mirroring the fallback in BuildMineConnectivityAnchors so the
    // depth window and the anchor set always agree on which designations exist.
    int32 DepthSettlementID = ActiveMineSettlementID;
    if (DepthSettlementID == INDEX_NONE)
    {
        if (const USettlementManager* SettlementMgr = GetWorld() ? GetWorld()->GetSubsystem<USettlementManager>() : nullptr)
        {
            DepthSettlementID = SettlementMgr->GetPlayerSettlementID();
        }
    }

    if (DepthSettlementID != INDEX_NONE)
    {
        const UMiningSubsystem* Mining = GetWorld() ? GetWorld()->GetSubsystem<UMiningSubsystem>() : nullptr;
        if (Mining)
        {
            TArray<FMiningDesignation> Designations;
            Mining->GetAllDesignations(DepthSettlementID, Designations);
            CompletedMineVoxels.Reset();
            for (const FMiningDesignation& Designation : Designations)
            {
                if (Designation.State == EMiningDesignationState::Complete)
                {
                    DeepestOpenZ = FMath::Min(DeepestOpenZ, Designation.GridPosition.Z);
                    // rule 7: completed cells are open and become future access anchors.
                    CompletedMineVoxels.Add(Designation.GridPosition);
                }
            }
        }
    }

    // Planning depth is capped to 3 Z levels below the currently exposed/open layer.
    // SurfaceZ is the original exposed layer when mining starts; completed mine blocks
    // can lower DeepestOpenZ, which then unlocks the next -3 planning slice.
    return FMath::Max(0, DeepestOpenZ - MaxMinePreviewDepthBelowSurface);
}

void UGridSelectionSubsystem::FilterMineSelectionByConnectivity(
    const TMap<FIntVector, float>& CandidateCells,
    TMap<FIntVector, float>& OutAcceptedCells) const
{
    OutAcceptedCells.Reset();

    if (!IsMineLikeTool() || CandidateCells.Num() == 0)
    {
        OutAcceptedCells = CandidateCells;
        return;
    }

    const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    const bool bSurfaceMineLayer = (LayerView && LayerView->HasActiveVoxelLayer())
        ? LayerView->GetRelativeLayerOffset() == 0
        : ActiveToolLayerOffset == 0;

    if (bSurfaceMineLayer)
    {
        // CUBE-MINING-01 MVP: surface mine selection is simple Farm/Build-style
        // X/Y painting. Keep connectivity filtering for future underground layers.
        OutAcceptedCells = CandidateCells;
        return;
    }

    // [CUBE-MINING-V3-ACCESS] Underground Mine no longer bypasses connectivity.
    // It now runs the SAME flood-fill the surface path uses, but with access gating:
    //   - candidates that are forbidden liquids or already open are dropped
    //   - a candidate is a valid entrance (rule 3) if the cell directly ABOVE it is open
    //   - same-Z completed/designated tunnel cells are anchors (rule 4)
    //   - disconnected cells fall out of the BFS and are logged (rules 5 & 6)
    // The old "accept everything in the depth window" block is intentionally gone.
    const ULayerViewSubsystem* UGLayerView = LayerView;
    if (ActiveToolMode == EBuildToolMode::Mine &&
        UGLayerView && UGLayerView->HasActiveVoxelLayer() &&
        UGLayerView->GetRelativeLayerOffset() < 0)
    {
        const int32 ActiveZ = UGLayerView->GetActiveVoxelLayerZ();
        const int32 SurfZ = UGLayerView->GetSurfaceVoxelLayerZ();
        const int32 MinAllowedZ = ComputeMinimumAllowedMineVoxelZ();
        const int32 RelOffset = UGLayerView->GetRelativeLayerOffset();

        // Terrain authority for per-column surface lookups (rule-3 ceiling test).
        const UVoxelWorldManager* VM =
            GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;

        // 1) Reduce candidates to mineable cells inside the depth window.
        TSet<FIntVector> UGCandidateSet;
        UGCandidateSet.Reserve(CandidateCells.Num());
        for (const TPair<FIntVector, float>& Pair : CandidateCells)
        {
            const FIntVector& V = Pair.Key;
            if (V.Z > SurfZ || V.Z < MinAllowedZ) continue;   // rule 2 depth window
            if (IsVoxelOpen(V))                    continue;   // already air/mined — nothing to mine
            if (IsVoxelForbiddenLiquid(V))         continue;   // rule: no lava/water
            UGCandidateSet.Add(V);
        }

        // 2) Build anchors: same-Z tunnel/designation cells (rule 4) + rule-3 entrances.
        TSet<FIntVector> UGAnchors;
        BuildMineConnectivityAnchors(UGAnchors, /*bCompletedOnly*/ false);

        TArray<FIntVector> UGFrontier;
        TSet<FIntVector> UGAccepted;

        for (const FIntVector& Candidate : UGCandidateSet)
        {
            bool bSeed = false;

            // Rule 3: a candidate is a valid entrance if it can be reached from above.
            // The cell directly above is reachable when it is OPEN — either already
            // mined out (CompletedMineVoxels), or it lies above this column's surface
            // (sky), OR it IS the column's surface block itself. That last case is the
            // crucial one for the FIRST dig down: the un-mined surface is the natural
            // ceiling/access for the topmost underground layer (Z = SurfaceZ - 1), so
            // a player can always start a shaft straight down from the surface without
            // needing a pre-existing opening. Without this, the surface block reads as
            // solid (Z == SurfaceZ is not "> SurfaceZ"), nothing above is open, and the
            // first layer down can never be seeded — which is exactly the "can't select
            // one layer down" bug.
            const FIntVector Above(Candidate.X, Candidate.Y, Candidate.Z + 1);

            // [CUBE-MINING-V3-ACCESS] Resolve this column's terrain surface through the
            // SAME proven chain the surface MeshHit path uses, NOT the per-voxel overload
            // of TryGetTerrainSurfaceVoxelZ. Passing the raw per-voxel XY (e.g. -785,-431)
            // sends that function into its grid->tile *fallback*, which divides by the cube
            // render runtime's BlocksPerTile (=8) instead of the true voxels-per-world-tile
            // (=32) and offsets by the wrong grid basis. That resolved the WRONG column, so
            // ColumnSurfaceZ never matched the real SurfZ and rule 3 could never seed —
            // every underground candidate was rejected (Accepted=0).
            //
            // Correct chain (mirrors surface path):
            //   per-voxel cell --GridToWorld--> world --WorldPositionToTileXY--> absolute
            //   tile --TryGetTerrainSurfaceVoxelZ(PREFERRED branch)--> SurfaceZ.
            int32 ColumnSurfaceZ = 0;
            bool bHasColumnSurface = false;
            if (VM)
            {
                const FVector ColumnWorld =
                    VM->GridToWorld(FIntPoint(Candidate.X, Candidate.Y), Candidate.Z);

                int32 AbsTileX = INDEX_NONE;
                int32 AbsTileY = INDEX_NONE;
                UWorldMapSubsystem* WorldMap_Col =
                    GetWorld() ? GetWorld()->GetSubsystem<UWorldMapSubsystem>() : nullptr;

                if (WorldMap_Col &&
                    WorldMap_Col->WorldPositionToTileXY(ColumnWorld, AbsTileX, AbsTileY))
                {
                    // Preferred branch: absolute world-map tile XY resolves directly.
                    bHasColumnSurface =
                        VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(AbsTileX, AbsTileY), ColumnSurfaceZ);
                }

                // Fallback: if the world-map lookup is unavailable, try the per-voxel
                // overload anyway (it has its own internal fallback). Better than nothing.
                if (!bHasColumnSurface)
                {
                    bHasColumnSurface =
                        VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(Candidate.X, Candidate.Y), ColumnSurfaceZ);
                }
            }

            const bool bAboveOpen = IsVoxelOpen(Above);
            const bool bSurfaceCeiling = (bHasColumnSurface && Above.Z >= ColumnSurfaceZ);

            // [CUBE-MINING-V3-ACCESS] Option B (queued planning): a pending or confirmed
            // mine designation directly above this candidate counts as future access, so
            // the player can plan the layer below before colonists have physically removed
            // the layer above. UGAnchors (built with bCompletedOnly=false) already contains
            // pending/confirmed designations AND the live SelectedCells, so "is Above an
            // anchor?" is exactly the queued-access test. This is what lets a freshly
            // designated Z=17 immediately unlock planning at Z=16, instead of forcing the
            // player to wait for the miner to finish (Option A / strict simulation).
            const bool bAbovePlanned = UGAnchors.Contains(Above);

            // [CUBE-MINING-V3-FLOOD] Omnidirectional anchor connection.
            // Design decision (confirmed): surface-adjacent layers seed freely
            // (surface ceiling / open sky above), but DEEPER layers seed if the
            // candidate is face-adjacent to ANY anchor in the whole plan — above,
            // below, or sideways — not only the cell directly above. This is what
            // lets a lower layer be wider than the one above it and lets the player
            // branch tunnels in 3D, instead of being forced into a strict vertical
            // shaft. The anchor set (UGAnchors) already aggregates persistent carry +
            // live selection + backend designations across every layer.
            bool bAnchorConnected = bAbovePlanned; // direct-above is the cheap common case
            if (!bAnchorConnected)
            {
                for (const FIntVector& Anchor : UGAnchors)
                {
                    if (Candidate == Anchor || IsAdjacent6(Candidate, Anchor))
                    {
                        bAnchorConnected = true;
                        break;
                    }
                }
            }

            // Surface-adjacent free seed (unchanged) OR connected-to-plan (loosened).
            if (bAboveOpen || bSurfaceCeiling || bAnchorConnected)
            {
                bSeed = true;
            }

            // [CUBE-MINING-V3-ACCESS] AnchorCheck: break down WHY a candidate did/didn't
            // seed so a failing case can be traced to the exact missing source without
            // guessing. AnchorConnected now reflects the full omnidirectional test.
            UE_LOG(LogGridSelection, Verbose,
                TEXT("[CUBE-MINING-V3-ACCESS] AnchorCheck Cand=(%d,%d,%d) Above=(%d,%d,%d) InPersistent=%s InSelected=%s AbovePlanned=%s AnchorConnected=%s"),
                Candidate.X, Candidate.Y, Candidate.Z,
                Above.X, Above.Y, Above.Z,
                PersistentMineAnchorVoxels.Contains(Above) ? TEXT("true") : TEXT("false"),
                SelectedCells.Contains(Above) ? TEXT("true") : TEXT("false"),
                bAbovePlanned ? TEXT("true") : TEXT("false"),
                bAnchorConnected ? TEXT("true") : TEXT("false"));

            // [CUBE-MINING-V3-ACCESS] Seed diagnostic. ColumnSurfaceZ should equal SurfZ
            // for an unmined column; if not, world->tile resolution is the next suspect.
            UE_LOG(LogGridSelection, Log,
                TEXT("[CUBE-MINING-V3-ACCESS] UGSeedCheck Target=(%d,%d,%d) Above=(%d,%d,%d) AboveOpen=%s HasColumnSurface=%s ColumnSurfaceZ=%d SurfZ=%d SurfaceCeiling=%s AbovePlanned=%s AnchorConnected=%s Seed=%s"),
                Candidate.X, Candidate.Y, Candidate.Z,
                Above.X, Above.Y, Above.Z,
                bAboveOpen ? TEXT("true") : TEXT("false"),
                bHasColumnSurface ? TEXT("true") : TEXT("false"),
                ColumnSurfaceZ, SurfZ,
                bSurfaceCeiling ? TEXT("true") : TEXT("false"),
                bAbovePlanned ? TEXT("true") : TEXT("false"),
                bAnchorConnected ? TEXT("true") : TEXT("false"),
                bSeed ? TEXT("true") : TEXT("false"));

            if (bSeed)
            {
                UGFrontier.Add(Candidate);
                UGAccepted.Add(Candidate);
            }
        }

        // 3) Flood-fill outward across the connected candidate set (rule 6).
        int32 UGRead = 0;
        while (UGRead < UGFrontier.Num())
        {
            const FIntVector Cur = UGFrontier[UGRead++];
            const FIntVector Neighbors[6] =
            {
                FIntVector(Cur.X + 1, Cur.Y,     Cur.Z),
                FIntVector(Cur.X - 1, Cur.Y,     Cur.Z),
                FIntVector(Cur.X,     Cur.Y + 1, Cur.Z),
                FIntVector(Cur.X,     Cur.Y - 1, Cur.Z),
                FIntVector(Cur.X,     Cur.Y,     Cur.Z + 1),
                FIntVector(Cur.X,     Cur.Y,     Cur.Z - 1)
            };
            for (const FIntVector& N : Neighbors)
            {
                if (UGCandidateSet.Contains(N) && !UGAccepted.Contains(N))
                {
                    UGAccepted.Add(N);
                    UGFrontier.Add(N);
                }
            }
        }

        for (const FIntVector& A : UGAccepted)
        {
            if (const float* FoundZ = CandidateCells.Find(A))
            {
                OutAcceptedCells.Add(A, *FoundZ);
            }
        }

        UE_LOG(LogGridSelection, Log,
            TEXT("[CUBE-MINING-V3-ACCESS] UGFilter Candidate=%d InWindow=%d Accepted=%d Rejected=%d ActiveZ=%d Relative=%d MinAllowedZ=%d SurfZ=%d"),
            CandidateCells.Num(), UGCandidateSet.Num(), OutAcceptedCells.Num(),
            UGCandidateSet.Num() - OutAcceptedCells.Num(),
            ActiveZ, RelOffset, MinAllowedZ, SurfZ);
        return;
    }

    const int32 SurfaceZ = (LayerView && LayerView->HasActiveVoxelLayer())
        ? LayerView->GetSurfaceVoxelLayerZ()
        : MAX_int32 / 4;
    const int32 MinAllowedZ = ComputeMinimumAllowedMineVoxelZ();

    TSet<FIntVector> CandidateSet;
    CandidateSet.Reserve(CandidateCells.Num());
    for (const TPair<FIntVector, float>& Pair : CandidateCells)
    {
        // Never allow mining above terrain surface, and do not allow targets more
        // than two layers below the current completed/connected mine path.
        if (Pair.Key.Z <= SurfaceZ && Pair.Key.Z >= MinAllowedZ)
        {
            CandidateSet.Add(Pair.Key);
        }
    }

    if (CandidateSet.Num() == 0)
    {
        return;
    }

    TSet<FIntVector> Anchors;
    BuildMineConnectivityAnchors(Anchors, /*bCompletedOnly*/ false);

    TArray<FIntVector> Frontier;
    TSet<FIntVector> AcceptedSet;

    if (Anchors.Num() == 0)
    {
        // First mine selection can start anywhere within the valid depth window.
        // Prefer the drag start if it is inside the candidate set.
        const FIntVector Seed = CandidateSet.Contains(DragStartVoxel)
            ? DragStartVoxel
            : *CandidateSet.CreateConstIterator();
        Frontier.Add(Seed);
        AcceptedSet.Add(Seed);
    }
    else
    {
        // New mine cells must connect to an existing selected/designated/completed
        // mine cell on one of the six voxel faces. From that first contact point,
        // the drag preview can grow as far as the candidate cells stay face-connected.
        for (const FIntVector& Candidate : CandidateSet)
        {
            bool bConnectedToAnchor = false;
            for (const FIntVector& Anchor : Anchors)
            {
                if (Candidate == Anchor || IsAdjacent6(Candidate, Anchor))
                {
                    bConnectedToAnchor = true;
                    break;
                }
            }

            if (bConnectedToAnchor)
            {
                Frontier.Add(Candidate);
                AcceptedSet.Add(Candidate);
            }
        }
    }

    int32 ReadIndex = 0;
    while (ReadIndex < Frontier.Num())
    {
        const FIntVector Current = Frontier[ReadIndex++];
        const FIntVector Neighbors[6] =
        {
            FIntVector(Current.X + 1, Current.Y,     Current.Z),
            FIntVector(Current.X - 1, Current.Y,     Current.Z),
            FIntVector(Current.X,     Current.Y + 1, Current.Z),
            FIntVector(Current.X,     Current.Y - 1, Current.Z),
            FIntVector(Current.X,     Current.Y,     Current.Z + 1),
            FIntVector(Current.X,     Current.Y,     Current.Z - 1)
        };

        for (const FIntVector& Neighbor : Neighbors)
        {
            if (CandidateSet.Contains(Neighbor) && !AcceptedSet.Contains(Neighbor))
            {
                AcceptedSet.Add(Neighbor);
                Frontier.Add(Neighbor);
            }
        }
    }

    for (const FIntVector& Accepted : AcceptedSet)
    {
        if (const float* FoundZ = CandidateCells.Find(Accepted))
        {
            OutAcceptedCells.Add(Accepted, *FoundZ);
        }
    }
}