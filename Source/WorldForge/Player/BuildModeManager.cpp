#include "BuildModeManager.h"
#include "BuildGhostActor.h"
#include "TileGridOverlayActor.h"
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
#include "Player/DigGhostActor.h"
#include "Player/MiningGhostActor.h"
#include "Voxel/DigSubsystem.h"
#include "Mining/MiningSubsystem.h"
#include "Mining/MiningLayerRenderer.h"
#include "Farming/FarmingSubsystem.h"
#include "Farming/FarmingTypes.h"
#include "Grid/GridSelectionSubsystem.h"
#include "Grid/LayerViewSubsystem.h"
#include "Grid/GridDesignationTypes.h"
#include "Colony/WorldForgeColonist.h"
#include "Actors/WorldResourceActor.h"


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
    ExitDigMode();

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->EndSelectionTool(true);
    }

    DestroyGridOverlay();
    DestroyMiningLayerRenderer();
    DestroyGhost();
    Super::Deinitialize();
}

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

UDigSubsystem* UBuildModeManager::GetDigSubsystem()
{
    if (!CachedDigSubsystem)
    {
        if (UWorld* W = GetWorld())
        {
            CachedDigSubsystem = W->GetSubsystem<UDigSubsystem>();
        }
    }

    return CachedDigSubsystem;
}

UMiningSubsystem* UBuildModeManager::GetMiningSubsystem() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<UMiningSubsystem>() : nullptr;
}

UFarmingSubsystem* UBuildModeManager::GetFarmingSubsystem() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<UFarmingSubsystem>() : nullptr;
}

UGridSelectionSubsystem* UBuildModeManager::GetGridSelectionSubsystem() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<UGridSelectionSubsystem>() : nullptr;
}

ULayerViewSubsystem* UBuildModeManager::GetLayerViewSubsystem() const
{
    UWorld* W = GetWorld();
    return W ? W->GetSubsystem<ULayerViewSubsystem>() : nullptr;
}

int32 UBuildModeManager::GetEffectiveSettlementID() const
{
    // Player-facing build/mine/farm tools must always apply to the player colony.
    // NPC civilization settlements are active now and may occupy IDs 0..N before
    // the player colony is created, so falling back to settlement 0 is no longer safe.
    if (const USettlementManager* SettlementManager = GetSettlementManager())
    {
        const int32 PlayerSettlementID = SettlementManager->GetPlayerSettlementID();
        if (PlayerSettlementID >= 0 && SettlementManager->SettlementExists(PlayerSettlementID))
        {
            return PlayerSettlementID;
        }
    }

    // Before the player colony is created, player tools have no valid settlement target.
    return INDEX_NONE;
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

    default:
        return false;
    }
}

bool UBuildModeManager::IsBuildVoxelInsidePlayerClaim(const FIntVector& Voxel) const
{
    const USettlementManager* SettlementManager = GetSettlementManager();
    if (!SettlementManager)
    {
        return false;
    }

    // Patch 4D: building operates in voxel placement space; settlement claims are strategic
    // world-map tiles. Patch 4C owns the voxel-to-world-tile conversion and claim lookup.
    return SettlementManager->DoesPlayerSettlementClaimVoxelCell(FIntPoint(Voxel.X, Voxel.Y));
}

bool UBuildModeManager::IsDigVoxelInsidePlayerClaim(const FIntVector& Voxel) const
{
    const USettlementManager* SettlementManager = GetSettlementManager();
    if (!SettlementManager)
    {
        return false;
    }

    // Patch 4E: player dig commands operate in voxel space, while colony claims are
    // strategic world-map tiles. Patch 4C converts voxel XY back to the claimed tile.
    return SettlementManager->DoesPlayerSettlementClaimVoxelCell(FIntPoint(Voxel.X, Voxel.Y));
}

bool UBuildModeManager::IsMineVoxelInsidePlayerClaim(const FIntVector& Voxel) const
{
    const USettlementManager* SettlementManager = GetSettlementManager();
    if (!SettlementManager)
    {
        return false;
    }

    // Patch 4E: mine designations operate in voxel space, while colony claims are
    // strategic world-map tiles. Patch 4C converts voxel XY back to the claimed tile.
    return SettlementManager->DoesPlayerSettlementClaimVoxelCell(FIntPoint(Voxel.X, Voxel.Y));
}

bool UBuildModeManager::IsFarmCellInsidePlayerClaim(const FIntPoint& WorldCell) const
{
    const USettlementManager* SettlementManager = GetSettlementManager();
    if (!SettlementManager)
    {
        return false;
    }

    // Farm-painted cells are exact absolute grid/voxel XY cells. Patch 4C owns
    // conversion from voxel XY back to the claimed strategic world-map tile.
    return SettlementManager->DoesPlayerSettlementClaimVoxelCell(WorldCell);
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

void UBuildModeManager::SetStaticBaseGridEnabled(bool bEnabled)
{
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetStaticBaseGridEnabled(bEnabled);
        BroadcastFeedback(bEnabled ? TEXT("Static grid overlay enabled.") : TEXT("Static grid overlay disabled."));
    }
}

bool UBuildModeManager::ToggleStaticBaseGrid()
{
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        const bool bEnabled = GridSelection->ToggleStaticBaseGrid();
        BroadcastFeedback(bEnabled ? TEXT("Static grid overlay enabled.") : TEXT("Static grid overlay disabled."));
        return bEnabled;
    }

    return false;
}

bool UBuildModeManager::IsStaticBaseGridEnabled() const
{
    if (const UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        return GridSelection->IsStaticBaseGridEnabled();
    }

    return false;
}

void UBuildModeManager::SetToggleSelectedCellsOnReselect(bool bEnabled)
{
    bToggleSelectedCellsOnReselect = bEnabled;

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetToggleSelectedCellsOnReselect(bEnabled);
    }

    BroadcastFeedback(bEnabled
        ? TEXT("Selection toggle enabled: selecting an already selected block removes it.")
        : TEXT("Selection toggle disabled: reselecting a block keeps it selected."));
}

void UBuildModeManager::SetMiningRemovalMode(EMiningRemovalMode NewMode)
{
    if (UMiningSubsystem* MiningSub = GetMiningSubsystem())
    {
        MiningSub->SetMiningRemovalMode(NewMode);
        BroadcastFeedback(MiningSub->GetMiningRemovalModeText().ToString());
    }
}

EMiningRemovalMode UBuildModeManager::ToggleMiningRemovalMode()
{
    if (UMiningSubsystem* MiningSub = GetMiningSubsystem())
    {
        const EMiningRemovalMode NewMode = MiningSub->ToggleMiningRemovalMode();
        BroadcastFeedback(MiningSub->GetMiningRemovalModeText().ToString());
        return NewMode;
    }

    return EMiningRemovalMode::VoxelCube;
}

EMiningRemovalMode UBuildModeManager::GetMiningRemovalMode() const
{
    if (const UMiningSubsystem* MiningSub = GetMiningSubsystem())
    {
        return MiningSub->GetMiningRemovalMode();
    }

    return EMiningRemovalMode::VoxelCube;
}

FText UBuildModeManager::GetMiningRemovalModeText() const
{
    if (const UMiningSubsystem* MiningSub = GetMiningSubsystem())
    {
        return MiningSub->GetMiningRemovalModeText();
    }

    return FText::FromString(TEXT("Mining: Voxel Cube"));
}


void UBuildModeManager::SetMiningHeightMode(EWFMiningHeightMode NewMode)
{
    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        LayerView->SetMiningHeightMode(NewMode);
        BroadcastFeedback(LayerView->GetMiningHeightModeText().ToString());
    }
}

EWFMiningHeightMode UBuildModeManager::ToggleMiningHeightMode()
{
    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        const EWFMiningHeightMode NewMode = LayerView->ToggleMiningHeightMode();
        BroadcastFeedback(LayerView->GetMiningHeightModeText().ToString());
        return NewMode;
    }

    return EWFMiningHeightMode::TwoVoxelColumn;
}

EWFMiningHeightMode UBuildModeManager::GetMiningHeightMode() const
{
    if (const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        return LayerView->GetMiningHeightMode();
    }

    return EWFMiningHeightMode::TwoVoxelColumn;
}

FText UBuildModeManager::GetMiningHeightModeText() const
{
    if (const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        return LayerView->GetMiningHeightModeText();
    }

    return FText::FromString(TEXT("Mine Height: 2 Block Column"));
}

int32 UBuildModeManager::GetActiveVoxelLayerZ() const
{
    if (const ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        if (LayerView->HasActiveVoxelLayer())
        {
            return LayerView->GetActiveVoxelLayerZ();
        }
    }

    if (const UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        return GridSelection->GetActiveVoxelLayerZ();
    }

    return CurrentHoveredVoxel.Z;
}

void UBuildModeManager::EnterPrefabMode(FGameplayTag BuildingTag)
{
    if (!BuildingTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] EnterPrefabMode: invalid BuildingTag."));
        BroadcastFeedback(TEXT("Invalid building selection."));
        return;
    }

    ExitDigMode();
    CurrentToolMode = EBuildToolMode::Place;

    CurrentMode = EBuildMode::Prefab;
    SelectedBuildingTag = BuildingTag;
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

    SpawnGhost();
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetToggleSelectedCellsOnReselect(bToggleSelectedCellsOnReselect);
        GridSelection->BeginSelectionTool(EBuildToolMode::Place, EGridSelectionShape::Single);
    }

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

    ExitDigMode();
    CurrentToolMode = EBuildToolMode::Place;

    CurrentMode = EBuildMode::CustomDraw;
    SelectedMaterialTag = MaterialTag;
    SelectedBuildingTag = FGameplayTag::EmptyTag;
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

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->SetMaterial(MaterialTag);
        Ctx->SetActiveSettlement(GetEffectiveSettlementID());
        Ctx->ClearSelection();
    }

    SpawnGhost();
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetToggleSelectedCellsOnReselect(bToggleSelectedCellsOnReselect);
        GridSelection->BeginSelectionTool(EBuildToolMode::Place, EGridSelectionShape::Rectangle);
    }

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered CustomDraw mode: %s"), *MaterialTag.ToString());
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(FString::Printf(TEXT("Selected material: %s"), *MaterialTag.ToString()));
}

void UBuildModeManager::ExitBuildMode()
{
    if (CurrentMode == EBuildMode::None)
    {
        return;
    }

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
    }

    CurrentMode = EBuildMode::None;
    CurrentToolMode = EBuildToolMode::None;
    SelectedBuildingTag = FGameplayTag::EmptyTag;
    SelectedMaterialTag = FGameplayTag::EmptyTag;
    SelectedCells.Empty();
    SelectedCells3D.Empty();
    FarmPaintedWorldCells.Empty();
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

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->EndSelectionTool(true);
    }
    DeactivateGridOverlay();
    DestroyGhost();

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Exited build mode."));
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(TEXT("Build mode exited."));
}

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

void UBuildModeManager::AdjustActiveToolLayer(int32 Delta)
{
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->AdjustActiveToolLayer(Delta);

        if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
        {
            GridSelection->TickSelectionFromMouse(PC);
        }

        BroadcastFeedback(FString::Printf(TEXT("Tool layer: %+d"), GridSelection->GetActiveToolLayerOffset()));
    }
}

void UBuildModeManager::ResetActiveToolLayer()
{
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->ResetActiveToolLayer();

        if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
        {
            GridSelection->TickSelectionFromMouse(PC);
        }

        BroadcastFeedback(TEXT("Tool layer reset to surface."));
    }
}

