#include "BuildModeManager.h"
#include "BuildGhostActor.h"
#include "PlayerBuildContext.h"
#include "VoxelWorldManager.h"
#include "VoxelConstructionSystem.h"
#include "SettlementManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"

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
    RotationIndex = 0;
    bHoveredCellValid = false;

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
        TEXT("[BuildMode] HandlePrimaryClick: Mode=%d HoverValid=%s HoveredCell=(%d,%d) SettlementID=%d BuildingTag=%s"),
        static_cast<int32>(CurrentMode),
        bHoveredCellValid ? TEXT("true") : TEXT("false"),
        CurrentHoveredCell.X,
        CurrentHoveredCell.Y,
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

        if (!SelectedCells.Contains(CurrentHoveredCell))
        {
            SelectedCells.Add(CurrentHoveredCell);

            if (UPlayerBuildContext* Ctx = GetBuildContext())
            {
                Ctx->AddCell(CurrentHoveredCell);
            }

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[BuildMode] Added cell (%d, %d) — %d total"),
                CurrentHoveredCell.X,
                CurrentHoveredCell.Y,
                SelectedCells.Num()
            );

            OnCellCountChanged.Broadcast(SelectedCells.Num());
            BroadcastFeedback(FString::Printf(TEXT("Added cell (%d, %d). Total: %d"), CurrentHoveredCell.X, CurrentHoveredCell.Y, SelectedCells.Num()));
            UpdateGhost();
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[BuildMode] HandlePrimaryClick -> CustomDraw skipped: cell already selected (%d,%d)"),
                CurrentHoveredCell.X,
                CurrentHoveredCell.Y
            );

            BroadcastFeedback(FString::Printf(TEXT("Cell (%d, %d) is already selected."), CurrentHoveredCell.X, CurrentHoveredCell.Y));
        }
    }
}

void UBuildModeManager::HandleSecondaryClick()
{
}

void UBuildModeManager::HandleUndo()
{
    if (CurrentMode != EBuildMode::CustomDraw || SelectedCells.Num() == 0)
    {
        return;
    }

    const FIntPoint Removed = SelectedCells.Pop();

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
        TEXT("[BuildMode] Undo cell (%d, %d) — %d remaining"),
        Removed.X,
        Removed.Y,
        SelectedCells.Num()
    );

    OnCellCountChanged.Broadcast(SelectedCells.Num());
    BroadcastFeedback(FString::Printf(TEXT("Undid cell (%d, %d). Remaining: %d"), Removed.X, Removed.Y, SelectedCells.Num()));
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
    if (CurrentMode == EBuildMode::None || !PC) return;

    FVector HitLocation;
    if (RaycastToTerrain(PC, HitLocation))
    {
        UVoxelWorldManager* VM = GetVoxelManager();
        if (VM)
        {
            const FIntPoint NewCell = VM->WorldToGrid(HitLocation);

            if (NewCell != CurrentHoveredCell || !bHoveredCellValid)
            {
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

bool UBuildModeManager::RaycastToTerrain(APlayerController* PC, FVector& OutHitLocation) const
{
    if (!PC) return false;

    FVector WorldLocation, WorldDirection;
    if (!PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
    {
        return false;
    }

    const FVector TraceStart = WorldLocation;
    const FVector TraceEnd = WorldLocation + (WorldDirection * 50000.f);

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.bTraceComplex = false;

    if (GhostActor)
    {
        Params.AddIgnoredActor(GhostActor);
    }

    UWorld* W = GetWorld();
    if (!W) return false;

    if (W->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
    {
        OutHitLocation = Hit.ImpactPoint;
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
            TEXT("[BuildMode] CommitPrefab: invalid state. HoverValid=%s SettlementID=%d Cell=(%d,%d) BuildingTag=%s"),
            bHoveredCellValid ? TEXT("true") : TEXT("false"),
            EffectiveSettlementID,
            CurrentHoveredCell.X,
            CurrentHoveredCell.Y,
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
        TEXT("[BuildMode] CommitPrefab: calling PlayerBuildContext::CommitPrefabBuildByTagWithRotation at Cell=(%d,%d), SettlementID=%d, AgentID=%d, Rotation=%d, BuildingTag=%s"),
        CurrentHoveredCell.X,
        CurrentHoveredCell.Y,
        EffectiveSettlementID,
        BuildAgentID,
        RotationIndex,
        *SelectedBuildingTag.ToString()
    );

    const bool bSuccess = Ctx->CommitPrefabBuildByTagWithRotation(
        SelectedBuildingTag,
        CurrentHoveredCell,
        RotationIndex,
        BuildAgentID
    );

    if (bSuccess)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[BuildMode] Prefab committed at (%d, %d)"),
            CurrentHoveredCell.X,
            CurrentHoveredCell.Y
        );

        BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[BuildMode] CommitPrefab: PlayerBuildContext::CommitPrefabBuildByTagWithRotation returned false.")
        );

        BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    }

    return bSuccess;
}

bool UBuildModeManager::CommitCustomBuild()
{
    if (SelectedCells.Num() == 0)
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

    const bool bSuccess = Ctx->CommitCustomBuild(BuildAgentID);

    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("[BuildMode] Custom build committed: %d cells"), SelectedCells.Num());

        SelectedCells.Empty();
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
    if (!GhostActor || !bHoveredCellValid) return;

    UVoxelWorldManager* VM = GetVoxelManager();
    if (!VM) return;

    if (CurrentMode == EBuildMode::Prefab)
    {
        FVector WorldPos = VM->GridToWorld(CurrentHoveredCell, 0);
        WorldPos.Z = LastHitZ;
        GhostActor->UpdatePrefabGhost(WorldPos, RotationIndex, SelectedBuildingTag, VM, LastHitZ);
        GhostActor->SetGhostState(EBuildGhostState::Valid);
    }
    else if (CurrentMode == EBuildMode::CustomDraw)
    {
        GhostActor->UpdateCustomDrawCells(SelectedCells, CurrentHoveredCell, VM, LastHitZ);
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