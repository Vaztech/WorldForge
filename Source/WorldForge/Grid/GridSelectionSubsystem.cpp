// Source/WorldForge/Grid/GridSelectionSubsystem.cpp

#include "Grid/GridSelectionSubsystem.h"
#include "Grid/LayerViewSubsystem.h"

#include "Player/TileGridOverlayActor.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Voxel/VoxelWorldManager.h"
#include "Mining/MiningSubsystem.h"

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
    LastPreviewEndVoxel = FIntVector(INT32_MIN);

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

    if (bCommitPreview)
    {
        TMap<FIntVector, float> CellsToCommit;
        if (IsMineLikeTool())
        {
            FilterMineSelectionByConnectivity(DragPreviewCells, CellsToCommit);
        }
        else
        {
            CellsToCommit = DragPreviewCells;
        }

        for (const TPair<FIntVector, float>& Pair : CellsToCommit)
        {
            if (bToggleSelectedCellsOnReselect && SelectedCells.Contains(Pair.Key))
            {
                SelectedCells.Remove(Pair.Key);
            }
            else
            {
                SelectedCells.FindOrAdd(Pair.Key) = Pair.Value;
            }
        }
        PushSelectionToOverlay();
    }

    const int32 CommittedCount = DragPreviewCells.Num();
    DragPreviewCells.Reset();
    LastPreviewEndVoxel = FIntVector(INT32_MIN);
    PushPreviewToOverlay();

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

    if (SelectedCells.Contains(HoveredVoxel))
    {
        if (bToggleSelectedCellsOnReselect)
        {
            SelectedCells.Remove(HoveredVoxel);
            if (GridOverlay)
            {
                GridOverlay->RemoveSelectedCell(HoveredVoxel);
            }
        }
        else
        {
            SelectedCells.FindOrAdd(HoveredVoxel) = HoveredTerrainZ;
        }
    }
    else
    {
        if (IsMineLikeTool())
        {
            TMap<FIntVector, float> Candidate;
            Candidate.Add(HoveredVoxel, HoveredTerrainZ);
            TMap<FIntVector, float> Accepted;
            FilterMineSelectionByConnectivity(Candidate, Accepted);
            if (Accepted.Num() == 0)
            {
                UE_LOG(LogGridSelection, Verbose, TEXT("Mine selection rejected; cell is not connected to current selection. Hover=(%d,%d,%d)"),
                    HoveredVoxel.X, HoveredVoxel.Y, HoveredVoxel.Z);
                return false;
            }
        }

        SelectedCells.Add(HoveredVoxel, HoveredTerrainZ);
        if (GridOverlay)
        {
            GridOverlay->AddSelectedCell(HoveredVoxel, HoveredTerrainZ);
        }
    }

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

void UGridSelectionSubsystem::ClearSelectedCells()
{
    SelectedCells.Reset();
    if (GridOverlay)
    {
        GridOverlay->ClearSelection();
        GridOverlay->ClearMiningVolumeCells();
    }
}