int32 UBuildModeManager::GetActiveToolLayerOffset() const
{
    if (const UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        return GridSelection->GetActiveToolLayerOffset();
    }

    return 0;
}

void UBuildModeManager::AdjustActiveViewLayer(int32 Delta)
{
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->AdjustActiveViewLayer(Delta);
        BroadcastFeedback(FString::Printf(TEXT("View layer: %+d"), GridSelection->GetActiveViewLayerOffset()));
    }
}

void UBuildModeManager::ResetActiveViewLayer()
{
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->ResetActiveViewLayer();
        BroadcastFeedback(TEXT("View layer reset to surface."));
    }
}

int32 UBuildModeManager::GetActiveViewLayerOffset() const
{
    if (const UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        return GridSelection->GetActiveViewLayerOffset();
    }

    return 0;
}

void UBuildModeManager::SetDragBuildActive(bool bActive)
{
    const bool bWasDragging = bIsDraggingBuild;
    bIsDraggingBuild = bActive;

    if (!bActive)
    {
        if (bWasDragging)
        {
            if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
            {
                if (GridSelection->EndDragSelection(true))
                {
                    SyncSelectionFromGridSelection();
                }
            }
        }

        LastPlacedDragVoxel = FIntVector(INT32_MIN);
        bHasDragStart = false;
        DragStartVoxel = FIntVector::ZeroValue;
        bDragStartedFromBlock = false;
    }
}


void UBuildModeManager::HandlePrimaryClick()
{
    if (CurrentToolMode == EBuildToolMode::Dig)
    {
        ExecuteDig();
        return;
    }

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        bJustCommitted = false;
        bIsDraggingBuild = true;
        bHasDragStart = true;
        DragStartVoxel = CurrentHoveredVoxel;
        LastPlacedDragVoxel = FIntVector(INT32_MIN);

        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            if (!GridSelection->HasValidHover())
            {
                if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
                {
                    GridSelection->TickSelectionFromMouse(PC);
                }
            }

            if (!GridSelection->BeginDragSelection())
            {
                GridSelection->ToggleHoveredSelection();
                SyncSelectionFromGridSelection();
            }
        }

        return;
    }

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

        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            if (!GridSelection->HasValidHover())
            {
                if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
                {
                    GridSelection->TickSelectionFromMouse(PC);
                }
            }

            if (GridSelection->BeginDragSelection())
            {
                BroadcastFeedback(TEXT("Selection started. Release mouse to commit selected area."));
                return;
            }

            if (GridSelection->ToggleHoveredSelection())
            {
                SyncSelectionFromGridSelection();
                return;
            }
        }

        if (SelectedCells3D.Contains(CurrentHoveredVoxel) || SelectedCells.Contains(CurrentHoveredCell))
        {
            SelectedCells3D.Remove(CurrentHoveredVoxel);
            SelectedCells.Remove(CurrentHoveredCell);

            if (CurrentToolMode == EBuildToolMode::Farm)
            {
                FarmPaintedWorldCells.Remove(CurrentHoveredCell);
            }

            if (GridOverlay)
            {
                GridOverlay->RemoveSelectedCell(CurrentOverlayVoxel);
            }

            if (UPlayerBuildContext* Ctx = GetBuildContext())
            {
                Ctx->ClearSelection();
                for (const FIntPoint& Cell : SelectedCells)
                {
                    Ctx->AddCell(Cell);
                }
            }

            OnCellCountChanged.Broadcast(
                CurrentToolMode == EBuildToolMode::Farm
                ? FarmPaintedWorldCells.Num()
                : SelectedCells.Num());

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[BuildMode] Removed selected cell (%d,%d,%d). Remaining=%d"),
                CurrentHoveredVoxel.X,
                CurrentHoveredVoxel.Y,
                CurrentHoveredVoxel.Z,
                SelectedCells.Num());

            UpdateGhost();
            UpdateGridOverlay();
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

        SelectedCells3D.Add(CurrentHoveredVoxel);
        SelectedCells.Add(CurrentHoveredCell);

        if (GridOverlay)
        {
            GridOverlay->AddSelectedCell(CurrentOverlayVoxel, CurrentHoveredTerrainZ);
        }

        // Farm mode stores world cells separately so Confirm cannot lose them
        // if legacy/custom draw selection arrays get cleared before CommitFarmZone().
        if (CurrentToolMode == EBuildToolMode::Farm)
        {
            // Fallback/non-grid path for farm selection also uses exact grid XY.
            // Keep this consistent with SyncSelectionFromGridSelection().
            const FIntPoint FarmWorldCell(CurrentOverlayVoxel.X, CurrentOverlayVoxel.Y);
            FarmPaintedWorldCells.Add(FarmWorldCell);

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[BuildMode] Farm painted exact grid cell (%d,%d). FarmPaintedWorldCells=%d SelectedCells=%d SelectedCells3D=%d"),
                FarmWorldCell.X,
                FarmWorldCell.Y,
                FarmPaintedWorldCells.Num(),
                SelectedCells.Num(),
                SelectedCells3D.Num()
            );
        }

        LastPlacedDragVoxel = CurrentHoveredVoxel;

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

        OnCellCountChanged.Broadcast(
            CurrentToolMode == EBuildToolMode::Farm
            ? FarmPaintedWorldCells.Num()
            : SelectedCells.Num()
        );

        BroadcastFeedback(FString::Printf(
            TEXT("Added cell (%d, %d, %d). Total: %d"),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z,
            CurrentToolMode == EBuildToolMode::Farm
            ? FarmPaintedWorldCells.Num()
            : SelectedCells3D.Num()
        ));

        UpdateGhost();
        UpdateGridOverlay();
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
    if (CurrentToolMode == EBuildToolMode::Dig)
    {
        UndoLastDig();
        return;
    }

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

    if (CurrentMode != EBuildMode::Prefab)
    {
        return;
    }

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
        UE_LOG(LogTemp, Verbose, TEXT("[BuildMode] HandleConfirm ignored: just committed."));
        return;
    }

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        if (CommitMineDesignations())
        {
            if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
            {
                GridSelection->CommitSelectedCellsToConfirmedOverlay(EGridDesignationType::Mine);
                GridSelection->ClearPreviewCells();
                GridSelection->ClearSelectedCells();
                GridSelection->EndSelectionTool(false);
            }

            bJustCommitted = true;
            bIsDraggingBuild = false;
            LastPlacedDragVoxel = FIntVector(INT32_MIN);
            bHasDragStart = false;
            DragStartVoxel = FIntVector::ZeroValue;
            bDragStartedFromBlock = false;

            SelectedCells.Empty();
            SelectedCells3D.Empty();
            FarmPaintedWorldCells.Empty();

            OnCellCountChanged.Broadcast(0);
            UpdateGridOverlay();
        }
        return;
    }

    if (CurrentToolMode == EBuildToolMode::Farm)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] HandleConfirm -> Farm. FarmPaintedWorldCells=%d SelectedCells=%d SelectedCells3D=%d"),
            FarmPaintedWorldCells.Num(),
            SelectedCells.Num(),
            SelectedCells3D.Num());

        if (CommitFarmZone())
        {
            if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
            {
                GridSelection->CommitSelectedCellsToConfirmedOverlay(EGridDesignationType::Farm);
                GridSelection->ClearPreviewCells();
                GridSelection->ClearSelectedCells();
                GridSelection->EndSelectionTool(false);
            }

            bJustCommitted = true;
            bIsDraggingBuild = false;
            LastPlacedDragVoxel = FIntVector(INT32_MIN);
            bHasDragStart = false;
            DragStartVoxel = FIntVector::ZeroValue;
            bDragStartedFromBlock = false;

            SelectedCells.Empty();
            SelectedCells3D.Empty();
            FarmPaintedWorldCells.Empty();

            OnCellCountChanged.Broadcast(0);
            BroadcastFeedback(TEXT("Farm zone committed."));
        }

        return;
    }

    if (CurrentMode == EBuildMode::CustomDraw)
    {
        if (CommitCustomBuild())
        {
            if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
            {
                GridSelection->CommitSelectedCellsToConfirmedOverlay(EGridDesignationType::Build);
                GridSelection->ClearPreviewCells();
                GridSelection->ClearSelectedCells();
                GridSelection->EndSelectionTool(false);
            }

            bJustCommitted = true;
            bIsDraggingBuild = false;
            LastPlacedDragVoxel = FIntVector(INT32_MIN);
            bHasDragStart = false;
            DragStartVoxel = FIntVector::ZeroValue;
            bDragStartedFromBlock = false;

            SelectedCells.Empty();
            SelectedCells3D.Empty();
            FarmPaintedWorldCells.Empty();

            OnCellCountChanged.Broadcast(0);
            UpdateGridOverlay();
        }
    }
}

void UBuildModeManager::HandleCancel()
{
    if (CurrentToolMode == EBuildToolMode::Dig)
    {
        ExitDigMode();
        return;
    }

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        CurrentToolMode = EBuildToolMode::None;
        CurrentMode = EBuildMode::None;
        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            GridSelection->EndSelectionTool(true);
        }
        DeactivateGridOverlay();
        RestoreMiningPresentationVisibility();
        DestroyMiningLayerRenderer();
        DestroyGhost();

        if (ActiveMiningGhost)
        {
            ActiveMiningGhost->Destroy();
            ActiveMiningGhost = nullptr;
        }

        BroadcastSelectionChanged();
        BroadcastFeedback(TEXT("Mine mode exited."));
        return;
    }

    if (CurrentToolMode == EBuildToolMode::Farm)
    {
        ExitBuildMode();
        BroadcastFeedback(TEXT("Farm mode exited."));
        return;
    }

    ExitBuildMode();
}

void UBuildModeManager::EnterFarmMode()
{
    if (CurrentToolMode == EBuildToolMode::Farm)
    {
        // Do not treat pressing the Farm button twice as Cancel. This was causing
        // painted farm selections to disappear before Confirm could commit them.
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] EnterFarmMode ignored: already in Farm mode. FarmPaintedWorldCells=%d SelectedCells=%d SelectedCells3D=%d"),
            FarmPaintedWorldCells.Num(),
            SelectedCells.Num(),
            SelectedCells3D.Num());

        BroadcastFeedback(TEXT("Farm mode already active. Paint farm tiles, then Confirm."));
        return;
    }

    ExitDigMode();

    CurrentToolMode = EBuildToolMode::Farm;
    CurrentMode = EBuildMode::CustomDraw;

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

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->SetActiveSettlement(GetEffectiveSettlementID());
        Ctx->ClearSelection();
    }

    SpawnGhost();
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetToggleSelectedCellsOnReselect(bToggleSelectedCellsOnReselect);
        GridSelection->BeginSelectionTool(EBuildToolMode::Farm, EGridSelectionShape::Rectangle);
    }

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered Farm mode."));
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(TEXT("Farm mode enabled. Paint farm tiles, then Confirm."));
}

