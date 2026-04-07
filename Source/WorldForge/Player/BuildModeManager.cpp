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

// ================================
// Input Handlers
// ================================

void UBuildModeManager::HandlePrimaryClick()
{
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
    }
    else if (CurrentMode == EBuildMode::CustomDraw)
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

            BroadcastFeedback(FString::Printf(
                TEXT("Cell (%d, %d, %d) is already selected."),
                CurrentHoveredVoxel.X,
                CurrentHoveredVoxel.Y,
                CurrentHoveredVoxel.Z));
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

                    BroadcastFeedback(FString::Printf(
                        TEXT("Cell (%d, %d, %d) is already occupied."),
                        CurrentHoveredVoxel.X,
                        CurrentHoveredVoxel.Y,
                        CurrentHoveredVoxel.Z));
                    return;
                }
            }
        }

        SelectedCells3D.Add(CurrentHoveredVoxel);
        SelectedCells.Add(CurrentHoveredCell);

        if (UPlayerBuildContext* Ctx = GetBuildContext())
        {
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
}

void UBuildModeManager::HandleUndo()
{
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
    if (CurrentMode == EBuildMode::CustomDraw)
    {
        CommitCustomBuild();
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

    if (RaycastToTerrain(PC, HitLocation, bHitPlacedBlock, SupportBlockVoxel, HitNormal))
    {
        UVoxelWorldManager* VM = GetVoxelManager();
        UWorld* World = GetWorld();

        if (VM && World)
        {
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

            const bool bPendingOccupied = SelectedCells3D.Contains(NewVoxel);
            const bool bPlacedOccupied = HasPlacedBlockAtVoxel(World, VM, NewVoxel);

            if (bPendingOccupied || bPlacedOccupied)
            {
                bHoveredCellValid = false;

                if (GhostActor)
                {
                    GhostActor->SetGhostState(EBuildGhostState::Hidden);
                }

                return;
            }

            const FIntPoint NewCell(NewVoxel.X, NewVoxel.Y);

            const bool bChanged =
                (NewVoxel != CurrentHoveredVoxel) ||
                (NewCell != CurrentHoveredCell) ||
                !bHoveredCellValid;

            if (bChanged)
            {
                CurrentHoveredVoxel = NewVoxel;
                CurrentHoveredCell = NewCell;
                bHoveredCellValid = true;
                LastHitZ = HitLocation.Z;

                OnHoveredCellChanged.Broadcast(CurrentHoveredCell);
                UpdateGhost();
            }
            else
            {
                bHoveredCellValid = true;
                LastHitZ = HitLocation.Z;
            }
        }
    }
    else
    {
        bHoveredCellValid = false;

        if (GhostActor)
        {
            GhostActor->SetGhostState(EBuildGhostState::Hidden);
        }
    }
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

    // Terrain-only fallback trace: ignore ghost + placed blocks so we can still
    // recover a terrain anchor when no support block face is under the cursor.
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

    // Full support trace: DO NOT ignore ghost actor here.
    // This is what lets pending selected ghost cubes act as support for
    // additional side attachments before construction completes.
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
    if (!GhostActor || !bHoveredCellValid)
    {
        return;
    }

    UVoxelWorldManager* VM = GetVoxelManager();
    if (!VM)
    {
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