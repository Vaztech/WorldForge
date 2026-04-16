#include "BuildModeManager.h"
#include "BuildGhostActor.h"
#include "PlayerBuildContext.h"
#include "VoxelWorldManager.h"
#include "VoxelConstructionSystem.h"
#include "SettlementManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

namespace
{
    static bool IsPlacedBlockActor(const AActor* Actor)
    {
        return Actor && Actor->ActorHasTag(FName(TEXT("WorldForgePlacedBlock")));
    }

    static bool IsPendingGhostSupportComponent(const UPrimitiveComponent* Component)
    {
        return Component && Component->ComponentHasTag(FName(TEXT("WorldForgePendingBlockGhost")));
    }

    static FIntVector GetFaceOffsetFromNormal(const FVector& InNormal)
    {
        const FVector N = InNormal.GetSafeNormal();

        if (FMath::Abs(N.Z) >= FMath::Abs(N.X) && FMath::Abs(N.Z) >= FMath::Abs(N.Y))
        {
            return (N.Z >= 0.0f) ? FIntVector(0, 0, 1) : FIntVector(0, 0, -1);
        }

        if (FMath::Abs(N.X) >= FMath::Abs(N.Y))
        {
            return (N.X >= 0.0f) ? FIntVector(1, 0, 0) : FIntVector(-1, 0, 0);
        }

        return (N.Y >= 0.0f) ? FIntVector(0, 1, 0) : FIntVector(0, -1, 0);
    }

    static bool HasPlacedBlockAtVoxel(UWorld* World, UVoxelWorldManager* VM, const FIntVector& TargetVoxel)
    {
        if (!World || !VM)
        {
            return false;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsPlacedBlockActor(Actor))
            {
                continue;
            }

            const FIntVector ActorVoxel = VM->WorldToGrid3D(Actor->GetActorLocation());
            if (ActorVoxel == TargetVoxel)
            {
                return true;
            }
        }

        return false;
    }
}

void UBuildModeManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Manager initialized."));
}

void UBuildModeManager::Deinitialize()
{
    DestroyGhost();
    Super::Deinitialize();
}

// ================================
// Subsystem Helpers
// ================================

UPlayerBuildContext* UBuildModeManager::GetBuildContext() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<UPlayerBuildContext>() : nullptr;
}

UVoxelWorldManager* UBuildModeManager::GetVoxelManager() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<UVoxelWorldManager>() : nullptr;
}

UVoxelConstructionSystem* UBuildModeManager::GetConstructionSystem() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<UVoxelConstructionSystem>() : nullptr;
}

USettlementManager* UBuildModeManager::GetSettlementManager() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<USettlementManager>() : nullptr;
}

int32 UBuildModeManager::GetEffectiveSettlementID() const
{
    if (ActiveSettlementID >= 0)
    {
        return ActiveSettlementID;
    }

    return 0;
}

float UBuildModeManager::GetMaxGroundBuildSlopeDegrees() const
{
    return 35.0f;
}

bool UBuildModeManager::IsBuildableTerrainType(EWorldForgeTerrainType TerrainType) const
{
    switch (TerrainType)
    {
    case EWorldForgeTerrainType::Grass:
    case EWorldForgeTerrainType::Dirt:
    case EWorldForgeTerrainType::Rock:
        return true;

    case EWorldForgeTerrainType::Sand:
    case EWorldForgeTerrainType::Snow:
    default:
        return false;
    }
}

bool UBuildModeManager::ValidateGroundTerrainPlacement(const FIntVector& Voxel, FString& OutReason) const
{
    OutReason.Empty();

    UVoxelWorldManager* VM = GetVoxelManager();
    if (!VM || !VM->HasCachedTerrainData())
    {
        OutReason = TEXT("Terrain data unavailable.");
        return false;
    }

    const FIntPoint VoxelXY(Voxel.X, Voxel.Y);

    int32 SurfaceVoxelZ = 0;
    if (VM->TryGetTerrainSurfaceVoxelZ(VoxelXY, SurfaceVoxelZ))
    {
        // Preserve stacking / face-attachment behavior.
        // Terrain restrictions only apply to the ground surface cell.
        if (Voxel.Z > SurfaceVoxelZ)
        {
            return true;
        }
    }

    EWorldForgeTerrainType TerrainType = EWorldForgeTerrainType::Grass;
    if (!VM->GetTerrainTypeAtVoxel(VoxelXY, TerrainType))
    {
        OutReason = TEXT("Invalid terrain cell.");
        return false;
    }

    if (!IsBuildableTerrainType(TerrainType))
    {
        OutReason = TEXT("Cannot build on this terrain type.");
        return false;
    }

    float SlopeDegrees = 0.0f;
    if (!VM->GetTerrainSlopeAtVoxel(VoxelXY, SlopeDegrees))
    {
        OutReason = TEXT("Could not evaluate terrain slope.");
        return false;
    }

    if (SlopeDegrees > GetMaxGroundBuildSlopeDegrees())
    {
        OutReason = FString::Printf(
            TEXT("Terrain too steep (%.1f° > %.1f°)."),
            SlopeDegrees,
            GetMaxGroundBuildSlopeDegrees());
        return false;
    }

    return true;
}