void UBuildModeManager::EnterMineMode()
{
    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        CurrentToolMode = EBuildToolMode::None;
        CurrentMode = EBuildMode::None;
        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            GridSelection->EndSelectionTool(true);
        }
        DeactivateGridOverlay();
        DestroyMiningLayerRenderer();
        DestroyGhost();
        if (ActiveMiningGhost)
        {
            ActiveMiningGhost->Destroy();
            ActiveMiningGhost = nullptr;
        }
        BroadcastSelectionChanged();
        BroadcastFeedback(TEXT("Mine mode exited."));
        return;
    }

    ExitDigMode();
    ExitBuildMode();

    CurrentToolMode = EBuildToolMode::Mine;
    CurrentMode = EBuildMode::None;

    SelectedBuildingTag = FGameplayTag::EmptyTag;
    SelectedMaterialTag = FGameplayTag::EmptyTag;
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

    SpawnGhost();

    // Spawn 3D hover ghost cube for mine mode.
    // Shows at the hovered voxel while the player selects cells to mine.
    if (UWorld* W = GetWorld())
    {
        TSubclassOf<AMiningGhostActor> ClassToSpawn = MiningGhostClass;
        if (!ClassToSpawn)
        {
            static const TCHAR* CandidateGhostPaths[] =
            {
                TEXT("/Game/WorldForge/BluePrints/BP_MiningGhostActor.BP_MiningGhostActor_C"),
                TEXT("/Game/WorldForge/Blueprints/BP_MiningGhostActor.BP_MiningGhostActor_C"),
                TEXT("/Game/WorldForge/Blueprints/Player/BP_MiningGhostActor.BP_MiningGhostActor_C"),
                TEXT("/Game/WorldForge/Player/BP_MiningGhostActor.BP_MiningGhostActor_C"),
                TEXT("/Game/WorldForge/Build/BP_MiningGhostActor.BP_MiningGhostActor_C"),
                TEXT("/Game/BP_MiningGhostActor.BP_MiningGhostActor_C")
            };

            for (const TCHAR* CandidatePath : CandidateGhostPaths)
            {
                if (UClass* LoadedClass = LoadClass<AMiningGhostActor>(nullptr, CandidatePath))
                {
                    ClassToSpawn = LoadedClass;
                    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Auto-loaded MiningGhostClass: %s"), CandidatePath);
                    break;
                }
            }
        }

        if (!ClassToSpawn)
        {
            ClassToSpawn = AMiningGhostActor::StaticClass();
            UE_LOG(LogTemp, Warning, TEXT("[BuildMode] MiningGhostClass not assigned and BP fallback was not found. Using native AMiningGhostActor."));
        }
        FActorSpawnParameters MineGhostParams;
        MineGhostParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ActiveMiningGhost = W->SpawnActor<AMiningGhostActor>(ClassToSpawn, FTransform::Identity, MineGhostParams);

        if (ActiveMiningGhost)
        {
            UE_LOG(LogTemp, Log, TEXT("[BuildMode] Mining ghost cube spawned."));
        }
    }

    if (ULayerViewSubsystem* LayerView = GetLayerViewSubsystem())
    {
        LayerView->EnterLayerView(EBuildToolMode::Mine);
    }

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetActiveMineSettlementID(GetEffectiveSettlementID());
        GridSelection->SetToggleSelectedCellsOnReselect(bToggleSelectedCellsOnReselect);
        GridSelection->BeginSelectionTool(EBuildToolMode::Mine, EGridSelectionShape::Rectangle);

        if (ActiveMiningGhost)
        {
            const FLinearColor SurfaceTint = ActiveMiningGhost->ConfirmedTint;
            const FLinearColor UndergroundTint = FLinearColor(1.0f, 0.95f, 0.05f, 1.0f);
            GridSelection->SetMiningGhostVisualAssets(
                ActiveMiningGhost->GetResolvedGhostCubeMesh(),
                ActiveMiningGhost->GetResolvedGhostMaterial(),
                SurfaceTint,
                UndergroundTint,
                FMath::Clamp(ActiveMiningGhost->ConfirmedOpacity, 0.45f, 0.80f),
                0.90f,
                0.97f);
        }
    }

    InitializeMiningLayerRenderer();
    UpdateMiningLayerRenderer();

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered Mine mode."));
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    BroadcastFeedback(TEXT("Mine mode enabled. Click a valid block to designate mining."));
}

void UBuildModeManager::EnterDigMode()
{
    if (CurrentToolMode == EBuildToolMode::Dig)
    {
        ExitDigMode();
        return;
    }

    ExitBuildMode();
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->EndSelectionTool(true);
    }
    DeactivateGridOverlay();
    DestroyGhost();

    UWorld* W = GetWorld();
    if (!W)
    {
        CurrentToolMode = EBuildToolMode::None;
        CurrentMode = EBuildMode::None;
        BroadcastFeedback(TEXT("Dig mode failed: world unavailable."));
        return;
    }

    CurrentToolMode = EBuildToolMode::Dig;
    CurrentMode = EBuildMode::None;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    TSubclassOf<ADigGhostActor> ClassToSpawn = DigGhostClass;
    if (!ClassToSpawn)
    {
        ClassToSpawn = ADigGhostActor::StaticClass();
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] DigGhostClass not set. Falling back to ADigGhostActor::StaticClass()."));
    }

    ActiveDigGhost = W->SpawnActor<ADigGhostActor>(
        ClassToSpawn,
        FTransform::Identity,
        Params
    );

    if (ActiveDigGhost)
    {
        ActiveDigGhost->SetGhostState(EDigGhostState::Valid);
        UE_LOG(LogTemp, Log, TEXT("[BuildMode] Dig ghost spawned."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildMode] Failed to spawn dig ghost."));
    }

    bHoveredCellValid = false;
    bHasLastValidVoxel = false;

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered Dig mode."));
    OnBuildModeChanged.Broadcast(CurrentMode);
    BroadcastSelectionChanged();
    BroadcastFeedback(TEXT("Dig mode enabled."));
}

void UBuildModeManager::ExitDigMode()
{
    if (ActiveDigGhost)
    {
        ActiveDigGhost->SetGhostState(EDigGhostState::Hidden);
        ActiveDigGhost->Destroy();
        ActiveDigGhost = nullptr;
    }

    // Clear X-ray state so it doesn't persist across mode switches.
    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->bXRayResourcesActive = false;
    }

    if (CurrentToolMode == EBuildToolMode::Dig)
    {
        CurrentToolMode = EBuildToolMode::None;
    }
}

void UBuildModeManager::ExecuteMineDesignation()
{
    if (CurrentToolMode != EBuildToolMode::Mine)
    {
        return;
    }

    if (!bHoveredCellValid)
    {
        BroadcastFeedback(TEXT("No valid mine target."));
        return;
    }

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetActiveMineSettlementID(GetEffectiveSettlementID());
    }

    UMiningSubsystem* MiningSub = GetMiningSubsystem();
    if (!MiningSub)
    {
        BroadcastFeedback(TEXT("Mining system unavailable."));
        return;
    }

    if (!IsMineVoxelInsidePlayerClaim(CurrentHoveredVoxel))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Mine designation skipped: unclaimed land. Voxel=(%d,%d,%d)"),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z);
        BroadcastFeedback(TEXT("Cannot mine here. This land is not claimed by your colony."));
        return;
    }

    int32 OutDesignationID = INDEX_NONE;

    const bool bCreated = MiningSub->CreateMiningDesignation(
        GetEffectiveSettlementID(),
        CurrentHoveredVoxel,
        0,
        OutDesignationID);

    if (!bCreated || OutDesignationID == INDEX_NONE)
    {
        BroadcastFeedback(TEXT("Mining designation failed."));
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Mining designation failed at voxel (%d,%d,%d)."),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z);
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Mining designation created. ID=%d Voxel=(%d,%d,%d) Settlement=%d"),
        OutDesignationID,
        CurrentHoveredVoxel.X,
        CurrentHoveredVoxel.Y,
        CurrentHoveredVoxel.Z,
        GetEffectiveSettlementID());

    BroadcastFeedback(FString::Printf(
        TEXT("Mining designated at (%d, %d, %d)."),
        CurrentHoveredVoxel.X,
        CurrentHoveredVoxel.Y,
        CurrentHoveredVoxel.Z));
}

void UBuildModeManager::ExecuteDig()
{
    if (CurrentToolMode != EBuildToolMode::Dig)
    {
        return;
    }

    UPlayerBuildContext* Ctx = GetBuildContext();
    if (!Ctx)
    {
        BroadcastFeedback(TEXT("Dig failed: build context unavailable."));
        return;
    }

    if (!Ctx->bDigTargetValid)
    {
        BroadcastFeedback(TEXT("No valid dig target."));
        return;
    }

    if (!Ctx->CanDigNow())
    {
        BroadcastFeedback(TEXT("Dig is on cooldown."));
        return;
    }

    UDigSubsystem* DigSub = GetDigSubsystem();
    if (!DigSub)
    {
        BroadcastFeedback(TEXT("Dig system unavailable."));
        return;
    }

    FDigParams Params;
    Params.TargetGridPos = Ctx->CurrentDigTarget;
    Params.Radius = Ctx->DigSettings.Radius;
    Params.DepthPerAction = Ctx->DigSettings.DepthPerAction;

    if (Ctx->DigSettings.MaxDigDepth > 0)
    {
        const int32 CurrentDepth = DigSub->GetRelativeDepth(Ctx->CurrentDigTarget);
        if (CurrentDepth >= Ctx->DigSettings.MaxDigDepth)
        {
            BroadcastFeedback(TEXT("Maximum dig depth reached."));
            return;
        }
    }

    if (!IsDigVoxelInsidePlayerClaim(Params.TargetGridPos))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Dig blocked: unclaimed land. Voxel=(%d,%d,%d)"),
            Params.TargetGridPos.X,
            Params.TargetGridPos.Y,
            Params.TargetGridPos.Z);
        BroadcastFeedback(TEXT("Cannot dig here. This land is not claimed by your colony."));
        return;
    }

    FDigResult Result = DigSub->DigAt(Params);

    if (!Result.WasEffective())
    {
        BroadcastFeedback(TEXT("Dig had no effect."));
        return;
    }

    // TODO: Replace with real sim tick when exposed here.
    const int64 SimTick = 0;

    Ctx->PushDigUndo(Result, SimTick);
    Ctx->StartDigCooldown();

    UE_LOG(LogTemp, Warning,
        TEXT("[DigTrace][BuildMode] RequestedTarget=%s RemovedBlocks=%d"),
        *Params.TargetGridPos.ToString(),
        Result.RemovedBlocks.Num());

    BroadcastFeedback(FString::Printf(
        TEXT("Dug %d block(s)."),
        Result.RemovedBlocks.Num()
    ));

    if (ActiveDigGhost && Result.RevealedAnyNodes())
    {
        ActiveDigGhost->SetGhostState(EDigGhostState::ResourceDetected);
    }
}

void UBuildModeManager::UndoLastDig()
{
    if (CurrentToolMode != EBuildToolMode::Dig)
    {
        return;
    }

    UPlayerBuildContext* Ctx = GetBuildContext();
    UDigSubsystem* DigSub = GetDigSubsystem();

    if (!Ctx || !DigSub)
    {
        return;
    }

    FDigUndoEntry UndoEntry;
    if (!Ctx->PopDigUndo(UndoEntry))
    {
        BroadcastFeedback(TEXT("Nothing to undo."));
        return;
    }

    const bool bSuccess = DigSub->UndoDig(UndoEntry.DigResult);

    BroadcastFeedback(
        bSuccess
        ? TEXT("Dig undone.")
        : TEXT("Dig undo partially failed.")
    );
}

