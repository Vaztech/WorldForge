#include "Player/BuildModeManager.h"

#include "Engine/StaticMesh.h"
#include "Player/BuildGhostActor.h"
#include "Grid/GridSelectionSubsystem.h"

bool UBuildModeManager::BeginDataTablePreviewMode(UStaticMesh* PreviewMesh, FName SourceID, bool bIsPrefab)
{
    if (!PreviewMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] DataTable preview activation failed: PreviewMesh is null. SourceID=%s Type=%s"),
            *SourceID.ToString(),
            bIsPrefab ? TEXT("Prefab") : TEXT("BuildPiece"));
        return false;
    }

    if (SourceID.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] DataTable preview activation failed: SourceID is None. Mesh=%s Type=%s"),
            *PreviewMesh->GetPathName(),
            bIsPrefab ? TEXT("Prefab") : TEXT("BuildPiece"));
        return false;
    }

    ExitDigMode();
    CurrentToolMode = EBuildToolMode::Place;

    CurrentMode = EBuildMode::Prefab;
    SelectedBuildingTag = FGameplayTag::EmptyTag;
    SelectedMaterialTag = FGameplayTag::EmptyTag;
    RotationIndex = 0;
    SelectedCells.Empty();
    SelectedCells3D.Empty();
    FarmPaintedWorldCells.Empty();
    bHoveredCellValid = false;
    bJustCommitted = false;
    bHasLastValidVoxel = false;
    LastValidVoxel = FIntVector::ZeroValue;
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;

    bDataTablePreviewActive = true;
    DataTablePreviewSourceID = SourceID;
    bDataTablePreviewIsPrefab = bIsPrefab;
    DataTablePreviewMesh = PreviewMesh;

    SpawnGhost();
    UE_LOG(LogTemp, Log, TEXT("[BuildMode] DataTable preview ghost spawn state: SourceID=%s GhostActor=%s Mesh=%s"),
        *SourceID.ToString(),
        GhostActor ? TEXT("valid") : TEXT("invalid"),
        *PreviewMesh->GetPathName());
    if (GhostActor)
    {
        GhostActor->LogPreviewDiagnostics(TEXT("BeginDataTablePreviewMode AfterSpawn"));
    }

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetToggleSelectedCellsOnReselect(bToggleSelectedCellsOnReselect);
        GridSelection->BeginSelectionTool(EBuildToolMode::Place, EGridSelectionShape::Single);
    }

    const bool bApplied = SetBuildPreviewMesh(PreviewMesh);
    if (!bApplied)
    {
        bDataTablePreviewActive = false;
        DataTablePreviewSourceID = NAME_None;
        bDataTablePreviewIsPrefab = false;
        DataTablePreviewMesh = nullptr;
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] DataTable preview mode started: SourceID=%s Type=%s Mesh=%s"),
        *SourceID.ToString(),
        bIsPrefab ? TEXT("Prefab") : TEXT("BuildPiece"),
        *PreviewMesh->GetPathName());
    if (GhostActor)
    {
        GhostActor->LogPreviewDiagnostics(TEXT("BeginDataTablePreviewMode Started"));
    }

    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(FString::Printf(TEXT("DataTable preview selected: %s"), *SourceID.ToString()));

    return true;
}

bool UBuildModeManager::SetBuildPreviewMesh(UStaticMesh* PreviewMesh)
{
    if (!PreviewMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] Build preview mesh update failed: PreviewMesh is null."));
        return false;
    }

    if (!GhostActor)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Build preview mesh resolved but no active ghost actor exists. Mesh=%s. Phase 8 hook point: call SetBuildPreviewMesh after SpawnGhost or DataTable placement preview activation."),
            *PreviewMesh->GetPathName());
        return false;
    }

    const bool bUpdated = GhostActor->SetPreviewMesh(PreviewMesh);
    if (bUpdated)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Build preview mesh applied to active ghost: %s"),
            *PreviewMesh->GetPathName());
        UpdateGhost();
        if (GhostActor)
        {
            GhostActor->LogPreviewDiagnostics(TEXT("SetBuildPreviewMesh Applied"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Build preview mesh could not be applied to active ghost: %s"),
            *PreviewMesh->GetPathName());
    }

    return bUpdated;
}