// ================================
// Live Cost / Availability Queries
// ================================

FGameplayTag UBuildModeManager::GetCurrentRequiredItemTag() const
{
    if (UVoxelConstructionSystem* Construction = GetConstructionSystem())
    {
        if (CurrentMode == EBuildMode::Prefab && SelectedBuildingTag.IsValid())
        {
            return Construction->GetRequiredItemForPrefab(SelectedBuildingTag);
        }

        if (CurrentMode == EBuildMode::CustomDraw && SelectedMaterialTag.IsValid())
        {
            return Construction->GetRequiredItemForMaterial(SelectedMaterialTag);
        }
    }

    return FGameplayTag::EmptyTag;
}

int32 UBuildModeManager::GetCurrentRequiredUnits() const
{
    if (UVoxelConstructionSystem* Construction = GetConstructionSystem())
    {
        if (CurrentMode == EBuildMode::Prefab && SelectedBuildingTag.IsValid())
        {
            return Construction->GetRequiredUnitsForPrefab(SelectedBuildingTag);
        }

        if (CurrentMode == EBuildMode::CustomDraw && SelectedMaterialTag.IsValid())
        {
            return Construction->GetRequiredUnitsForCustom(SelectedCells.Num());
        }
    }

    return 0;
}

int32 UBuildModeManager::GetCurrentAvailableUnits() const
{
    const int32 SettlementID = GetEffectiveSettlementID();

    if (SettlementID < 0)
    {
        return 0;
    }

    if (UVoxelConstructionSystem* Construction = GetConstructionSystem())
    {
        if (CurrentMode == EBuildMode::Prefab && SelectedBuildingTag.IsValid())
        {
            return Construction->GetAvailableUnitsForPrefab(SettlementID, SelectedBuildingTag, GetWorld());
        }

        if (CurrentMode == EBuildMode::CustomDraw && SelectedMaterialTag.IsValid())
        {
            return Construction->GetAvailableUnitsForMaterial(SettlementID, SelectedMaterialTag, GetWorld());
        }
    }

    return 0;
}

bool UBuildModeManager::CanAffordCurrentSelection() const
{
    const int32 SettlementID = GetEffectiveSettlementID();

    if (SettlementID < 0)
    {
        return false;
    }

    if (UVoxelConstructionSystem* Construction = GetConstructionSystem())
    {
        if (CurrentMode == EBuildMode::Prefab && SelectedBuildingTag.IsValid())
        {
            return Construction->CanAffordPrefab(SettlementID, SelectedBuildingTag, GetWorld());
        }

        if (CurrentMode == EBuildMode::CustomDraw && SelectedMaterialTag.IsValid())
        {
            return Construction->CanAffordCustom(SettlementID, SelectedMaterialTag, SelectedCells.Num(), GetWorld());
        }
    }

    return false;
}

// ================================
// Mode Entry / Exit
// ================================

void UBuildModeManager::EnterPrefabMode(FGameplayTag BuildingTag)
{
    if (!BuildingTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] EnterPrefabMode: invalid BuildingTag."));
        BroadcastFeedback(TEXT("Invalid building selection."));
        return;
    }

    CurrentMode = EBuildMode::Prefab;
    SelectedBuildingTag = BuildingTag;
    SelectedMaterialTag = FGameplayTag::EmptyTag;
    RotationIndex = 0;
    SelectedCells.Empty();
    SelectedCells3D.Empty();
    bHoveredCellValid = false;
    bJustCommitted = false;
    bHasLastValidVoxel = false;
    LastValidVoxel = FIntVector::ZeroValue;
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;

    SpawnGhost();

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered Prefab mode: %s"), *BuildingTag.ToString());
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(FString::Printf(TEXT("Selected building: %s"), *BuildingTag.ToString()));
}