void UGridSelectionSubsystem::RetainSelectedCells(const TSet<FIntVector>& AllowedVoxels)
{
    TArray<FIntVector> CellsToRemove;

    for (const TPair<FIntVector, float>& Pair : SelectedCells)
    {
        if (!AllowedVoxels.Contains(Pair.Key))
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
            TerrainZ = WorldPos.Z;
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

void UGridSelectionSubsystem::SetMiningVolumePreviewCells(const TArray<FIntVector>& VoxelCoords)
{
    if (GridOverlay)
    {
        // Surface mine selections use flat marker tiles. Underground selections use
        // M_WF_MiningGhost voxel cubes. Do not rely on the static grid rebuild to
        // set this flag; selection preview must stay correct even if the grid is
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
        GridOverlay->SetCellSizeUU(CellSizeUU);
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
    MineStyle.TileColor = FLinearColor(1.0f, 0.45f, 0.05f, 0.35f);
    MineStyle.BorderColor = FLinearColor(1.0f, 0.65f, 0.2f, 0.90f);
    MineStyle.HoverColor = FLinearColor(1.0f, 0.8f, 0.35f, 0.65f);
    MineStyle.DragPreviewColor = FLinearColor(1.0f, 0.55f, 0.1f, 0.25f);
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
    }
}

void UGridSelectionSubsystem::PushHoverToOverlay()
{
    if (GridOverlay && bHasValidHover)
    {
        GridOverlay->SetHoveredCell(HoveredVoxel, HoveredTerrainZ);
    }
}

void UGridSelectionSubsystem::PushSelectionToOverlay()
{
    if (GridOverlay)
    {
        GridOverlay->SetSelectedCells(SelectedCells);
    }
}

void UGridSelectionSubsystem::PushPreviewToOverlay()
{
    if (GridOverlay)
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

    // Underground mining must not depend on visible terrain ray hits.
    // Once the terrain is clipped away, the mouse ray may miss all geometry.
    // Project onto the active layer plane instead so the grid remains usable underground.
    if (bUseSyntheticLayerPlane && FMath::Abs(RayDirection.Z) > KINDA_SMALL_NUMBER)
    {
        const float PlaneZ = LayerView->GetActiveLayerWorldZ();
        const float T = (PlaneZ - RayOrigin.Z) / RayDirection.Z;
        if (T >= 0.0f)
        {
            OutHitLocation = RayOrigin + (RayDirection * T);
            OutHitLocation.Z = PlaneZ;

            OutVoxel = WorldToVoxel(OutHitLocation, CellSizeUU);
            OutVoxel.Z = LayerView->GetActiveVoxelLayerZ();
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
            FIntVector SurfaceVoxel = VM->WorldToGrid3D(Hit.ImpactPoint);
            int32 SurfaceZ = SurfaceVoxel.Z;
            VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(SurfaceVoxel.X, SurfaceVoxel.Y), SurfaceZ);

            int32 TargetZ = SurfaceZ + ActiveToolLayerOffset;

            if (LayerView && (ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig))
            {
                if (!LayerView->HasActiveVoxelLayer())
                {
                    LayerView->InitializeFromSurfaceVoxelZ(SurfaceZ, ActiveToolLayerOffset);
                }
                TargetZ = LayerView->GetActiveVoxelLayerZ();
            }

            OutVoxel = FIntVector(SurfaceVoxel.X, SurfaceVoxel.Y, TargetZ);

            const FVector TargetWorld = VM->GridToWorld(FIntPoint(OutVoxel.X, OutVoxel.Y), OutVoxel.Z);
            OutTerrainZ = TargetWorld.Z;
            return true;
        }
    }

    const FIntVector SurfaceVoxel = WorldToVoxel(Hit.ImpactPoint, CellSizeUU);
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
    OutTerrainZ = (LayerView && LayerView->HasActiveVoxelLayer())
        ? LayerView->GetActiveLayerWorldZ()
        : static_cast<float>(OutVoxel.Z) * CellSizeUU;
    return true;
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

            LayerView->MoveLayerBy(Delta);

            // Mine/dig tools may move downward over time, but only within two
            // layers of the deepest completed connected mining path. If no
            // completed path exists yet, the first planning window is surface-2.
            if (LayerView->HasActiveVoxelLayer())
            {
                const int32 SurfaceZ = LayerView->GetSurfaceVoxelLayerZ();
                const int32 MinAllowedZ = ComputeMinimumAllowedMineVoxelZ();
                const int32 ClampedZ = FMath::Clamp(LayerView->GetActiveVoxelLayerZ(), MinAllowedZ, SurfaceZ);
                if (ClampedZ != LayerView->GetActiveVoxelLayerZ())
                {
                    LayerView->SetActiveVoxelLayerZ(ClampedZ);
                    UE_LOG(LogGridSelection, Log,
                        TEXT("Mine layer clamped. Requested beyond allowed range. SurfaceZ=%d MinAllowedZ=%d ActiveZ=%d"),
                        SurfaceZ, MinAllowedZ, ClampedZ);
                }
            }

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
            StoreLayerOffsetForTool(ActiveToolMode, ActiveToolLayerOffset + Delta);
        }
    }
    else
    {
        StoreLayerOffsetForTool(ActiveToolMode, ActiveToolLayerOffset + Delta);
    }

    // Rebuild the static base grid to the new target layer when possible.
    RebuildStaticBaseGridFromHover();
    LastPreviewEndVoxel = FIntVector(INT32_MIN);

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
                CellWorldZ = TargetWorld.Z;
                BaseCells.Add(FIntVector(X, Y, TargetZ), CellWorldZ);
            }
            else
            {
                BaseCells.Add(FIntVector(X, Y, CenterZ), CellWorldZ);
            }
        }
    }

    const bool bUndergroundWhiteGrid = bUseFlatActiveLayer;
    GridOverlay->SetUseUndergroundWhiteGrid(bUndergroundWhiteGrid);
    GridOverlay->SetBaseGridCells(BaseCells);

    // The dedicated MiningLayerRenderer owns the solid underground slice.
    // The overlay grid is only a light targeting guide.
    GridOverlay->ClearUndergroundFloorBlocks();

    UE_LOG(LogGridSelection, Log,
        TEXT("Static base grid rebuilt from hover. Center=(%d,%d,%d) ToolLayer=%d Radius=%d Cells=%d FlatLayer=%s UndergroundWhite=%s"),
        HoveredVoxel.X,
        HoveredVoxel.Y,
        CenterZ,
        ActiveToolLayerOffset,
        EffectiveGridRadius,
        BaseCells.Num(),
        bUseFlatActiveLayer ? TEXT("true") : TEXT("false"),
        bUndergroundWhiteGrid ? TEXT("true") : TEXT("false"));
}