void UBuildModeManager::TickDigMode(float DeltaTime)
{
    UPlayerBuildContext* Ctx = GetBuildContext();
    if (!Ctx)
    {
        return;
    }

    Ctx->TickDigCooldown(DeltaTime);

    FIntVector GridPos;
    FVector WorldPos;
    const bool bHit = RaycastDigTarget(GridPos, WorldPos);

    if (bHit)
    {
        Ctx->CurrentDigTarget = GridPos;
        Ctx->CurrentDigWorldPosition = WorldPos;

        UDigSubsystem* DigSub = GetDigSubsystem();
        Ctx->bDigTargetValid = DigSub && DigSub->CanDigAt(GridPos);

        if (Ctx->bDigTargetValid && Ctx->DigSettings.MaxDigDepth > 0 && DigSub)
        {
            const int32 Depth = DigSub->GetRelativeDepth(GridPos);
            if (Depth >= Ctx->DigSettings.MaxDigDepth)
            {
                Ctx->bDigTargetValid = false;
            }
        }
    }
    else
    {
        Ctx->bDigTargetValid = false;
    }

    if (ActiveDigGhost)
    {
        if (bHit)
        {
            ActiveDigGhost->UpdateGhost(
                WorldPos,
                GridPos,
                Ctx->DigSettings.Radius,
                Ctx->DigSettings.DepthPerAction
            );

            ActiveDigGhost->SetGhostState(
                Ctx->bDigTargetValid ? EDigGhostState::Valid : EDigGhostState::Invalid
            );
        }
        else
        {
            ActiveDigGhost->SetGhostState(EDigGhostState::Invalid);
        }
    }
}

bool UBuildModeManager::RaycastDigTarget(FIntVector& OutGridPos, FVector& OutWorldPos) const
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PC)
    {
        return false;
    }

    FVector HitLocation = FVector::ZeroVector;
    bool bHitPlacedBlock = false;
    FIntVector SupportVoxel = FIntVector::ZeroValue;
    FVector HitNormal = FVector::UpVector;

    if (!RaycastToTerrain(PC, HitLocation, bHitPlacedBlock, SupportVoxel, HitNormal))
    {
        return false;
    }

    UVoxelWorldManager* VM = GetVoxelManager();
    if (!VM)
    {
        return false;
    }

    if (bHitPlacedBlock)
    {
        OutGridPos = SupportVoxel;
    }
    else
    {
        FIntVector SurfaceVoxel = VM->WorldToGrid3D(HitLocation);

        int32 SurfaceZ = SurfaceVoxel.Z;
        VM->TryGetTerrainSurfaceVoxelZ(
            FIntPoint(SurfaceVoxel.X, SurfaceVoxel.Y),
            SurfaceZ
        );

        OutGridPos = FIntVector(SurfaceVoxel.X, SurfaceVoxel.Y, SurfaceZ);
    }

    OutWorldPos = VM->GridToWorld(FIntPoint(OutGridPos.X, OutGridPos.Y), OutGridPos.Z);
    return true;
}

void UBuildModeManager::UpdateFromMouse(APlayerController* PC)
{
    if (CurrentToolMode == EBuildToolMode::Dig)
    {
        TickDigMode(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f);

        // X-Ray resource outlines: toggle based on Ctrl key state.
        // When Ctrl is held in Dig mode, DigGhostActor renders wireframe
        // sphere outlines at nearby hidden resource node positions.
        if (UPlayerBuildContext* Ctx = GetBuildContext())
        {
            const bool bCtrlHeld = PC
                && (PC->IsInputKeyDown(EKeys::LeftControl)
                    || PC->IsInputKeyDown(EKeys::RightControl));
            Ctx->bXRayResourcesActive = bCtrlHeld;
        }

        return;
    }

    if (!PC)
    {
        return;
    }

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        if (GridSelection->IsSelectionActive())
        {
            GridSelection->TickSelectionFromMouse(PC);

            const bool bUseSharedGridHover =
                CurrentToolMode == EBuildToolMode::Mine ||
                CurrentToolMode == EBuildToolMode::Farm ||
                (CurrentMode == EBuildMode::CustomDraw && CurrentToolMode == EBuildToolMode::Place);

            if (bUseSharedGridHover && GridSelection->HasValidHover())
            {
                const FIntVector NewGridHover = GridSelection->GetHoveredVoxel();
                const bool bChanged = (NewGridHover != CurrentHoveredVoxel) || !bHoveredCellValid;

                CurrentHoveredVoxel = NewGridHover;
                CurrentOverlayVoxel = NewGridHover;
                CurrentHoveredCell = FIntPoint(NewGridHover.X, NewGridHover.Y);
                CurrentHoveredTerrainZ = GridSelection->GetHoveredTerrainZ();
                LastHitZ = CurrentHoveredTerrainZ;
                LastValidVoxel = NewGridHover;
                bHasLastValidVoxel = true;
                bHoveredCellValid = true;

                if (bChanged)
                {
                    OnHoveredCellChanged.Broadcast(CurrentHoveredCell);
                }

                UpdateGhost();
                UpdateGridOverlay();

                // Mine hover visual: always use flat GridSelection tile only.
                // The 3D cube ghost is hidden; the GridSelection hover highlight
                // (single cell flat tile) covers the visual need at both surface
                // and underground. The ghost actor stays alive for internal state
                // tracking but its mesh is never shown.
                if (CurrentToolMode == EBuildToolMode::Mine && ActiveMiningGhost)
                {
                    ActiveMiningGhost->HideGhost();
                }

                if (CurrentToolMode == EBuildToolMode::Mine)
                {
                    UpdateMiningLayerRenderer();
                }

                // For shared grid tools, GridSelectionSubsystem is now the authoritative
                // X/Y + tool-layer Z hover source. Returning here prevents legacy terrain
                // hover logic from snapping the target back to surface Z.
                return;
            }
        }
    }

    // ── PRIORITY 2 FIX: mine mode fallback ──────────────────────────────────
    //
    // This path is reached only when GridSelectionSubsystem either:
    //   (a) has no active selection tool (shouldn't happen — EnterMineMode calls
    //       BeginSelectionTool — but defended here for safety), or
    //   (b) TickSelectionFromMouse ran but returned HasValidHover()=false (cursor
    //       is at the map edge, the synthetic layer plane didn't intersect, etc.).
    //
    // Previously this fell back to RaycastDigTarget → RaycastToTerrain (SDF
    // geometry collision). Underground, that collision returns whichever terrain
    // polygon is still visible through the cutaway — typically the clipped surface
    // face at the wrong Z — so the hover voxel slipped to the terrain surface
    // instead of staying on the active layer plane. Clicks then designated the
    // wrong layer.
    //
    // Correct behaviour: if GridSelection (which owns the authoritative layer-plane
    // projection) says "no valid hover," clear hover state and hide the ghost.
    // Do NOT override with terrain collision. The ghost reappears the instant the
    // cursor moves back over a position where the layer plane can be resolved.
    // ─────────────────────────────────────────────────────────────────────────
    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        bHoveredCellValid = false;

        if (ActiveMiningGhost)
        {
            ActiveMiningGhost->HideGhost();
        }

        if (GhostActor)
        {
            GhostActor->SetGhostState(EBuildGhostState::Invalid);
        }

        UpdateGridOverlay();
        UpdateMiningLayerRenderer();
        return;
    }

    if (CurrentMode == EBuildMode::None)
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
            UpdateGridOverlay();
        }
        else
        {
            bHoveredCellValid = false;

            if (GhostActor)
            {
                GhostActor->SetGhostState(EBuildGhostState::Invalid);
                UpdateGhost();
            }
            UpdateGridOverlay();
        }
        return;
    }

    const FIntPoint TerrainCell = VM->WorldToTerrainCell(HitLocation);

    // True voxel-space coordinate for the visual grid overlay.
    CurrentOverlayVoxel = WorldToVoxel(HitLocation, 100.0f);
    CurrentHoveredTerrainZ = HitLocation.Z;

    // Existing gameplay coordinate path.
    // Keep this because farming/building currently depend on terrain-cell X/Y.
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
    CurrentHoveredTerrainZ = HitLocation.Z;

    if (bChanged)
    {
        OnHoveredCellChanged.Broadcast(CurrentHoveredCell);
        UpdateGhost();
        UpdateGridOverlay();
    }

    if (!bIsDraggingBuild || CurrentMode != EBuildMode::CustomDraw || !bHasDragStart)
    {
        return;
    }

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        if (GridSelection->IsDraggingSelection())
        {
            // New grid system owns drag preview. Legacy per-frame line rebuild is disabled.
            return;
        }
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

        const int32 Steps = FMath::Max3(FMath::Abs(Dx), FMath::Abs(Dy), FMath::Abs(Dz));

        for (int32 Index = 0; Index <= Steps; ++Index)
        {
            const float Alpha = (Steps == 0) ? 0.0f : static_cast<float>(Index) / static_cast<float>(Steps);

            FIntVector LineVoxel;
            LineVoxel.X = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.X), static_cast<float>(EndVoxel.X), Alpha));
            LineVoxel.Y = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.Y), static_cast<float>(EndVoxel.Y), Alpha));
            LineVoxel.Z = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DragStartVoxel.Z), static_cast<float>(EndVoxel.Z), Alpha));

            if (LineCells3D.Contains(LineVoxel) || HasPlacedBlockAtVoxel(World, VM, LineVoxel))
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
        const int32 Steps = FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy));

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

                if (!LineCells3D.Contains(FirstVoxel) && !HasPlacedBlockAtVoxel(World, VM, FirstVoxel))
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

                    if (LineCells3D.Contains(BridgeVoxel) || HasPlacedBlockAtVoxel(World, VM, BridgeVoxel))
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

            if (!LineCells3D.Contains(CurrentVoxel) && !HasPlacedBlockAtVoxel(World, VM, CurrentVoxel))
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

    if (CurrentToolMode == EBuildToolMode::Farm)
    {
        FarmPaintedWorldCells.Reset();
        for (const FIntPoint& Cell : SelectedCells)
        {
            FarmPaintedWorldCells.Add(Cell);
        }
    }

    LastPlacedDragVoxel = CurrentHoveredVoxel;

    // Batch-sync the overlay with the rebuilt drag selection.
    //if (GridOverlay)
    //{
       // TMap<FIntVector, float> BatchCells;
       // UVoxelWorldManager* OverlayVM = GetVoxelManager();

       // for (const FIntVector& Cell : SelectedCells3D)
       // {
         //   FIntVector OverlayCell = Cell;
          //  float CellZ = CurrentHoveredTerrainZ;

          //  if (OverlayVM)
          //  {
                // SelectedCells3D stores legacy gameplay cells for build/farm
                // compatibility. Convert those cells to world space, then back
                // to true voxel-space for TileGridOverlayActor rendering.
              //  const FVector CellWorld = OverlayVM->GridToWorld(FIntPoint(Cell.X, Cell.Y), Cell.Z);
              //  OverlayCell = WorldToVoxel(CellWorld, 100.0f);
               // CellZ = CellWorld.Z;
           // }

          //  BatchCells.Add(OverlayCell, CellZ);
        //}

        //GridOverlay->SetSelectedCells(BatchCells);
    //}

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
        for (const FIntPoint& Cell : SelectedCells)
        {
            Ctx->AddCell(Cell);
        }
    }

    OnCellCountChanged.Broadcast(
        CurrentToolMode == EBuildToolMode::Farm
        ? FarmPaintedWorldCells.Num()
        : SelectedCells.Num());
    UpdateGhost();
    UpdateGridOverlay();
}

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
    const FVector TraceEnd = WorldLocation + (WorldDirection * 5000000.0f);

    FCollisionQueryParams TerrainParams(SCENE_QUERY_STAT(BuildModeTerrainTrace), false);
    TerrainParams.bTraceComplex = false;

    if (GhostActor)
    {
        TerrainParams.AddIgnoredActor(GhostActor);
    }

    if (ActiveDigGhost)
    {
        TerrainParams.AddIgnoredActor(ActiveDigGhost);
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

    if (GhostActor)
    {
        FullParams.AddIgnoredActor(GhostActor);
    }

    if (ActiveDigGhost)
    {
        FullParams.AddIgnoredActor(ActiveDigGhost);
    }

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

bool UBuildModeManager::CommitPrefabBuild()
{
    const int32 EffectiveSettlementID = GetEffectiveSettlementID();

    if (!bHoveredCellValid || EffectiveSettlementID < 0)
    {
        BroadcastFeedback(TEXT("Cannot place building here."));
        return false;
    }

    if (!SelectedBuildingTag.IsValid())
    {
        BroadcastFeedback(TEXT("No building selected."));
        return false;
    }

    FString TerrainFailure;
    if (!ValidateGroundTerrainPlacement(CurrentHoveredVoxel, TerrainFailure))
    {
        BroadcastFeedback(TerrainFailure);
        return false;
    }

    if (!IsBuildVoxelInsidePlayerClaim(CurrentHoveredVoxel))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[BuildMode] Build blocked: unclaimed land. Voxel=(%d,%d,%d)"),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z
        );
        BroadcastFeedback(TEXT("Cannot build here. This land is not claimed by your colony."));
        return false;
    }

    UPlayerBuildContext* Ctx = GetBuildContext();
    if (!Ctx)
    {
        BroadcastFeedback(TEXT("Build context unavailable."));
        return false;
    }

    Ctx->SetActiveSettlement(EffectiveSettlementID);

    const bool bSuccess = Ctx->CommitPrefabBuildByTagWithRotation3D(
        SelectedBuildingTag,
        CurrentHoveredVoxel,
        RotationIndex,
        BuildAgentID
    );

    BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    return bSuccess;
}