void UBuildModeManager::EnterCustomDrawMode(FGameplayTag MaterialTag)
{
    if (!MaterialTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] EnterCustomDrawMode: invalid MaterialTag."));
        BroadcastFeedback(TEXT("Invalid material selection."));
        return;
    }

    CurrentMode = EBuildMode::CustomDraw;
    SelectedMaterialTag = MaterialTag;
    SelectedBuildingTag = FGameplayTag::EmptyTag;
    RotationIndex = 0;
    SelectedCells.Empty();
    SelectedCells3D.Empty();
    bHoveredCellValid = false;
    bJustCommitted = false;
    bHasLastValidVoxel = false;
    LastValidVoxel = FIntVector::ZeroValue;
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->SetMaterial(MaterialTag);
        Ctx->SetActiveSettlement(GetEffectiveSettlementID());
        Ctx->ClearSelection();
    }

    SpawnGhost();

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered CustomDraw mode: %s"), *MaterialTag.ToString());
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(FString::Printf(TEXT("Selected material: %s"), *MaterialTag.ToString()));
}

void UBuildModeManager::ExitBuildMode()
{
    if (CurrentMode == EBuildMode::None) return;

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
    }

    CurrentMode = EBuildMode::None;
    SelectedBuildingTag = FGameplayTag::EmptyTag;
    SelectedMaterialTag = FGameplayTag::EmptyTag;
    SelectedCells.Empty();
    SelectedCells3D.Empty();
    CurrentHoveredCell = FIntPoint::ZeroValue;
    CurrentHoveredVoxel = FIntVector::ZeroValue;
    RotationIndex = 0;
    bHoveredCellValid = false;
    LastHitZ = 0.0f;
    bJustCommitted = false;
    bHasLastValidVoxel = false;
    LastValidVoxel = FIntVector::ZeroValue;
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;

    DestroyGhost();

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Exited build mode."));
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(TEXT("Build mode exited."));
}

// ================================
// Configuration
// ================================

void UBuildModeManager::SetActiveSettlement(int32 SettlementID)
{
    ActiveSettlementID = SettlementID;

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->SetActiveSettlement(SettlementID);
    }
}

void UBuildModeManager::SetBuildAgentID(int32 AgentID)
{
    BuildAgentID = AgentID;
}

void UBuildModeManager::SetDragBuildActive(bool bActive)
{
    bIsDraggingBuild = bActive;

    if (!bActive)
    {
        LastPlacedDragVoxel = FIntVector(INT32_MIN);
        bHasDragStart = false;
        DragStartVoxel = FIntVector::ZeroValue;
        bDragStartedFromBlock = false;
    }
}

// ================================
// Input Handlers
// ================================

void UBuildModeManager::HandlePrimaryClick()
{
    bJustCommitted = false;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[BuildMode] HandlePrimaryClick: Mode=%d HoverValid=%s HoveredCell=(%d,%d) HoveredVoxel=(%d,%d,%d) SettlementID=%d BuildingTag=%s"),
        static_cast<int32>(CurrentMode),
        bHoveredCellValid ? TEXT("true") : TEXT("false"),
        CurrentHoveredCell.X,
        CurrentHoveredCell.Y,
        CurrentHoveredVoxel.X,
        CurrentHoveredVoxel.Y,
        CurrentHoveredVoxel.Z,
        GetEffectiveSettlementID(),
        SelectedBuildingTag.IsValid() ? *SelectedBuildingTag.ToString() : TEXT("Invalid")
    );

    if (CurrentMode == EBuildMode::None || !bHoveredCellValid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] HandlePrimaryClick: blocked by mode/hover validity."));
        BroadcastFeedback(TEXT("Hover a valid terrain cell first."));
        return;
    }

    bIsDraggingBuild = true;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    DragStartVoxel = CurrentHoveredVoxel;
    bHasDragStart = true;
    bDragStartedFromBlock = false;

    if (CurrentMode == EBuildMode::Prefab)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] HandlePrimaryClick -> Prefab"));
        const bool bCommitted = CommitPrefabBuild();
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[BuildMode] HandlePrimaryClick -> Prefab result: %s"),
            bCommitted ? TEXT("SUCCESS") : TEXT("FAILED")
        );
        return;
    }

    if (CurrentMode == EBuildMode::CustomDraw)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] HandlePrimaryClick -> CustomDraw"));

        if (SelectedCells3D.Contains(CurrentHoveredVoxel))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[BuildMode] HandlePrimaryClick -> CustomDraw skipped: cell already selected (%d,%d,%d)"),
                CurrentHoveredVoxel.X,
                CurrentHoveredVoxel.Y,
                CurrentHoveredVoxel.Z
            );
            return;
        }

        if (UWorld* World = GetWorld())
        {
            if (UVoxelWorldManager* VM = GetVoxelManager())
            {
                if (HasPlacedBlockAtVoxel(World, VM, CurrentHoveredVoxel))
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[BuildMode] HandlePrimaryClick -> CustomDraw skipped: occupied by placed block (%d,%d,%d)"),
                        CurrentHoveredVoxel.X,
                        CurrentHoveredVoxel.Y,
                        CurrentHoveredVoxel.Z
                    );
                    return;
                }

                FString TerrainFailure;
                if (!ValidateGroundTerrainPlacement(CurrentHoveredVoxel, TerrainFailure))
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[BuildMode] HandlePrimaryClick -> CustomDraw blocked by terrain validation at (%d,%d,%d): %s"),
                        CurrentHoveredVoxel.X,
                        CurrentHoveredVoxel.Y,
                        CurrentHoveredVoxel.Z,
                        *TerrainFailure
                    );
                    BroadcastFeedback(TerrainFailure);
                    return;
                }
            }
        }

        SelectedCells3D.Empty();
        SelectedCells.Empty();
        SelectedCells3D.Add(CurrentHoveredVoxel);
        SelectedCells.Add(CurrentHoveredCell);
        LastPlacedDragVoxel = CurrentHoveredVoxel;

        if (UPlayerBuildContext* Ctx = GetBuildContext())
        {
            Ctx->ClearSelection();
            Ctx->AddCell(CurrentHoveredCell);
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[BuildMode] Added cell (%d, %d, %d) — %d total"),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z,
            SelectedCells3D.Num()
        );

        OnCellCountChanged.Broadcast(SelectedCells.Num());
        BroadcastFeedback(FString::Printf(
            TEXT("Added cell (%d, %d, %d). Total: %d"),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z,
            SelectedCells3D.Num()));
        UpdateGhost();
    }
}

