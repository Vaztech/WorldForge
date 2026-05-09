// Source/WorldForge/Grid/GridSelectionSubsystem.cpp

#include "Grid/GridSelectionSubsystem.h"

#include "Player/TileGridOverlayActor.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

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

    UE_LOG(LogGridSelection, Log,
        TEXT("BeginSelectionTool Tool=%d Shape=%d ActiveSelected=%d HoverValid=%s Hover=(%d,%d,%d)"),
        static_cast<int32>(ToolMode),
        static_cast<int32>(Shape),
        SelectedCells.Num(),
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
            GridOverlay->ClearBaseGrid();
        }
        else
        {
            GridOverlay->HideOverlay();
        }
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
        for (const TPair<FIntVector, float>& Pair : DragPreviewCells)
        {
            SelectedCells.FindOrAdd(Pair.Key) = Pair.Value;
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
        SelectedCells.Remove(HoveredVoxel);
        if (GridOverlay)
        {
            GridOverlay->RemoveSelectedCell(HoveredVoxel);
        }
    }
    else
    {
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
    }
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

    const FVector TraceStart = RayOrigin;
    const FVector TraceEnd = RayOrigin + (RayDirection * TraceDistance);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(GridSelectionRaycast), true);
    Params.bReturnPhysicalMaterial = false;
    Params.AddIgnoredActor(PC->GetPawn());

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
    OutTerrainZ = Hit.ImpactPoint.Z;
    OutVoxel = WorldToVoxel(Hit.ImpactPoint, CellSizeUU);
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