bool UBuildModeManager::CommitFarmZone()
{
    if (CurrentToolMode != EBuildToolMode::Farm)
    {
        return false;
    }

    USettlementManager* SettlementManager = GetSettlementManager();
    if (!SettlementManager)
    {
        BroadcastFeedback(TEXT("Settlement system unavailable."));
        return false;
    }

    const int32 SettlementID = GetEffectiveSettlementID();
    const FSettlementState* Settlement = SettlementManager->GetSettlement(SettlementID);

    if (!Settlement)
    {
        BroadcastFeedback(TEXT("No active settlement found."));
        return false;
    }

    // Long-term exact-cell farming path.
    //
    // Important: Do NOT mix raw SelectedCells3D voxel coordinates with farm/world cells.
    // That old mixed-coordinate path could turn a handful of selected cells into
    // a massive bounding rectangle, e.g. 9 selected cells becoming 57k farm tiles.
    // FarmPaintedWorldCells is the only accepted farm/world-cell source here.
    TSet<FIntPoint> RequestedWorldCells;
    for (const FIntPoint& WorldCell : FarmPaintedWorldCells)
    {
        RequestedWorldCells.Add(WorldCell);
    }

    if (RequestedWorldCells.Num() == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[BuildMode] CommitFarmZone failed: no exact farm cells. FarmPaintedWorldCells=%d SelectedCells=%d SelectedCells3D=%d"),
            FarmPaintedWorldCells.Num(),
            SelectedCells.Num(),
            SelectedCells3D.Num());

        BroadcastFeedback(TEXT("No farm tiles selected."));
        return false;
    }

    TSet<FIntPoint> ClaimedWorldCells;
    ClaimedWorldCells.Reserve(RequestedWorldCells.Num());

    int32 SkippedUnclaimedCount = 0;
    for (const FIntPoint& WorldCell : RequestedWorldCells)
    {
        if (IsFarmCellInsidePlayerClaim(WorldCell))
        {
            ClaimedWorldCells.Add(WorldCell);
            continue;
        }

        ++SkippedUnclaimedCount;
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Farm cell skipped: unclaimed land. WorldCell=(%d,%d)"),
            WorldCell.X,
            WorldCell.Y);
    }

    if (ClaimedWorldCells.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Farm zone blocked: no selected cells inside claimed land."));
        BroadcastFeedback(TEXT("Cannot farm here. This land is not claimed by your colony."));
        return false;
    }

    if (SkippedUnclaimedCount > 0)
    {
        FarmPaintedWorldCells = ClaimedWorldCells;

        SelectedCells.RemoveAll([&ClaimedWorldCells](const FIntPoint& Cell)
            {
                return !ClaimedWorldCells.Contains(Cell);
            });

        SelectedCells3D.RemoveAll([&ClaimedWorldCells](const FIntVector& Voxel)
            {
                return !ClaimedWorldCells.Contains(FIntPoint(Voxel.X, Voxel.Y));
            });

        if (UPlayerBuildContext* Ctx = GetBuildContext())
        {
            Ctx->ClearSelection();
            for (const FIntPoint& Cell : FarmPaintedWorldCells)
            {
                Ctx->AddCell(Cell);
            }
        }

        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            TSet<FIntVector> ClaimedSelectionVoxels;
            ClaimedSelectionVoxels.Reserve(SelectedCells3D.Num());
            for (const FIntVector& Voxel : SelectedCells3D)
            {
                ClaimedSelectionVoxels.Add(Voxel);
            }

            if (ClaimedSelectionVoxels.Num() > 0)
            {
                GridSelection->RetainSelectedCells(ClaimedSelectionVoxels);
            }
        }

        OnCellCountChanged.Broadcast(FarmPaintedWorldCells.Num());
    }

    // FarmPaintedWorldCells stores exact absolute grid/voxel XY cells.
    // Settlement->WorldOrigin is a terrain/world-map cell, so convert that
    // origin to the same exact grid space before producing settlement-local
    // farm cells. This keeps farm jobs near local coordinates instead of
    // large absolute voxel values.
    FIntPoint FarmGridOrigin = Settlement->WorldOrigin;
    if (UVoxelWorldManager* VM = GetVoxelManager())
    {
        const FVector OriginWorld = VM->TerrainCellToWorld(Settlement->WorldOrigin, 0.0f, true);
        const FIntVector OriginVoxel = WorldToVoxel(OriginWorld, 100.0f);
        FarmGridOrigin = FIntPoint(OriginVoxel.X, OriginVoxel.Y);
    }

    TSet<FIntPoint> UniqueLocalCells;
    for (const FIntPoint& WorldCell : ClaimedWorldCells)
    {
        UniqueLocalCells.Add(WorldCell - FarmGridOrigin);
    }

    if (UniqueLocalCells.Num() == 0)
    {
        BroadcastFeedback(TEXT("No valid farm tiles selected."));
        return false;
    }

    UFarmingSubsystem* FarmingSub = GetFarmingSubsystem();
    if (!FarmingSub)
    {
        BroadcastFeedback(TEXT("Farming system unavailable."));
        return false;
    }

    const ECropCategory Category = ECropCategory::Grains;
    const FName CropRow = FName(TEXT("LP_Wheat"));

    const int32 ZoneID = FarmingSub->CreateFarmZoneFromCells(
        SettlementID,
        FarmGridOrigin,
        UniqueLocalCells,
        Category,
        CropRow,
        false);

    if (ZoneID == INDEX_NONE)
    {
        BroadcastFeedback(TEXT("Farm zone creation failed."));
        return false;
    }

    int32 MinX = TNumericLimits<int32>::Max();
    int32 MinY = TNumericLimits<int32>::Max();
    int32 MaxX = TNumericLimits<int32>::Min();
    int32 MaxY = TNumericLimits<int32>::Min();

    for (const FIntPoint& LocalCell : UniqueLocalCells)
    {
        MinX = FMath::Min(MinX, LocalCell.X);
        MinY = FMath::Min(MinY, LocalCell.Y);
        MaxX = FMath::Max(MaxX, LocalCell.X);
        MaxY = FMath::Max(MaxY, LocalCell.Y);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Exact farm zone committed. ZoneID=%d ExactCells=%d Requested=%d SkippedUnclaimed=%d BoundsLocal=(%d,%d)->(%d,%d) FarmGridOrigin=(%d,%d) Crop=%s Settlement=%d"),
        ZoneID,
        UniqueLocalCells.Num(),
        RequestedWorldCells.Num(),
        SkippedUnclaimedCount,
        MinX,
        MinY,
        MaxX,
        MaxY,
        FarmGridOrigin.X,
        FarmGridOrigin.Y,
        *CropRow.ToString(),
        SettlementID);

    // Do not clear BuildMode/GridSelection state here. HandleConfirm() must first
    // copy the active grid selection into the persistent confirmed overlay layer.
    // Cleanup happens in HandleConfirm after CommitSelectedCellsToConfirmedOverlay().

    BroadcastFeedback(FString::Printf(
        TEXT("Farm zone created: %d exact tile(s) wheat field. Auto-replant: OFF."),
        UniqueLocalCells.Num()));

    return true;
}

bool UBuildModeManager::CommitCustomBuild()
{
    if (SelectedCells3D.Num() == 0)
    {
        BroadcastFeedback(TEXT("Select at least one cell first."));
        return false;
    }

    const int32 EffectiveSettlementID = GetEffectiveSettlementID();

    if (EffectiveSettlementID < 0)
    {
        BroadcastFeedback(TEXT("No active settlement selected."));
        return false;
    }

    for (const FIntVector& Cell : SelectedCells3D)
    {
        FString TerrainFailure;
        if (!ValidateGroundTerrainPlacement(Cell, TerrainFailure))
        {
            BroadcastFeedback(TerrainFailure);
            return false;
        }

        if (!IsBuildVoxelInsidePlayerClaim(Cell))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[BuildMode] Build blocked: unclaimed land. Voxel=(%d,%d,%d) SelectedCells=%d"),
                Cell.X,
                Cell.Y,
                Cell.Z,
                SelectedCells3D.Num()
            );
            BroadcastFeedback(TEXT("Cannot build here. This land is not claimed by your colony."));
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
        SelectedCells.Empty();
        SelectedCells3D.Empty();
        OnCellCountChanged.Broadcast(0);
    }

    BroadcastFeedback(Ctx->GetLastCommitResultMessage());
    return bSuccess;
}