void UBuildModeManager::HandleSecondaryClick()
{
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;
}

void UBuildModeManager::HandleUndo()
{
    bJustCommitted = false;
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;

    if (CurrentMode != EBuildMode::CustomDraw || SelectedCells3D.Num() == 0)
    {
        return;
    }

    const FIntVector Removed3D = SelectedCells3D.Pop();
    if (SelectedCells.Num() > 0)
    {
        SelectedCells.Pop();
    }

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
        for (const FIntPoint& Cell : SelectedCells)
        {
            Ctx->AddCell(Cell);
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[BuildMode] Undo cell (%d, %d, %d) — %d remaining"),
        Removed3D.X,
        Removed3D.Y,
        Removed3D.Z,
        SelectedCells3D.Num()
    );

    OnCellCountChanged.Broadcast(SelectedCells.Num());
    BroadcastFeedback(FString::Printf(
        TEXT("Undid cell (%d, %d, %d). Remaining: %d"),
        Removed3D.X, Removed3D.Y, Removed3D.Z,
        SelectedCells3D.Num()));
    UpdateGhost();
}

void UBuildModeManager::HandleRotate(bool bClockwise)
{
    bJustCommitted = false;
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;

    if (CurrentMode != EBuildMode::Prefab) return;

    RotationIndex = bClockwise
        ? (RotationIndex + 1) % 4
        : (RotationIndex + 3) % 4;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[BuildMode] Rotation: %d (= %d degrees)"),
        RotationIndex,
        RotationIndex * 90
    );

    BroadcastFeedback(FString::Printf(TEXT("Rotation: %d degrees"), RotationIndex * 90));
    UpdateGhost();
}

void UBuildModeManager::HandleConfirm()
{
    if (bJustCommitted)
    {
        return;
    }

    if (CurrentMode == EBuildMode::CustomDraw)
    {
        if (CommitCustomBuild())
        {
            bJustCommitted = true;
            bIsDraggingBuild = false;
            LastPlacedDragVoxel = FIntVector(INT32_MIN);
            bHasDragStart = false;
            DragStartVoxel = FIntVector::ZeroValue;
            bDragStartedFromBlock = false;
        }
    }
}

void UBuildModeManager::HandleCancel()
{
    ExitBuildMode();
}

// ================================
// Mouse Update (call each frame while in build mode)
// ================================