void UGridSelectionSubsystem::RebuildDragPreview()
{
    DragPreviewCells.Reset();

    if (!bDraggingSelection || !bHasValidHover)
    {
        return;
    }

    switch (ActiveShape)
    {
    case EGridSelectionShape::Rectangle:
        BuildRectangleCells(DragStartVoxel, HoveredVoxel, HoveredTerrainZ, DragPreviewCells);
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
        BuildRectangleCells(DragStartVoxel, HoveredVoxel, HoveredTerrainZ, DragPreviewCells);
        break;

    default:
        DragPreviewCells.Add(HoveredVoxel, HoveredTerrainZ);
        break;
    }

    if (IsMineLikeTool())
    {
        TMap<FIntVector, float> Accepted;
        FilterMineSelectionByConnectivity(DragPreviewCells, Accepted);
        DragPreviewCells = Accepted;
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

    // Keep Z fixed to the drag-start layer for the first stable implementation.
    // Later we can add per-cell surface sampling for sloped terrain and Z-layer tools.
    const int32 Z = Start.Z;

    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            OutCells.Add(FIntVector(X, Y, Z), TerrainZ);
        }
    }
}

bool UGridSelectionSubsystem::IsMineLikeTool() const
{
    return ActiveToolMode == EBuildToolMode::Mine || ActiveToolMode == EBuildToolMode::Dig;
}

bool UGridSelectionSubsystem::IsAdjacent6(const FIntVector& A, const FIntVector& B) const
{
    const FIntVector Delta = A - B;
    return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z) == 1;
}

void UGridSelectionSubsystem::BuildMineConnectivityAnchors(TSet<FIntVector>& OutAnchors, bool bCompletedOnly) const
{
    OutAnchors.Reset();

    // Current active selection is always a valid planning anchor.
    for (const TPair<FIntVector, float>& Pair : SelectedCells)
    {
        OutAnchors.Add(Pair.Key);
    }

    if (ActiveMineSettlementID == INDEX_NONE)
    {
        return;
    }

    const UMiningSubsystem* Mining = GetWorld() ? GetWorld()->GetSubsystem<UMiningSubsystem>() : nullptr;
    if (!Mining)
    {
        return;
    }

    TArray<FMiningDesignation> Designations;
    Mining->GetAllDesignations(ActiveMineSettlementID, Designations);

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

        OutAnchors.Add(Designation.GridPosition);
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
    if (ActiveMineSettlementID != INDEX_NONE)
    {
        const UMiningSubsystem* Mining = GetWorld() ? GetWorld()->GetSubsystem<UMiningSubsystem>() : nullptr;
        if (Mining)
        {
            TArray<FMiningDesignation> Designations;
            Mining->GetAllDesignations(ActiveMineSettlementID, Designations);
            for (const FMiningDesignation& Designation : Designations)
            {
                if (Designation.State == EMiningDesignationState::Complete)
                {
                    DeepestOpenZ = FMath::Min(DeepestOpenZ, Designation.GridPosition.Z);
                }
            }
        }
    }

    return DeepestOpenZ - 2;
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