void UBuildModeManager::SpawnGhost()
{
    DestroyGhost();

    UWorld* W = GetWorld();
    if (!W)
    {
        return;
    }

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

    const bool bUsesGridOverlayCursor =
        CurrentMode == EBuildMode::CustomDraw ||
        CurrentToolMode == EBuildToolMode::Farm ||
        CurrentToolMode == EBuildToolMode::Mine;

    // IMPORTANT:
    // CustomDraw/Farm/Mine now use TileGridOverlayActor for their cell visuals.
    // The old BuildGhostActor custom-draw path renders full cube placeholders,
    // which is the giant gray block grid seen in PIE. Keep the ghost hidden for
    // grid tools so the flat overlay is the only selection feedback.
    if (bUsesGridOverlayCursor && CurrentMode != EBuildMode::Prefab)
    {
        GhostActor->ClearAllHighlights();
        GhostActor->SetGhostState(EBuildGhostState::Hidden);
        return;
    }

    if (!bHoveredCellValid)
    {
        GhostActor->SetGhostState(EBuildGhostState::Invalid);

        if (CurrentMode == EBuildMode::Prefab)
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
}



void UBuildModeManager::InitializeMiningLayerRenderer()
{
    if (MiningLayerRenderer)
    {
        return;
    }

    UWorld* W = GetWorld();
    if (!W)
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    MiningLayerRenderer = W->SpawnActor<AMiningLayerRenderer>(
        AMiningLayerRenderer::StaticClass(),
        FTransform::Identity,
        Params);

    if (MiningLayerRenderer)
    {
        MiningLayerRenderer->SetCellSizeUU(100.0f);
        MiningLayerRenderer->SetRendererVisible(false);
        UE_LOG(LogTemp, Log, TEXT("[BuildMode] MiningLayerRenderer spawned."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildMode] Failed to spawn MiningLayerRenderer."));
    }
}

void UBuildModeManager::DestroyMiningLayerRenderer()
{
    RestoreMiningPresentationVisibility();

    if (MiningLayerRenderer)
    {
        MiningLayerRenderer->Destroy();
        MiningLayerRenderer = nullptr;
    }

    bMiningLayerAnchorValid = false;
    MiningLayerAnchorVoxel = FIntVector::ZeroValue;
    MiningLayerAnchorZ = MIN_int32;
}

void UBuildModeManager::UpdateMiningLayerRenderer()
{
    // Camera bounds are clamped every call regardless of state-change deduplication.
    // This prevents the player from panning outside the diorama walls and seeing the
    // underlying grass terrain that bleeds through the clip plane.
    if (CurrentToolMode == EBuildToolMode::Mine && bMiningLayerAnchorValid)
    {
        ClampCameraToMineRegion();
    }

    if (CurrentToolMode != EBuildToolMode::Mine)
    {
        if (MiningLayerRenderer)
        {
            MiningLayerRenderer->ClearLayer();
        }

        RestoreMiningPresentationVisibility();

        bMiningLayerAnchorValid = false;
        MiningLayerAnchorVoxel = FIntVector::ZeroValue;
        MiningLayerAnchorZ = MIN_int32;
        return;
    }

    InitializeMiningLayerRenderer();
    if (!MiningLayerRenderer)
    {
        return;
    }

    if (!bHoveredCellValid && !bHasLastValidVoxel && !bMiningLayerAnchorValid)
    {
        MiningLayerRenderer->ClearLayer();
        return;
    }

    const int32 ActiveZ = GetActiveVoxelLayerZ();

    // ─── Anchor policy ───────────────────────────────────────────────────────────
    //
    // The anchor is SET once when first going underground (or when the layer
    // changes) using the current hover position as the center. After that it
    // is FROZEN — it only moves when the dynamic radius expansion logic (below)
    // determines confirmed mine cells have grown beyond the wall edge.
    //
    // Removing per-frame cursor drift eliminates the two main visual problems:
    //   1. Flickering: every anchor move triggered ForceNextRebuild → full HISM
    //      clear+rebuild every frame while the cursor was near the edge.
    //   2. Box jitter: anchor visibly sliding around while just hovering.
    //
    // If the player wants to mine in a completely different area they should exit
    // and re-enter mine mode, which re-anchors to the new cursor position.
    // ─────────────────────────────────────────────────────────────────────────────
    const int32 DioramaRadiusCells = 10;
    const bool bLayerChanged = !bMiningLayerAnchorValid || MiningLayerAnchorZ != ActiveZ;

    if (bLayerChanged || !bMiningLayerAnchorValid)
    {
        // Set anchor from current cursor position on initial entry or layer change.
        FIntVector NewAnchor = bHoveredCellValid
            ? CurrentHoveredVoxel
            : (bHasLastValidVoxel ? LastValidVoxel : MiningLayerAnchorVoxel);
        NewAnchor.Z = ActiveZ;

        MiningLayerAnchorVoxel = NewAnchor;
        MiningLayerAnchorZ     = ActiveZ;
        bMiningLayerAnchorValid = true;

        if (MiningLayerRenderer)
        {
            MiningLayerRenderer->ForceNextRebuild();
        }

        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Mining diorama anchor SET. Anchor=(%d,%d,%d) ActiveZ=%d Reason=%s"),
            MiningLayerAnchorVoxel.X, MiningLayerAnchorVoxel.Y, MiningLayerAnchorVoxel.Z,
            ActiveZ,
            !bMiningLayerAnchorValid ? TEXT("InitialEntry") : TEXT("LayerChanged"));
    }
    // Anchor does NOT drift with cursor hover — only dynamic radius expansion moves the wall.

    FIntVector CenterVoxel = MiningLayerAnchorVoxel;
    CenterVoxel.Z = ActiveZ;

    TArray<FIntVector> ConfirmedMineCells;
    if (UMiningSubsystem* Mining = GetMiningSubsystem())
    {
        TArray<FMiningDesignation> Designations;
        Mining->GetAllDesignations(GetEffectiveSettlementID(), Designations);
        for (const FMiningDesignation& Designation : Designations)
        {
            if (Designation.State == EMiningDesignationState::Cancelled ||
                Designation.State == EMiningDesignationState::Complete ||
                Designation.State == EMiningDesignationState::Failed)
            {
                continue;
            }
            ConfirmedMineCells.AddUnique(Designation.GridPosition);
        }
    }

    // ─── Dynamic radius expansion ────────────────────────────────────────────────
    //
    // If a confirmed (active) mine designation sits within EdgeBufferCells of the
    // current diorama edge, expand VisibleRadius to keep all designations at least
    // 5 voxels inside the wall. This preserves the diorama illusion when the player
    // is mining outward from the initial area.
    //
    // Expansion cap: DioramaRadiusCells + 20 to prevent unbounded growth.
    // ─────────────────────────────────────────────────────────────────────────────
    if (MiningLayerRenderer && ConfirmedMineCells.Num() > 0)
    {
        const int32 BaseRadius  = DioramaRadiusCells;
        const int32 MaxRadius   = BaseRadius + 20;
        const int32 ExpandBuffer = 5; // keep designations this far inside the wall

        int32 DesiredRadius = MiningLayerRenderer->VisibleRadius;

        for (const FIntVector& Cell : ConfirmedMineCells)
        {
            if (Cell.Z != ActiveZ) { continue; }
            const int32 DX = FMath::Abs(Cell.X - MiningLayerAnchorVoxel.X);
            const int32 DY = FMath::Abs(Cell.Y - MiningLayerAnchorVoxel.Y);
            const int32 MaxDist = FMath::Max(DX, DY);
            // If this designation would be within ExpandBuffer of the wall, push the wall out
            if (MaxDist >= DesiredRadius - ExpandBuffer)
            {
                DesiredRadius = MaxDist + ExpandBuffer;
            }
        }

        DesiredRadius = FMath::Clamp(DesiredRadius, BaseRadius, MaxRadius);

        if (MiningLayerRenderer->VisibleRadius != DesiredRadius)
        {
            MiningLayerRenderer->VisibleRadius = DesiredRadius;
            MiningLayerRenderer->ForceNextRebuild();

            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] Diorama radius expanded to %d (base=%d) due to confirmed mine near edge."),
                DesiredRadius, BaseRadius);
        }
    }

    // ─── End dynamic expansion ───────────────────────────────────────────────────

    // ─── Anchor nudge for lateral mining ────────────────────────────────────────
    //
    // Radius expansion above handles outward mining in all directions from center.
    // But when the player mines laterally (one direction only), the anchor stays
    // fixed and the far edge stays at radius+20 while the near edge drifts away.
    // This nudge slides the anchor toward confirmed mine cells that are within 2
    // voxels of the current wall edge, keeping the work area centered.
    //
    // Key difference from the old cursor-based drift we removed: this only fires
    // when confirmed designations change, not every hover tick — no flicker.
    // ─────────────────────────────────────────────────────────────────────────────
    if (bMiningLayerAnchorValid && ConfirmedMineCells.Num() > 0)
    {
        const int32 CurrentRadius = MiningLayerRenderer
            ? MiningLayerRenderer->VisibleRadius
            : DioramaRadiusCells;

        // Nudge when a confirmed cell is within 2 voxels of the wall edge.
        const int32 NudgeTrigger = CurrentRadius - 2;

        FIntVector NudgeOffset = FIntVector::ZeroValue;

        for (const FIntVector& Cell : ConfirmedMineCells)
        {
            if (Cell.Z != ActiveZ) { continue; }
            const int32 DX = Cell.X - MiningLayerAnchorVoxel.X;
            const int32 DY = Cell.Y - MiningLayerAnchorVoxel.Y;

            // X axis
            if      (DX >  NudgeTrigger) NudgeOffset.X = FMath::Max(NudgeOffset.X,  DX - NudgeTrigger);
            else if (DX < -NudgeTrigger) NudgeOffset.X = FMath::Min(NudgeOffset.X,  DX + NudgeTrigger);

            // Y axis
            if      (DY >  NudgeTrigger) NudgeOffset.Y = FMath::Max(NudgeOffset.Y,  DY - NudgeTrigger);
            else if (DY < -NudgeTrigger) NudgeOffset.Y = FMath::Min(NudgeOffset.Y,  DY + NudgeTrigger);
        }

        if (NudgeOffset.X != 0 || NudgeOffset.Y != 0)
        {
            MiningLayerAnchorVoxel.X += NudgeOffset.X;
            MiningLayerAnchorVoxel.Y += NudgeOffset.Y;

            // CenterVoxel is computed from anchor before this block — re-sync it.
            CenterVoxel.X = MiningLayerAnchorVoxel.X;
            CenterVoxel.Y = MiningLayerAnchorVoxel.Y;

            if (MiningLayerRenderer)
            {
                MiningLayerRenderer->ForceNextRebuild();
            }

            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] Diorama anchor nudged (%+d,%+d) for lateral mining. "
                     "NewAnchor=(%d,%d,%d) Radius=%d"),
                NudgeOffset.X, NudgeOffset.Y,
                MiningLayerAnchorVoxel.X, MiningLayerAnchorVoxel.Y, MiningLayerAnchorVoxel.Z,
                CurrentRadius);
        }
    }

    // ─── End anchor nudge ────────────────────────────────────────────────────────

    // Selection preview is owned by TileGridOverlayActor:
    //   surface     = flat marker tiles
    //   underground = M_WF_MiningGhost cube instances
    // MiningLayerRenderer should only draw the underground cap/floor and confirmed designations.
    const bool bUnderground = GetLayerViewSubsystem() && GetLayerViewSubsystem()->GetRelativeLayerOffset() < 0;
    UpdateMiningPresentationVisibility(CenterVoxel, ActiveZ, bUnderground);

    const TArray<FIntVector> NoSelectedPreviewCells;
    MiningLayerRenderer->SetLayerState(
        CenterVoxel,
        ActiveZ,
        NoSelectedPreviewCells,
        ConfirmedMineCells);
}