void UBuildModeManager::UpdateFromMouse(APlayerController* PC)
{
    if (CurrentMode == EBuildMode::None || !PC)
    {
        return;
    }

    FVector HitLocation = FVector::ZeroVector;
    bool bHitPlacedBlock = false;
    FIntVector SupportBlockVoxel = FIntVector::ZeroValue;
    FVector HitNormal = FVector::UpVector;

    const bool bHit = RaycastToTerrain(PC, HitLocation, bHitPlacedBlock, SupportBlockVoxel, HitNormal);

    UVoxelWorldManager* VM = GetVoxelManager();
    UWorld* World = GetWorld();
    if (!VM || !World)
    {
        return;
    }

    if (!bHit)
    {
        if (bHasLastValidVoxel)
        {
            CurrentHoveredVoxel = LastValidVoxel;
            CurrentHoveredCell = FIntPoint(CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y);
            bHoveredCellValid = true;
            UpdateGhost();
        }
        else
        {
            bHoveredCellValid = false;

            if (GhostActor)
            {
                GhostActor->SetGhostState(EBuildGhostState::Invalid);
                UpdateGhost();
            }
        }
        return;
    }

    const FIntPoint TerrainCell = VM->WorldToTerrainCell(HitLocation);

    FIntVector NewVoxel = VM->WorldToGrid3D(HitLocation);
    NewVoxel.X = TerrainCell.X;
    NewVoxel.Y = TerrainCell.Y;

    if (bHitPlacedBlock)
    {
        const FIntVector FaceOffset = GetFaceOffsetFromNormal(HitNormal);
        NewVoxel = SupportBlockVoxel + FaceOffset;
    }
    else
    {
        if (!(bIsDraggingBuild && CurrentMode == EBuildMode::CustomDraw))
        {
            int32 HighestZ = NewVoxel.Z;

            for (const FIntVector& Cell : SelectedCells3D)
            {
                if (Cell.X == TerrainCell.X && Cell.Y == TerrainCell.Y)
                {
                    if (Cell.Z >= HighestZ)
                    {
                        HighestZ = Cell.Z + 1;
                    }
                }
            }

            NewVoxel.Z = HighestZ;
        }
    }

    const bool bPendingOccupied = SelectedCells3D.Contains(NewVoxel);
    const bool bPlacedOccupied = HasPlacedBlockAtVoxel(World, VM, NewVoxel);

    const bool bSkipOccupancyGateForDrag =
        bIsDraggingBuild &&
        CurrentMode == EBuildMode::CustomDraw &&
        bHasDragStart;

    if ((bPendingOccupied || bPlacedOccupied) && !bSkipOccupancyGateForDrag)
    {
        bHoveredCellValid = false;

        if (GhostActor)
        {
            GhostActor->SetGhostState(EBuildGhostState::Invalid);
            UpdateGhost();
        }

        return;
    }

    if (!bHitPlacedBlock)
    {
        FString TerrainFailure;
        if (!ValidateGroundTerrainPlacement(NewVoxel, TerrainFailure))
        {
            bHoveredCellValid = false;

            if (GhostActor)
            {
                GhostActor->SetGhostState(EBuildGhostState::Invalid);
                UpdateGhost();
            }

            return;
        }
    }

    LastValidVoxel = NewVoxel;
    bHasLastValidVoxel = true;

    const bool bChanged =
        (NewVoxel != CurrentHoveredVoxel) ||
        !bHoveredCellValid;

    CurrentHoveredVoxel = NewVoxel;
    CurrentHoveredCell = FIntPoint(NewVoxel.X, NewVoxel.Y);
    bHoveredCellValid = true;
    LastHitZ = HitLocation.Z;

    if (bChanged)
    {
        OnHoveredCellChanged.Broadcast(CurrentHoveredCell);
        UpdateGhost();
    }

    if (!bIsDraggingBuild || CurrentMode != EBuildMode::CustomDraw || !bHasDragStart)
    {
        return;
    }

    TArray<FIntVector> LineCells3D;
    TArray<FIntPoint> LineCells2D;
    const FIntVector EndVoxel = CurrentHoveredVoxel;

    if (SelectedCells3D.Num() <= 1)
    {
        bDragStartedFromBlock = (DragStartVoxel != CurrentHoveredVoxel) && bHitPlacedBlock;
    }

    if (bDragStartedFromBlock)
    {
        const int32 Dx = EndVoxel.X - DragStartVoxel.X;
        const int32 Dy = EndVoxel.Y - DragStartVoxel.Y;
        const int32 Dz = EndVoxel.Z - DragStartVoxel.Z;

        const int32 Steps = FMath::Max3(
            FMath::Abs(Dx),
            FMath::Abs(Dy),
            FMath::Abs(Dz)
        );

        for (int32 Index = 0; Index <= Steps; ++Index)
        {
            const float Alpha = (Steps == 0) ? 0.0f : static_cast<float>(Index) / static_cast<float>(Steps);

            FIntVector LineVoxel;
            LineVoxel.X = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.X), static_cast<float>(EndVoxel.X), Alpha));
            LineVoxel.Y = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.Y), static_cast<float>(EndVoxel.Y), Alpha));
            LineVoxel.Z = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.Z), static_cast<float>(EndVoxel.Z), Alpha));

            if (LineCells3D.Contains(LineVoxel))
            {
                continue;
            }

            if (HasPlacedBlockAtVoxel(World, VM, LineVoxel))
            {
                continue;
            }

            LineCells3D.Add(LineVoxel);
            LineCells2D.Add(FIntPoint(LineVoxel.X, LineVoxel.Y));
        }
    }
    else
    {
        const int32 Dx = EndVoxel.X - DragStartVoxel.X;
        const int32 Dy = EndVoxel.Y - DragStartVoxel.Y;

        const int32 Steps = FMath::Max(
            FMath::Abs(Dx),
            FMath::Abs(Dy)
        );

        bool bHasPreviousTerrainPoint = false;
        int32 PreviousX = 0;
        int32 PreviousY = 0;
        int32 PreviousZ = 0;

        for (int32 Index = 0; Index <= Steps; ++Index)
        {
            const float Alpha = (Steps == 0) ? 0.0f : static_cast<float>(Index) / static_cast<float>(Steps);

            const int32 LineX = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.X), static_cast<float>(EndVoxel.X), Alpha));
            const int32 LineY = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.Y), static_cast<float>(EndVoxel.Y), Alpha));

            int32 SurfaceVoxelZ = DragStartVoxel.Z;
            VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(LineX, LineY), SurfaceVoxelZ);

            const FIntVector CurrentTerrainVoxel(LineX, LineY, SurfaceVoxelZ);
            FString TerrainFailure;
            if (!ValidateGroundTerrainPlacement(CurrentTerrainVoxel, TerrainFailure))
            {
                continue;
            }

            if (!bHasPreviousTerrainPoint)
            {
                const FIntVector FirstVoxel(LineX, LineY, SurfaceVoxelZ);

                if (!LineCells3D.Contains(FirstVoxel) &&
                    !HasPlacedBlockAtVoxel(World, VM, FirstVoxel))
                {
                    LineCells3D.Add(FirstVoxel);
                    LineCells2D.Add(FIntPoint(FirstVoxel.X, FirstVoxel.Y));
                }

                PreviousX = LineX;
                PreviousY = LineY;
                PreviousZ = SurfaceVoxelZ;
                bHasPreviousTerrainPoint = true;
                continue;
            }

            const int32 DeltaZ = SurfaceVoxelZ - PreviousZ;
            const int32 ZStepDir = (DeltaZ > 0) ? 1 : ((DeltaZ < 0) ? -1 : 0);

            if (ZStepDir != 0)
            {
                for (int32 BridgeZ = PreviousZ; BridgeZ != SurfaceVoxelZ + ZStepDir; BridgeZ += ZStepDir)
                {
                    const FIntVector BridgeVoxel(PreviousX, PreviousY, BridgeZ);

                    if (LineCells3D.Contains(BridgeVoxel))
                    {
                        continue;
                    }

                    if (HasPlacedBlockAtVoxel(World, VM, BridgeVoxel))
                    {
                        continue;
                    }

                    FString BridgeFailure;
                    if (!ValidateGroundTerrainPlacement(BridgeVoxel, BridgeFailure))
                    {
                        continue;
                    }

                    LineCells3D.Add(BridgeVoxel);
                    LineCells2D.Add(FIntPoint(BridgeVoxel.X, BridgeVoxel.Y));
                }
            }

            const FIntVector CurrentVoxel(LineX, LineY, SurfaceVoxelZ);

            if (!LineCells3D.Contains(CurrentVoxel) &&
                !HasPlacedBlockAtVoxel(World, VM, CurrentVoxel))
            {
                LineCells3D.Add(CurrentVoxel);
                LineCells2D.Add(FIntPoint(CurrentVoxel.X, CurrentVoxel.Y));
            }

            PreviousX = LineX;
            PreviousY = LineY;
            PreviousZ = SurfaceVoxelZ;
        }
    }

    SelectedCells3D = LineCells3D;
    SelectedCells = LineCells2D;
    LastPlacedDragVoxel = CurrentHoveredVoxel;

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
        for (const FIntPoint& Cell : SelectedCells)
        {
            Ctx->AddCell(Cell);
        }
    }

    OnCellCountChanged.Broadcast(SelectedCells.Num());
    UpdateGhost();
}

// ================================
// Raycast
// ================================

bool UBuildModeManager::RaycastToTerrain(
    APlayerController* PC,
    FVector& OutHitLocation,
    bool& bOutHitPlacedBlock,
    FIntVector& OutSupportBlockVoxel,
    FVector& OutHitNormal) const
{
    bOutHitPlacedBlock = false;
    OutSupportBlockVoxel = FIntVector::ZeroValue;
    OutHitNormal = FVector::UpVector;

    if (!PC)
    {
        return false;
    }

    FVector WorldLocation = FVector::ZeroVector;
    FVector WorldDirection = FVector::ZeroVector;
    if (!PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
    {
        return false;
    }

    UWorld* W = GetWorld();
    if (!W)
    {
        return false;
    }

    UVoxelWorldManager* VM = GetVoxelManager();
    if (!VM)
    {
        return false;
    }

    const FVector TraceStart = WorldLocation;
    const FVector TraceEnd = WorldLocation + (WorldDirection * 50000.0f);

    FCollisionQueryParams TerrainParams(SCENE_QUERY_STAT(BuildModeTerrainTrace), false);
    TerrainParams.bTraceComplex = false;

    if (GhostActor)
    {
        TerrainParams.AddIgnoredActor(GhostActor);
    }

    for (TActorIterator<AActor> It(W); It; ++It)
    {
        AActor* Actor = *It;
        if (IsPlacedBlockActor(Actor))
        {
            TerrainParams.AddIgnoredActor(Actor);
        }
    }

    FHitResult TerrainHit;
    const bool bHitTerrain = W->LineTraceSingleByChannel(
        TerrainHit,
        TraceStart,
        TraceEnd,
        ECC_Visibility,
        TerrainParams
    );

    FCollisionQueryParams FullParams(SCENE_QUERY_STAT(BuildModeFullTrace), false);
    FullParams.bTraceComplex = false;

    FHitResult FullHit;
    const bool bHitFull = W->LineTraceSingleByChannel(
        FullHit,
        TraceStart,
        TraceEnd,
        ECC_Visibility,
        FullParams
    );

    if (bHitFull)
    {
        if (IsPlacedBlockActor(FullHit.GetActor()))
        {
            bOutHitPlacedBlock = true;
            OutSupportBlockVoxel = VM->WorldToGrid3D(FullHit.GetActor()->GetActorLocation());
            OutHitLocation = FullHit.ImpactPoint;
            OutHitNormal = FullHit.ImpactNormal;
            return true;
        }

        if (IsPendingGhostSupportComponent(FullHit.GetComponent()))
        {
            bOutHitPlacedBlock = true;
            OutSupportBlockVoxel = VM->WorldToGrid3D(FullHit.GetComponent()->GetComponentLocation());
            OutHitLocation = FullHit.ImpactPoint;
            OutHitNormal = FullHit.ImpactNormal;
            return true;
        }
    }

    if (bHitTerrain)
    {
        OutHitLocation = TerrainHit.ImpactPoint;
        OutHitNormal = TerrainHit.ImpactNormal;
        return true;
    }

    return false;
}

// ================================
// Commit
// ================================

bool UBuildModeManager::CommitPrefabBuild()
{
    const int32 EffectiveSettlementID = GetEffectiveSettlementID();

    if (!bHoveredCellValid || EffectiveSettlementID < 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[BuildMode] CommitPrefab: invalid state. HoverValid=%s SettlementID=%d Cell=(%d,%d) Voxel=(%d,%d,%d) BuildingTag=%s"),
            bHoveredCellValid ? TEXT("true") : TEXT("false"),
            EffectiveSettlementID,
            CurrentHoveredCell.X,
            CurrentHoveredCell.Y,
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z,
            SelectedBuildingTag.IsValid() ? *SelectedBuildingTag.ToString() : TEXT("Invalid")
        );

        BroadcastFeedback(TEXT("Cannot place building here."));
        return false;
    }

    if (!SelectedBuildingTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] CommitPrefab: SelectedBuildingTag invalid."));
        BroadcastFeedback(TEXT("No building selected."));
        return false;
    }

    FString TerrainFailure;
    if (!ValidateGroundTerrainPlacement(CurrentHoveredVoxel, TerrainFailure))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[BuildMode] CommitPrefab blocked by terrain validation at (%d,%d,%d): %s"),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z,
            *TerrainFailure
        );
        BroadcastFeedback(TerrainFailure);
        return false;
    }

    UPlayerBuildContext* Ctx = GetBuildContext();
    if (!Ctx)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] CommitPrefab: PlayerBuildContext is null."));
        BroadcastFeedback(TEXT("Build context unavailable."));
        return false;
    }

    Ctx->SetActiveSettlement(EffectiveSettlementID);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[BuildMode] CommitPrefab: calling PlayerBuildContext::CommitPrefabBuildByTagWithRotation3D at Cell=(%d,%d), HoveredVoxel=(%d,%d,%d), SettlementID=%d, AgentID=%d, Rotation=%d, BuildingTag=%s"),
        CurrentHoveredCell.X,
        CurrentHoveredCell.Y,
        CurrentHoveredVoxel.X,
        CurrentHoveredVoxel.Y,
        CurrentHoveredVoxel.Z,
        EffectiveSettlementID,
        BuildAgentID,
        RotationIndex,
        *SelectedBuildingTag.ToString()
    );

    const bool bSuccess = Ctx->CommitPrefabBuildByTagWithRotation3D(
        SelectedBuildingTag,
        CurrentHoveredVoxel,
        RotationIndex,
        BuildAgentID
    );

    if (bSuccess)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[BuildMode] Prefab committed at (%d, %d, %d)"),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z
        );

        BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[BuildMode] CommitPrefab: PlayerBuildContext::CommitPrefabBuildByTagWithRotation3D returned false.")
        );

        BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    }

    return bSuccess;
}