void UBuildModeManager::ClampCameraToMineRegion()
{
    if (!MiningLayerRenderer || !bMiningLayerAnchorValid) { return; }

    UWorld* World = GetWorld();
    if (!World) { return; }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) { return; }

    APawn* CamPawn = PC->GetPawn();
    if (!CamPawn) { return; }

    // Allow the camera pawn to sit at most VisibleRadius/2+1 voxels from the anchor
    // in XY.  The tighter radius ensures that even at max pitch -45° the spring arm
    // tip (camera eye) stays inside or at most in the extended dark-earth floor ring,
    // never in the visible grass terrain beyond radius 25.
    // Z is unconstrained — pitch/zoom govern vertical perspective.
    const float CellSizeUU = 100.0f;
    const float HalfBound  = static_cast<float>(MiningLayerRenderer->VisibleRadius / 2 + 1) * CellSizeUU;

    const float AnchorWorldX = (static_cast<float>(MiningLayerAnchorVoxel.X) + 0.5f) * CellSizeUU;
    const float AnchorWorldY = (static_cast<float>(MiningLayerAnchorVoxel.Y) + 0.5f) * CellSizeUU;

    FVector Loc = CamPawn->GetActorLocation();
    const float ClampedX = FMath::Clamp(Loc.X, AnchorWorldX - HalfBound, AnchorWorldX + HalfBound);
    const float ClampedY = FMath::Clamp(Loc.Y, AnchorWorldY - HalfBound, AnchorWorldY + HalfBound);

    if (FMath::Abs(ClampedX - Loc.X) > 0.5f || FMath::Abs(ClampedY - Loc.Y) > 0.5f)
    {
        Loc.X = ClampedX;
        Loc.Y = ClampedY;
        CamPawn->SetActorLocation(Loc, false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void UBuildModeManager::InitializeGridOverlay()
{
    if (GridOverlay)
    {
        return; // Already alive — no-op.
    }

    UWorld* W = GetWorld();
    if (!W)
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    GridOverlay = W->SpawnActor<ATileGridOverlayActor>(
        ATileGridOverlayActor::StaticClass(),
        FTransform::Identity,
        Params);

    if (!GridOverlay)
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildMode] Failed to spawn TileGridOverlayActor."));
        return;
    }

    // ── Register per-mode styles ──

    // Build / CustomDraw / Prefab — blue + hammer
    {
        FGridOverlayStyle Style;
        Style.TileColor        = FLinearColor(0.15f, 0.4f, 0.9f, 0.30f);
        Style.BorderColor      = FLinearColor(0.3f,  0.6f, 1.0f, 0.75f);
        Style.HoverColor       = FLinearColor(0.5f,  0.7f, 1.0f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.4f,  0.5f, 0.9f, 0.20f);
        Style.IconTexture      = BuildIconTexture;
        Style.IconScale        = 0.45f;
        GridOverlay->RegisterModeStyle(EBuildToolMode::Place, Style);
    }

    // Farm — green + wheat
    {
        FGridOverlayStyle Style;
        Style.TileColor        = FLinearColor(0.2f,  0.7f, 0.15f, 0.30f);
        Style.BorderColor      = FLinearColor(0.3f,  0.85f, 0.2f, 0.75f);
        Style.HoverColor       = FLinearColor(0.4f,  0.9f,  0.3f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.3f,  0.8f,  0.2f, 0.20f);
        Style.IconTexture      = FarmIconTexture;
        Style.IconScale        = 0.45f;
        GridOverlay->RegisterModeStyle(EBuildToolMode::Farm, Style);
    }

    // Mine — grey + pickaxe
    {
        FGridOverlayStyle Style;
        Style.TileColor        = FLinearColor(0.5f,  0.5f, 0.5f, 0.30f);
        Style.BorderColor      = FLinearColor(0.65f, 0.65f, 0.65f, 0.75f);
        Style.HoverColor       = FLinearColor(0.8f,  0.8f, 0.7f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.6f,  0.6f, 0.5f, 0.20f);
        Style.IconTexture      = MineIconTexture;
        Style.IconScale        = 0.40f;
        GridOverlay->RegisterModeStyle(EBuildToolMode::Mine, Style);
    }

    // Dig — orange, no icon (dig ghost handles its own visuals)
    {
        FGridOverlayStyle Style;
        Style.TileColor        = FLinearColor(0.9f, 0.6f, 0.1f, 0.30f);
        Style.BorderColor      = FLinearColor(1.0f, 0.7f, 0.2f, 0.75f);
        Style.HoverColor       = FLinearColor(1.0f, 0.8f, 0.3f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.9f, 0.7f, 0.2f, 0.20f);
        Style.IconTexture      = PrefabIconTexture; // optional, nullptr OK
        Style.IconScale        = 0.0f;
        GridOverlay->RegisterModeStyle(EBuildToolMode::Dig, Style);
    }

    // Start hidden — will be shown when a mode is entered.
    GridOverlay->HideOverlay();

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] GridOverlay spawned and styles registered."));
}

void UBuildModeManager::DestroyGridOverlay()
{
    if (GridOverlay)
    {
        GridOverlay->Destroy();
        GridOverlay = nullptr;
    }
}

void UBuildModeManager::UpdateGridOverlay()
{
    if (!GridOverlay)
    {
        return;
    }

    // Push hover state.
    if (bHoveredCellValid)
    {
        GridOverlay->SetHoveredCell(CurrentOverlayVoxel, CurrentHoveredTerrainZ);
    }
    else
    {
        GridOverlay->ClearHover();
    }
}

void UBuildModeManager::ActivateGridOverlayForMode(EBuildToolMode ToolMode)
{
    if (!GridOverlay)
    {
        InitializeGridOverlay();
    }

    if (!GridOverlay)
    {
        return;
    }

    GridOverlay->ClearSelection();
    GridOverlay->ClearDragPreview();
    GridOverlay->ClearHover();
    GridOverlay->SetActiveMode(ToolMode);
    GridOverlay->ShowOverlay();
}


void UBuildModeManager::UpdateMiningPresentationVisibility(const FIntVector& CenterVoxel, int32 ActiveVoxelZ, bool bUnderground)
{
    if (!bUnderground)
    {
        RestoreMiningPresentationVisibility();
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const float CellSize = 100.0f;
    const float CenterWorldX = (static_cast<float>(CenterVoxel.X) + 0.5f) * CellSize;
    const float CenterWorldY = (static_cast<float>(CenterVoxel.Y) + 0.5f) * CellSize;
    const float ActiveWorldZ = static_cast<float>(ActiveVoxelZ) * CellSize;
    const float VisibleRadiusUU = 1350.0f; // 20x20 workspace plus a small wall/camera buffer.
    const float AboveLayerBufferUU = 250.0f;

    TSet<TWeakObjectPtr<AActor>> StillHidden;

    for (TActorIterator<AWorldForgeColonist> It(World); It; ++It)
    {
        AWorldForgeColonist* Colonist = *It;
        if (!IsValid(Colonist))
        {
            continue;
        }

        const FVector Loc = Colonist->GetActorLocation();
        const bool bOutsideXY =
            FMath::Abs(Loc.X - CenterWorldX) > VisibleRadiusUU ||
            FMath::Abs(Loc.Y - CenterWorldY) > VisibleRadiusUU;
        const bool bAboveSlice = Loc.Z > (ActiveWorldZ + AboveLayerBufferUU);
        const bool bShouldHide = bOutsideXY || bAboveSlice;

        if (bShouldHide)
        {
            Colonist->SetActorHiddenInGame(true);
            MiningPresentationHiddenActors.Add(Colonist);
            StillHidden.Add(Colonist);
        }
        else if (MiningPresentationHiddenActors.Contains(Colonist))
        {
            Colonist->SetActorHiddenInGame(false);
        }
    }

    // ─── Surface resource node actors (AWorldResourceActor base — catches BP_MetalOreNode etc.) ──
    //
    // These are the ShatterStone-driven ore node BPs spawned at or near the terrain surface.
    // They are not clipped by ViewLayerClipZ (which only applies to terrain materials), so they
    // float visibly above the underground cut. Hide any node that is above the active slice or
    // outside the diorama XY footprint.
    for (TActorIterator<AWorldResourceActor> It(World); It; ++It)
    {
        AWorldResourceActor* ResourceActor = *It;
        if (!IsValid(ResourceActor))
        {
            continue;
        }

        const FVector Loc = ResourceActor->GetActorLocation();
        const bool bOutsideXY =
            FMath::Abs(Loc.X - CenterWorldX) > VisibleRadiusUU ||
            FMath::Abs(Loc.Y - CenterWorldY) > VisibleRadiusUU;
        const bool bAboveSlice = Loc.Z > (ActiveWorldZ + AboveLayerBufferUU);
        const bool bShouldHide = bOutsideXY || bAboveSlice;

        if (bShouldHide)
        {
            ResourceActor->SetActorHiddenInGame(true);
            MiningPresentationHiddenActors.Add(ResourceActor);
            StillHidden.Add(ResourceActor);
        }
        else if (MiningPresentationHiddenActors.Contains(ResourceActor))
        {
            ResourceActor->SetActorHiddenInGame(false);
        }
    }

    // ─── Surface decoration actors (WorldDecorationSubsystem: trees, bushes, props) ──────────
    //
    // WorldDecorationSubsystem stamps its spawned actors with the FName actor tag
    // "WorldForgeDecoration" so we can filter them here without a subsystem pointer.
    // Same hide/restore logic as colonists and resource nodes.
    static const FName GDecorationActorTag = FName("WorldForgeDecoration");
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* DecoActor = *It;
        if (!IsValid(DecoActor) || !DecoActor->ActorHasTag(GDecorationActorTag))
        {
            continue;
        }

        const FVector Loc = DecoActor->GetActorLocation();
        const bool bOutsideXY =
            FMath::Abs(Loc.X - CenterWorldX) > VisibleRadiusUU ||
            FMath::Abs(Loc.Y - CenterWorldY) > VisibleRadiusUU;
        const bool bAboveSlice = Loc.Z > (ActiveWorldZ + AboveLayerBufferUU);
        const bool bShouldHide = bOutsideXY || bAboveSlice;

        if (bShouldHide)
        {
            DecoActor->SetActorHiddenInGame(true);
            MiningPresentationHiddenActors.Add(DecoActor);
            StillHidden.Add(DecoActor);
        }
        else if (MiningPresentationHiddenActors.Contains(DecoActor))
        {
            DecoActor->SetActorHiddenInGame(false);
        }
    }

    // ─── WildHarvest crop patch actors (BP_CropPatch_LowPoly_C etc.) ─────────────
    //
    // WildHarvest crop patches are not tagged WorldForgeDecoration and do not
    // inherit from AWorldResourceActor, so they need their own pass. We match by
    // class name substring ("CropPatch") which catches all LowPoly / HighPoly / etc.
    // variants without requiring a hard C++ base class reference.
    //
    // All crop patches above the active layer are hidden underground; patches that
    // happen to be at or below the layer (unusual, but possible near stairs/ramps)
    // are left visible.
    // ─────────────────────────────────────────────────────────────────────────────
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* CropActor = *It;
        if (!IsValid(CropActor)) { continue; }

        const FString ClassName = CropActor->GetClass()->GetName();
        if (!ClassName.Contains(TEXT("CropPatch"), ESearchCase::IgnoreCase) &&
            !ClassName.Contains(TEXT("WildHarvest"), ESearchCase::IgnoreCase))
        {
            continue;
        }

        const FVector Loc = CropActor->GetActorLocation();
        const bool bAboveSlice = Loc.Z > (ActiveWorldZ + AboveLayerBufferUU);
        const bool bOutsideXY  =
            FMath::Abs(Loc.X - CenterWorldX) > VisibleRadiusUU ||
            FMath::Abs(Loc.Y - CenterWorldY) > VisibleRadiusUU;
        const bool bShouldHide = bAboveSlice || bOutsideXY;

        if (bShouldHide)
        {
            CropActor->SetActorHiddenInGame(true);
            MiningPresentationHiddenActors.Add(CropActor);
            StillHidden.Add(CropActor);
        }
        else if (MiningPresentationHiddenActors.Contains(CropActor))
        {
            CropActor->SetActorHiddenInGame(false);
        }
    }

    TArray<TWeakObjectPtr<AActor>> ToRestore;
    for (const TWeakObjectPtr<AActor>& HiddenActor : MiningPresentationHiddenActors)
    {
        if (!StillHidden.Contains(HiddenActor))
        {
            ToRestore.Add(HiddenActor);
        }
    }

    for (const TWeakObjectPtr<AActor>& HiddenActor : ToRestore)
    {
        if (AActor* Actor = HiddenActor.Get())
        {
            Actor->SetActorHiddenInGame(false);
        }
        MiningPresentationHiddenActors.Remove(HiddenActor);
    }
}