bool UBuildModeManager::CommitCustomBuild()
{
    if (SelectedCells3D.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] CommitCustom: no cells selected."));
        BroadcastFeedback(TEXT("Select at least one cell first."));
        return false;
    }

    const int32 EffectiveSettlementID = GetEffectiveSettlementID();

    if (EffectiveSettlementID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] CommitCustom: no settlement."));
        BroadcastFeedback(TEXT("No active settlement selected."));
        return false;
    }

    for (const FIntVector& Cell : SelectedCells3D)
    {
        FString TerrainFailure;
        if (!ValidateGroundTerrainPlacement(Cell, TerrainFailure))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[BuildMode] CommitCustom blocked by terrain validation at (%d,%d,%d): %s"),
                Cell.X,
                Cell.Y,
                Cell.Z,
                *TerrainFailure
            );
            BroadcastFeedback(TerrainFailure);
            return false;
        }
    }

    UPlayerBuildContext* Ctx = GetBuildContext();
    if (!Ctx)
    {
        BroadcastFeedback(TEXT("Build context unavailable."));
        return false;
    }

    Ctx->SetActiveSettlement(EffectiveSettlementID);

    const bool bSuccess = Ctx->CommitCustomBuild3D(SelectedCells3D, BuildAgentID);

    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("[BuildMode] Custom build committed: %d cells"), SelectedCells3D.Num());

        SelectedCells.Empty();
        SelectedCells3D.Empty();
        OnCellCountChanged.Broadcast(0);
        BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    }
    else
    {
        BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    }

    return bSuccess;
}

// ================================
// Ghost Management
// ================================

void UBuildModeManager::SpawnGhost()
{
    DestroyGhost();

    UWorld* W = GetWorld();
    if (!W) return;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    GhostActor = W->SpawnActor<ABuildGhostActor>(
        ABuildGhostActor::StaticClass(),
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        Params
    );

    if (GhostActor)
    {
        GhostActor->SetGhostState(EBuildGhostState::Hidden);
        UE_LOG(LogTemp, Log, TEXT("[BuildMode] Ghost actor spawned."));
    }
}

void UBuildModeManager::DestroyGhost()
{
    if (GhostActor)
    {
        GhostActor->Destroy();
        GhostActor = nullptr;
    }
}

void UBuildModeManager::UpdateGhost()
{
    if (!GhostActor)
    {
        return;
    }

    UVoxelWorldManager* VM = GetVoxelManager();
    if (!VM)
    {
        return;
    }

    if (!bHoveredCellValid)
    {
        if (CurrentMode == EBuildMode::CustomDraw)
        {
            GhostActor->UpdateCustomDrawCells3D(SelectedCells3D, CurrentHoveredVoxel, VM);
        }
        else if (CurrentMode == EBuildMode::Prefab)
        {
            const FVector WorldPos = VM->GridToWorld(
                FIntPoint(CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y),
                CurrentHoveredVoxel.Z
            );

            GhostActor->UpdatePrefabGhost(WorldPos, RotationIndex, SelectedBuildingTag, VM, WorldPos.Z);
        }

        return;
    }

    if (CurrentMode == EBuildMode::Prefab)
    {
        const FVector WorldPos = VM->GridToWorld(
            FIntPoint(CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y),
            CurrentHoveredVoxel.Z
        );

        GhostActor->UpdatePrefabGhost(WorldPos, RotationIndex, SelectedBuildingTag, VM, WorldPos.Z);
        GhostActor->SetGhostState(EBuildGhostState::Valid);
    }
    else if (CurrentMode == EBuildMode::CustomDraw)
    {
        GhostActor->UpdateCustomDrawCells3D(SelectedCells3D, CurrentHoveredVoxel, VM);
        GhostActor->SetGhostState(EBuildGhostState::Valid);
    }
}

// ================================
// UI Broadcast Helpers
// ================================

void UBuildModeManager::BroadcastSelectionChanged()
{
    OnBuildSelectionChanged.Broadcast(CurrentMode, SelectedBuildingTag, SelectedMaterialTag);
}

void UBuildModeManager::BroadcastFeedback(const FString& Message)
{
    if (!Message.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("[BuildMode] UI Feedback: %s"), *Message);
        OnBuildFeedbackMessage.Broadcast(Message);
    }
}