void UBuildModeManager::RestoreMiningPresentationVisibility()
{
    for (const TWeakObjectPtr<AActor>& HiddenActor : MiningPresentationHiddenActors)
    {
        if (AActor* Actor = HiddenActor.Get())
        {
            Actor->SetActorHiddenInGame(false);
        }
    }

    MiningPresentationHiddenActors.Reset();
}

void UBuildModeManager::DeactivateGridOverlay()
{
    if (!GridOverlay)
    {
        return;
    }

    GridOverlay->ClearSelection();
    GridOverlay->ClearDragPreview();
    GridOverlay->ClearHover();

    // Keep confirmed designations visible after exiting a tool. The overlay
    // actor now has persistent confirmed state separate from active selection.
    if (GridOverlay->HasConfirmedDesignationCells())
    {
        GridOverlay->ShowOverlay();
    }
    else
    {
        GridOverlay->HideOverlay();
    }
}

FIntVector UBuildModeManager::WorldToVoxel(const FVector& WorldPos, float VoxelSize)
{
    return FIntVector(
        FMath::FloorToInt(WorldPos.X / VoxelSize),
        FMath::FloorToInt(WorldPos.Y / VoxelSize),
        FMath::FloorToInt(WorldPos.Z / VoxelSize)
    );
}

FIntPoint UBuildModeManager::VoxelToTerrainCell(const FIntVector& VoxelCoord, int32 VoxelsPerCell)
{
    return FIntPoint(
        FMath::FloorToInt32(VoxelCoord.X / static_cast<float>(VoxelsPerCell)),
        FMath::FloorToInt32(VoxelCoord.Y / static_cast<float>(VoxelsPerCell))
    );
}

void UBuildModeManager::SyncSelectionFromGridSelection()
{
    UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem();
    if (!GridSelection)
    {
        return;
    }

    TArray<FGridSelectionCell> GridCells;
    GridSelection->GetSelectedCells(GridCells);

    SelectedCells.Empty();
    SelectedCells3D.Empty();
    FarmPaintedWorldCells.Empty();

    UPlayerBuildContext* Ctx = GetBuildContext();
    if (Ctx)
    {
        Ctx->ClearSelection();
    }

    UVoxelWorldManager* VM = GetVoxelManager();

    for (const FGridSelectionCell& GridCell : GridCells)
    {
        if (CurrentToolMode == EBuildToolMode::Mine && GetMiningHeightMode() == EWFMiningHeightMode::TwoVoxelColumn)
        {
            SelectedCells3D.AddUnique(GridCell.VoxelCoord);
            SelectedCells3D.AddUnique(FIntVector(GridCell.VoxelCoord.X, GridCell.VoxelCoord.Y, GridCell.VoxelCoord.Z - 1));
        }
        else
        {
            SelectedCells3D.AddUnique(GridCell.VoxelCoord);
        }

        FIntPoint ToolCell = FIntPoint::ZeroValue;

        if (CurrentToolMode == EBuildToolMode::Farm)
        {
            // Option B farm selection:
            // Preserve the exact grid/voxel XY selected by the player.
            // Do NOT collapse through WorldToTerrainCell(), because several
            // 100uu grid cells can map into one larger terrain cell.
            //
            // Result:
            //   1 click       = 1 farm cell
            //   3 clicks      = 3 farm cells
            //   2x5 drag      = 10 farm cells
            ToolCell = FIntPoint(GridCell.VoxelCoord.X, GridCell.VoxelCoord.Y);
            FarmPaintedWorldCells.Add(ToolCell);
        }
        else
        {
            // Non-farm tools keep the existing 2D helper cell conversion for
            // legacy UI/build-context compatibility. Build/Mine still use
            // SelectedCells3D as their authoritative exact voxel selection.
            ToolCell = VoxelToTerrainCell(GridCell.VoxelCoord);
            if (VM)
            {
                const FVector WorldPos(
                    (static_cast<float>(GridCell.VoxelCoord.X) + 0.5f) * 100.0f,
                    (static_cast<float>(GridCell.VoxelCoord.Y) + 0.5f) * 100.0f,
                    GridCell.TerrainZ);

                ToolCell = VM->WorldToTerrainCell(WorldPos);
            }
        }

        SelectedCells.AddUnique(ToolCell);

        if (Ctx)
        {
            Ctx->AddCell(ToolCell);
        }
    }

    const int32 Count = CurrentToolMode == EBuildToolMode::Farm
        ? FarmPaintedWorldCells.Num()
        : SelectedCells3D.Num();

    OnCellCountChanged.Broadcast(Count);

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        // Visual preview should show the cells the player directly marked.
        // CommitMineDesignations may expand to a two-voxel column, but previewing
        // both layers at the surface creates confusing stacked markers.
        TArray<FIntVector> VisualMineCells;
        VisualMineCells.Reserve(GridCells.Num());
        for (const FGridSelectionCell& GridCell : GridCells)
        {
            VisualMineCells.AddUnique(GridCell.VoxelCoord);
        }

        GridSelection->SetMiningVolumePreviewCells(VisualMineCells);
        UpdateMiningLayerRenderer();
    }
    else
    {
        GridSelection->ClearMiningVolumePreviewCells();
    }

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Synced grid selection. Tool=%d GridCells=%d Selected2D=%d Selected3D=%d FarmCells=%d"),
        static_cast<int32>(CurrentToolMode),
        GridCells.Num(),
        SelectedCells.Num(),
        SelectedCells3D.Num(),
        FarmPaintedWorldCells.Num());
}

void UBuildModeManager::ClearTileSelectionState()
{
    SelectedCells.Empty();
    SelectedCells3D.Empty();
    FarmPaintedWorldCells.Empty();

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->ClearSelectedCells();
        GridSelection->ClearPreviewCells();
        GridSelection->ClearMiningVolumePreviewCells();
    }

    if (GridOverlay)
    {
        GridOverlay->ClearSelection();
        GridOverlay->ClearDragPreview();
    }

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
    }

    OnCellCountChanged.Broadcast(0);
}

void UBuildModeManager::AddOrToggleCurrentTileSelection(bool bToggleExisting)
{
    if (!bHoveredCellValid)
    {
        return;
    }

    const bool bAlreadySelected2D = SelectedCells.Contains(CurrentHoveredCell);
    const bool bAlreadySelected3D = SelectedCells3D.Contains(CurrentHoveredVoxel);

    if (bToggleExisting && bToggleSelectedCellsOnReselect && (bAlreadySelected2D || bAlreadySelected3D))
    {
        SelectedCells.Remove(CurrentHoveredCell);
        SelectedCells3D.Remove(CurrentHoveredVoxel);
        FarmPaintedWorldCells.Remove(CurrentHoveredCell);

        if (GridOverlay)
        {
            GridOverlay->RemoveSelectedCell(CurrentOverlayVoxel);
        }
    }
    else
    {
        SelectedCells.AddUnique(CurrentHoveredCell);
        SelectedCells3D.AddUnique(CurrentHoveredVoxel);

        if (CurrentToolMode == EBuildToolMode::Farm)
        {
            FarmPaintedWorldCells.Add(CurrentHoveredCell);
        }

        if (GridOverlay)
        {
            GridOverlay->AddSelectedCell(CurrentOverlayVoxel, CurrentHoveredTerrainZ);
        }
    }

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
        for (const FIntPoint& Cell : SelectedCells)
        {
            Ctx->AddCell(Cell);
        }
    }

    OnCellCountChanged.Broadcast(
        CurrentToolMode == EBuildToolMode::Farm
        ? FarmPaintedWorldCells.Num()
        : SelectedCells.Num());

    UpdateGhost();
    UpdateGridOverlay();
}

bool UBuildModeManager::CommitMineDesignations()
{
    if (CurrentToolMode != EBuildToolMode::Mine)
    {
        return false;
    }

    if (SelectedCells3D.Num() == 0 && bHoveredCellValid)
    {
        AddOrToggleCurrentTileSelection(false);
    }

    if (SelectedCells3D.Num() == 0)
    {
        BroadcastFeedback(TEXT("No mine tiles selected."));
        return false;
    }

    UMiningSubsystem* MiningSub = GetMiningSubsystem();
    if (!MiningSub)
    {
        BroadcastFeedback(TEXT("Mining system unavailable."));
        return false;
    }

    TSet<FIntVector> RequestedMineVoxels;
    RequestedMineVoxels.Reserve(SelectedCells3D.Num() * 2);

    for (const FIntVector& MineVoxel : SelectedCells3D)
    {
        RequestedMineVoxels.Add(MineVoxel);

        if (GetMiningHeightMode() == EWFMiningHeightMode::TwoVoxelColumn)
        {
            RequestedMineVoxels.Add(FIntVector(MineVoxel.X, MineVoxel.Y, MineVoxel.Z - 1));
        }
    }

    TSet<FIntVector> ClaimedMineVoxels;
    ClaimedMineVoxels.Reserve(RequestedMineVoxels.Num());

    int32 SkippedUnclaimedCount = 0;
    for (const FIntVector& MineVoxel : RequestedMineVoxels)
    {
        if (IsMineVoxelInsidePlayerClaim(MineVoxel))
        {
            ClaimedMineVoxels.Add(MineVoxel);
            continue;
        }

        ++SkippedUnclaimedCount;
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Mine designation skipped: unclaimed land. Voxel=(%d,%d,%d)"),
            MineVoxel.X,
            MineVoxel.Y,
            MineVoxel.Z);
    }

    if (ClaimedMineVoxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Mine designation blocked: no selected cells inside claimed land."));
        BroadcastFeedback(TEXT("Cannot mine here. This land is not claimed by your colony."));
        return false;
    }

    if (SkippedUnclaimedCount > 0)
    {
        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            GridSelection->RetainSelectedCells(ClaimedMineVoxels);
        }
    }

    int32 CreatedCount = 0;
    for (const FIntVector& MineVoxel : ClaimedMineVoxels)
    {
        int32 OutDesignationID = INDEX_NONE;
        const bool bCreated = MiningSub->CreateMiningDesignation(
            GetEffectiveSettlementID(),
            MineVoxel,
            0,
            OutDesignationID);

        if (bCreated && OutDesignationID != INDEX_NONE)
        {
            ++CreatedCount;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Mine designations committed. Created=%d Requested=%d Claimed=%d SkippedUnclaimed=%d Settlement=%d"),
        CreatedCount,
        RequestedMineVoxels.Num(),
        ClaimedMineVoxels.Num(),
        SkippedUnclaimedCount,
        GetEffectiveSettlementID());

    if (SkippedUnclaimedCount > 0)
    {
        BroadcastFeedback(FString::Printf(
            TEXT("Mining designated: %d voxel(s). Skipped %d unclaimed voxel(s)."),
            CreatedCount,
            SkippedUnclaimedCount));
    }
    else
    {
        BroadcastFeedback(FString::Printf(
            TEXT("Mining designated: %d voxel(s)."),
            CreatedCount));
    }

    return CreatedCount > 0;
}

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
