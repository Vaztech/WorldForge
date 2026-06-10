#include "BuildModeManager.h"
#include "BuildGhostActor.h"
#include "TileGridOverlayActor.h"
#include "PlayerBuildContext.h"
#include "VoxelWorldManager.h"
#include "VoxelConstructionSystem.h"
#include "SettlementManager.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Player/DigGhostActor.h"
#include "Player/MiningGhostActor.h"
#include "Voxel/DigSubsystem.h"
#include "Mining/MiningSubsystem.h"
#include "Mining/MiningLayerRenderer.h"
#include "Farming/FarmingSubsystem.h"
#include "Farming/FarmingTypes.h"
#include "Farming/WildHarvestAdapter.h"
#include "Grid/GridSelectionSubsystem.h"
#include "Grid/LayerViewSubsystem.h"
#include "Grid/GridDesignationTypes.h"
#include "WorldMapSubsystem.h"
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

    static FString GetCropDisplayName(FName CropRow)
    {
        FString Result = CropRow.ToString();
        if (Result.StartsWith(TEXT("LP_")))
        {
            Result.RightChopInline(3);
        }
        return Result;
    }
    static bool IsCropRowKnownToWildHarvest(const UWildHarvestAdapter* Adapter, FName CropRow, bool& bOutWasValidated)
    {
        bOutWasValidated = false;

        if (CropRow.IsNone())
        {
            return false;
        }

        if (!IsValid(Adapter) || !Adapter->GetCropDataTable())
        {
            return true;
        }

        bOutWasValidated = true;
        return Adapter->GetAllCropRowNames().Contains(CropRow);
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
    DestroyMiningLayerRenderer(TEXT("Deinitialize"));
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

int32 UBuildModeManager::GetTotalMinePlanCount() const
{
    const UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem();
    return GridSelection ? GridSelection->GetTotalMinePlanCount() : 0;
}

int32 UBuildModeManager::GetMinePlanLayerCount() const
{
    const UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem();
    return GridSelection ? GridSelection->GetMinePlanLayerCount() : 0;
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

bool UBuildModeManager::IsVoxelCellInsidePlayerClaim(const FIntPoint& VoxelCell, const TCHAR* ToolLabel) const
{
    const USettlementManager* SettlementManager = GetSettlementManager();
    if (!SettlementManager)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] %s claim check failed: SettlementManager missing. CellXY=(%d,%d)"),
            ToolLabel ? ToolLabel : TEXT("Tool"),
            VoxelCell.X,
            VoxelCell.Y);
        return false;
    }

    int32 WorldTileID = INDEX_NONE;
    FIntPoint WorldTileXY(INDEX_NONE, INDEX_NONE);
    int32 PlayerSettlementID = INDEX_NONE;
    int32 ClaimedWorldTileCount = 0;
    const bool bClaimed = SettlementManager->DoesPlayerSettlementClaimVoxelCellDetailed(
        VoxelCell,
        WorldTileID,
        WorldTileXY,
        PlayerSettlementID,
        ClaimedWorldTileCount);

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] %s claim check: CellXY=(%d,%d) WorldTile=(%d,%d) WorldTileID=%d PlayerSettlementID=%d ClaimedTileCount=%d Claimed=%s"),
        ToolLabel ? ToolLabel : TEXT("Tool"),
        VoxelCell.X,
        VoxelCell.Y,
        WorldTileXY.X,
        WorldTileXY.Y,
        WorldTileID,
        PlayerSettlementID,
        ClaimedWorldTileCount,
        bClaimed ? TEXT("true") : TEXT("false"));

    return bClaimed;
}

bool UBuildModeManager::IsBuildVoxelInsidePlayerClaim(const FIntVector& Voxel) const
{
    return IsVoxelCellInsidePlayerClaim(FIntPoint(Voxel.X, Voxel.Y), TEXT("Build"));
}

bool UBuildModeManager::IsDigVoxelInsidePlayerClaim(const FIntVector& Voxel) const
{
    return IsVoxelCellInsidePlayerClaim(FIntPoint(Voxel.X, Voxel.Y), TEXT("Dig"));
}

bool UBuildModeManager::IsMineVoxelInsidePlayerClaim(const FIntVector& Voxel) const
{
    return IsVoxelCellInsidePlayerClaim(FIntPoint(Voxel.X, Voxel.Y), TEXT("Mine"));
}

bool UBuildModeManager::IsFarmCellInsidePlayerClaim(const FIntPoint& WorldCell) const
{
    return IsVoxelCellInsidePlayerClaim(WorldCell, TEXT("Farm"));
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

    bDataTablePreviewActive = false;
    DataTablePreviewSourceID = NAME_None;
    bDataTablePreviewIsPrefab = false;
    DataTablePreviewMesh = nullptr;

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

    bDataTablePreviewActive = false;
    DataTablePreviewSourceID = NAME_None;
    bDataTablePreviewIsPrefab = false;
    DataTablePreviewMesh = nullptr;

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
    bDataTablePreviewActive = false;
    DataTablePreviewSourceID = NAME_None;
    bDataTablePreviewIsPrefab = false;
    DataTablePreviewMesh = nullptr;
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

        if (CurrentToolMode == EBuildToolMode::Mine)
        {
            SyncSelectionFromGridSelection();
            UpdateMiningLayerRenderer();
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

        if (CurrentToolMode == EBuildToolMode::Mine)
        {
            SyncSelectionFromGridSelection();
            UpdateMiningLayerRenderer();
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

        if (CurrentToolMode == EBuildToolMode::Mine)
        {
            if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
            {
                GridSelection->TickSelectionFromMouse(PC);
            }
            SyncSelectionFromGridSelection();
            UpdateMiningLayerRenderer();
        }

        BroadcastFeedback(FString::Printf(TEXT("View layer: %+d"), GridSelection->GetActiveViewLayerOffset()));
    }
}

void UBuildModeManager::ResetActiveViewLayer()
{
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->ResetActiveViewLayer();

        if (CurrentToolMode == EBuildToolMode::Mine)
        {
            if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
            {
                GridSelection->TickSelectionFromMouse(PC);
            }
            SyncSelectionFromGridSelection();
            UpdateMiningLayerRenderer();
        }

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
        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            if (CurrentToolMode == EBuildToolMode::Mine)
            {
                if (GridSelection->IsDraggingSelection() && GridSelection->EndDragSelection(true))
                {
                    SyncSelectionFromGridSelection();

                    UE_LOG(LogTemp, Log,
                        TEXT("[CUBE-MINING-V2] Mine drag end SelectedCells=%d"),
                        GridSelection->GetSelectedCount());
                }
            }
            else if (bWasDragging && GridSelection->EndDragSelection(true))
            {
                SyncSelectionFromGridSelection();
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
        bIsDraggingBuild = false;
        bHasDragStart = false;
        DragStartVoxel = FIntVector::ZeroValue;
        LastPlacedDragVoxel = FIntVector(INT32_MIN);

        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
            {
                GridSelection->TickSelectionFromMouse(PC);

                const FIntVector RefreshedHover = GridSelection->GetHoveredVoxel();
                UE_LOG(LogTemp, Log,
                    TEXT("[CUBE-MINING-01] Mine pre-drag hover refreshed. HoverValid=%s Hover=(%d,%d,%d)"),
                    GridSelection->HasValidHover() ? TEXT("true") : TEXT("false"),
                    RefreshedHover.X,
                    RefreshedHover.Y,
                    RefreshedHover.Z);

                if (GridSelection->HasValidHover())
                {
                    CurrentHoveredVoxel = RefreshedHover;
                    CurrentOverlayVoxel = RefreshedHover;
                    CurrentHoveredCell = FIntPoint(RefreshedHover.X, RefreshedHover.Y);
                    CurrentHoveredTerrainZ = GridSelection->GetHoveredTerrainZ();
                    LastHitZ = CurrentHoveredTerrainZ;
                    LastValidVoxel = RefreshedHover;
                    bHasLastValidVoxel = true;
                    bHoveredCellValid = true;
                }
            }

            if (GridSelection->BeginDragSelection())
            {
                bIsDraggingBuild = true;
                bHasDragStart = true;
                DragStartVoxel = CurrentHoveredVoxel;

                UE_LOG(LogTemp, Log,
                    TEXT("[CUBE-MINING-V2] Mine drag start Hover=(%d,%d,%d)"),
                    CurrentHoveredVoxel.X,
                    CurrentHoveredVoxel.Y,
                    CurrentHoveredVoxel.Z);
            }
            else
            {
                GridSelection->ToggleHoveredSelection();
                SyncSelectionFromGridSelection();
            }
        }

        return;
    }

    if (bDataTablePreviewActive && CurrentMode == EBuildMode::Prefab && CurrentToolMode == EBuildToolMode::Place)
    {
        CommitDataTableBuildPiecePlacement();
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

        if (CurrentToolMode == EBuildToolMode::Farm && !IsFarmCellInsidePlayerClaim(FIntPoint(CurrentOverlayVoxel.X, CurrentOverlayVoxel.Y)))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] Farm selection rejected: unclaimed land. FarmCell=(%d,%d)"),
                CurrentOverlayVoxel.X,
                CurrentOverlayVoxel.Y);
            BroadcastFeedback(TEXT("Cannot farm here. This land is not claimed by your colony."));
            return;
        }

        if (CurrentToolMode == EBuildToolMode::Place && !IsBuildVoxelInsidePlayerClaim(CurrentHoveredVoxel))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] Build selection rejected: unclaimed land. Voxel=(%d,%d,%d)"),
                CurrentHoveredVoxel.X,
                CurrentHoveredVoxel.Y,
                CurrentHoveredVoxel.Z);
            BroadcastFeedback(TEXT("Cannot build here. This land is not claimed by your colony."));
            return;
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

        // Verify hover cell == confirm cell. After the Patch 1 fix these should
        // always match. Log if they don't so regressions are caught immediately.
        UE_LOG(LogTemp, Verbose,
            TEXT("[MouseGrid] ConfirmCell=(%d,%d,%d) OverlayCell=(%d,%d,%d) Match=%s"),
            CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y, CurrentHoveredVoxel.Z,
            CurrentOverlayVoxel.X, CurrentOverlayVoxel.Y, CurrentOverlayVoxel.Z,
            (CurrentHoveredVoxel == CurrentOverlayVoxel) ? TEXT("YES") : TEXT("NO"));

        if (CurrentHoveredVoxel != CurrentOverlayVoxel)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[MouseGrid] MISMATCH: ConfirmCell≠OverlayCell. "
                    "Confirm=(%d,%d,%d) Overlay=(%d,%d,%d) — "
                    "player saw hover on wrong tile."),
                CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y, CurrentHoveredVoxel.Z,
                CurrentOverlayVoxel.X, CurrentOverlayVoxel.Y, CurrentOverlayVoxel.Z);
        }

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
    int32 GridSelectedCountForLog = 0;
    if (UGridSelectionSubsystem* GridSelectionForLog = GetGridSelectionSubsystem())
    {
        TArray<FGridSelectionCell> GridCellsForLog;
        GridSelectionForLog->GetSelectedCells(GridCellsForLog);
        GridSelectedCountForLog = GridCellsForLog.Num();
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[CUBE-MINING-V2] HandleConfirm entered. ToolMode=%d CurrentMode=%d bJustCommitted=%s SelectedCells=%d SelectedCells3D=%d GridSelected=%d"),
        static_cast<int32>(CurrentToolMode),
        static_cast<int32>(CurrentMode),
        bJustCommitted ? TEXT("true") : TEXT("false"),
        SelectedCells.Num(),
        SelectedCells3D.Num(),
        GridSelectedCountForLog);

    if (bJustCommitted)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CUBE-MINING-V2] HandleConfirm blocked by bJustCommitted."));
        return;
    }

    if (bDataTablePreviewActive && CurrentMode == EBuildMode::Prefab && CurrentToolMode == EBuildToolMode::Place)
    {
        if (CommitDataTableBuildPiecePlacement())
        {
            bJustCommitted = true;
            bIsDraggingBuild = false;
            LastPlacedDragVoxel = FIntVector(INT32_MIN);
            bHasDragStart = false;
            DragStartVoxel = FIntVector::ZeroValue;
            bDragStartedFromBlock = false;
        }
        return;
    }

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CUBE-MINING-V2] HandleConfirm dispatching Mine commit."));

        if (CommitMineDesignations())
        {
            bJustCommitted = true;
            bIsDraggingBuild = false;
            LastPlacedDragVoxel = FIntVector(INT32_MIN);
            bHasDragStart = false;
            DragStartVoxel = FIntVector::ZeroValue;
            bDragStartedFromBlock = false;

            OnCellCountChanged.Broadcast(0);
            UpdateGridOverlay();
            UpdateMiningLayerRenderer();
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
            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] Farm zone committed. Confirming %d farm cells to overlay."),
                FarmPaintedWorldCells.Num());

            if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
            {
                // Use FarmPaintedWorldCells as the authoritative source for confirmed markers.
                // CommitSelectedCellsToConfirmedOverlay reads GridOverlay::SelectedCells which
                // can be empty when selection went through the legacy paint path (bSelectionActive
                // was false at click time). FarmPaintedWorldCells is always correct regardless
                // of which selection path was used.
                GridSelection->CommitFarmCellsToConfirmedOverlay(FarmPaintedWorldCells);
                GridSelection->ClearPreviewCells();
                GridSelection->ClearSelectedCells();
                GridSelection->EndSelectionTool(false);
            }

            bJustCommitted = false;
            bIsDraggingBuild = false;
            LastPlacedDragVoxel = FIntVector(INT32_MIN);
            bHasDragStart = false;
            DragStartVoxel = FIntVector::ZeroValue;
            bDragStartedFromBlock = false;

            SelectedCells.Empty();
            SelectedCells3D.Empty();
            FarmPaintedWorldCells.Empty();

            if (UPlayerBuildContext* Ctx = GetBuildContext())
            {
                Ctx->ClearSelection();
            }

            OnCellCountChanged.Broadcast(0);

            if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
            {
                GridSelection->SetToggleSelectedCellsOnReselect(bToggleSelectedCellsOnReselect);
                GridSelection->BeginSelectionTool(EBuildToolMode::Farm, EGridSelectionShape::Rectangle);
                GridSelection->ClearSelectedCells();
                GridSelection->ClearPreviewCells();
                GridSelection->ClearMiningVolumePreviewCells();
            }

            UpdateGridOverlay();

            // Re-check inventory after seed consumption and auto-select the next available seed.
            // This prevents the next confirm from silently failing with a depleted crop.
            const bool bHasSeedsRemaining = SelectNextAvailableFarmSeed(GetEffectiveSettlementID());

            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] Farm selection tool re-armed. SelectedFarmCropRow=%s HasSeeds=%s"),
                SelectedFarmCropRow.IsNone() ? TEXT("None") : *SelectedFarmCropRow.ToString(),
                bHasSeedsRemaining ? TEXT("true") : TEXT("false"));

            if (bHasSeedsRemaining)
            {
                BroadcastFeedback(FString::Printf(
                    TEXT("Farm zone committed. Next seed: %s."),
                    *GetCropDisplayName(SelectedFarmCropRow)));
            }
            else
            {
                BroadcastFeedback(TEXT("Farm zone committed. No farm seeds remain."));
            }
        }

        return;
    }

    if (CurrentMode == EBuildMode::CustomDraw)
    {
        // Patch 2D: Capture cells from GridSelection BEFORE CommitCustomBuild.
        // CommitCustomBuild empties SelectedCells3D internally on success.
        // GetSelectedCells reads GridSelectionSubsystem::SelectedCells — the same source
        // CommitSelectedCellsToConfirmedOverlay uses, so coord match is guaranteed.
        TArray<FGridSelectionCell> BuildCellsSnapshot;
        if (UGridSelectionSubsystem* GridSelectionSnap = GetGridSelectionSubsystem())
        {
            GridSelectionSnap->GetSelectedCells(BuildCellsSnapshot);
        }

        if (CommitCustomBuild())
        {
            if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
            {
                GridSelection->CommitSelectedCellsToConfirmedOverlay(EGridDesignationType::Build);

                // Patch 2D: Remove confirmed build overlay per cell — build is committed.
                UE_LOG(LogTemp, Log,
                    TEXT("[BuildMode] [2D] Custom build committed. Removing confirmed overlay for %d cells."),
                    BuildCellsSnapshot.Num());

                for (const FGridSelectionCell& SnapCell : BuildCellsSnapshot)
                {
                    const bool bRemoved = GridSelection->RemoveConfirmedDesignationCell(
                        SnapCell.VoxelCoord,
                        EGridDesignationType::Build);

                    UE_LOG(LogTemp, Log,
                        TEXT("[BuildMode] [2D] Build overlay removal. Voxel=(%d,%d,%d) Removed=%s"),
                        SnapCell.VoxelCoord.X,
                        SnapCell.VoxelCoord.Y,
                        SnapCell.VoxelCoord.Z,
                        bRemoved ? TEXT("true") : TEXT("false"));
                }

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
        DestroyMiningLayerRenderer(TEXT("CancelMineMode"));
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

void UBuildModeManager::SetSelectedFarmCropSeed(FName CropRowName)
{
    if (CropRowName.IsNone())
    {
        ClearSelectedFarmCropSeed();
        return;
    }

    SelectedFarmCropRow = CropRowName;

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Selected farm crop seed: %s"),
        *SelectedFarmCropRow.ToString());

    BroadcastFeedback(FString::Printf(
        TEXT("Farm seed selected: %s"),
        *GetCropDisplayName(SelectedFarmCropRow)));
}

void UBuildModeManager::ClearSelectedFarmCropSeed()
{
    SelectedFarmCropRow = NAME_None;
    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Selected farm crop seed cleared."));
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

    // Auto-select the first available seed if none is currently selected (or current selection is depleted).
    if (SelectedFarmCropRow.IsNone())
    {
        if (const USettlementManager* SM = GetSettlementManager())
        {
            const int32 SID = GetEffectiveSettlementID();
            const TArray<FName> SeedOrder = SM->GetStartingSeedInventoryOrder(SID);
            for (const FName& CropRow : SeedOrder)
            {
                if (SM->GetStartingSeedInventoryAmount(SID, CropRow) > 0)
                {
                    SetSelectedFarmCropSeed(CropRow);
                    UE_LOG(LogTemp, Log,
                        TEXT("[BuildMode] Auto-selected farm seed on enter: %s (Settlement=%d)"),
                        *CropRow.ToString(), SID);
                    break;
                }
            }
        }
    }
    else
    {
        // Verify the existing selection still has seeds available.
        if (const USettlementManager* SM = GetSettlementManager())
        {
            const int32 SID = GetEffectiveSettlementID();
            if (SM->GetStartingSeedInventoryAmount(SID, SelectedFarmCropRow) <= 0)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[BuildMode] Previously selected farm seed depleted: %s. Re-selecting."),
                    *SelectedFarmCropRow.ToString());
                ClearSelectedFarmCropSeed();

                const TArray<FName> SeedOrder = SM->GetStartingSeedInventoryOrder(SID);
                for (const FName& CropRow : SeedOrder)
                {
                    if (SM->GetStartingSeedInventoryAmount(SID, CropRow) > 0)
                    {
                        SetSelectedFarmCropSeed(CropRow);
                        UE_LOG(LogTemp, Log,
                            TEXT("[BuildMode] Auto-selected replacement farm seed: %s (Settlement=%d)"),
                            *CropRow.ToString(), SID);
                        break;
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered Farm mode. SelectedFarmCropRow=%s"),
        SelectedFarmCropRow.IsNone() ? TEXT("None") : *SelectedFarmCropRow.ToString());
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
        ActiveMiningZ = INDEX_NONE;

        if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
        {
            GridSelection->EndSelectionTool(true);
            GridSelection->ClearMiningVolumePreviewCells();
        }

        DeactivateGridOverlay();
        DestroyMiningLayerRenderer(TEXT("ExitMineMode"));
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
    CurrentHoveredCell = FIntPoint::ZeroValue;
    CurrentHoveredVoxel = FIntVector::ZeroValue;
    CurrentOverlayVoxel = FIntVector::ZeroValue;
    bHoveredCellValid = false;
    LastHitZ = 0.0f;
    CurrentHoveredTerrainZ = 0.0f;
    bJustCommitted = false;
    bHasLastValidVoxel = false;
    LastValidVoxel = FIntVector::ZeroValue;
    bIsDraggingBuild = false;
    LastPlacedDragVoxel = FIntVector(INT32_MIN);
    bHasDragStart = false;
    DragStartVoxel = FIntVector::ZeroValue;
    bDragStartedFromBlock = false;
    ActiveMiningZ = INDEX_NONE;

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
    }

    // Mine mode keeps the dedicated layer renderer alive so underground cutaway
    // has an owner for the solid cap/slice and mining ghosts.
    DestroyGhost();
    if (ActiveMiningGhost)
    {
        ActiveMiningGhost->Destroy();
        ActiveMiningGhost = nullptr;
    }
    RestoreMiningPresentationVisibility();
    InitializeMiningLayerRenderer();
    if (MiningLayerRenderer)
    {
        MiningLayerRenderer->ForceNextRebuild();
    }

    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        GridSelection->SetActiveMineSettlementID(GetEffectiveSettlementID());
        GridSelection->SetToggleSelectedCellsOnReselect(bToggleSelectedCellsOnReselect);
        GridSelection->BeginSelectionTool(EBuildToolMode::Mine, EGridSelectionShape::Rectangle);
        GridSelection->ClearMiningVolumePreviewCells();
    }

    UE_LOG(LogTemp, Log,
        TEXT("[CUBE-MINING-01] EnterMineMode visual. ActiveMiningZ=UNSET SettlementID=%d Ghost=false Renderer=%s Cutaway=pending"),
        GetEffectiveSettlementID(),
        MiningLayerRenderer ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log, TEXT("[BuildMode] Entered Mine mode."));
    OnBuildModeChanged.Broadcast(CurrentMode);
    OnCellCountChanged.Broadcast(0);
    BroadcastSelectionChanged();
    UpdateMiningLayerRenderer();
    BroadcastFeedback(TEXT("Mine mode enabled. Paint mine tiles, then Confirm."));
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

    UpdateMiningLayerRenderer();
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
                FIntVector NewGridHover = GridSelection->GetHoveredVoxel();

                if (CurrentToolMode == EBuildToolMode::Mine)
                {
                    ULayerViewSubsystem* LV_Hover = GetLayerViewSubsystem();
                    if (LV_Hover && LV_Hover->HasActiveVoxelLayer() && LV_Hover->GetRelativeLayerOffset() < 0)
                    {
                        // Underground: authoritative Z comes from LayerView, not locked surface ActiveMiningZ.
                        NewGridHover.Z = LV_Hover->GetActiveVoxelLayerZ();
                    }
                    else
                    {
                        if (UVoxelWorldManager* VM = GetVoxelManager())
                        {
                            int32 SurfaceZ = INDEX_NONE;
                            if (VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(NewGridHover.X, NewGridHover.Y), SurfaceZ))
                            {
                                if (ActiveMiningZ == INDEX_NONE)
                                {
                                    ActiveMiningZ = SurfaceZ;
                                    UE_LOG(LogTemp, Log,
                                        TEXT("[CUBE-MINING-01] ActiveMiningZ resolved. SurfaceZ=%d HoveredVoxel=(%d,%d,%d)"),
                                        SurfaceZ,
                                        NewGridHover.X,
                                        NewGridHover.Y,
                                        ActiveMiningZ);
                                }

                                NewGridHover.Z = ActiveMiningZ;
                            }
                        }
                    }
                }

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

                    if (CurrentToolMode == EBuildToolMode::Mine)
                    {
                        UE_LOG(LogTemp, Log,
                            TEXT("[MineSelectionAudit] Hover XY=(%d,%d) ActiveMiningZ=%d OutVoxel=(%d,%d,%d) OverlayVoxel=(%d,%d,%d)"),
                            CurrentHoveredCell.X,
                            CurrentHoveredCell.Y,
                            ActiveMiningZ,
                            NewGridHover.X,
                            NewGridHover.Y,
                            NewGridHover.Z,
                            CurrentOverlayVoxel.X,
                            CurrentOverlayVoxel.Y,
                            CurrentOverlayVoxel.Z);

                        // CUBE-MINING-V3: this Mine hover-tick audit fires on every mine
                        // layer and reports the live visual path before the renderer refresh
                        // later in this tick.
                        {
                            ULayerViewSubsystem* LV_Tick = GetLayerViewSubsystem();
                            const int32 RelOffsetTick =
                                (LV_Tick && LV_Tick->HasActiveVoxelLayer()) ? LV_Tick->GetRelativeLayerOffset() : 0;
                            const bool bUndergroundTick = RelOffsetTick < 0;
                            UE_LOG(LogTemp, Log,
                                TEXT("[CUBE-MINING-V3] Mine hover tick. Layer=%s ActiveMiningZ=%d Relative=%d Ghost=false Renderer=%s Clamp=%s"),
                                bUndergroundTick ? TEXT("Underground") : TEXT("Surface"),
                                ActiveMiningZ,
                                RelOffsetTick,
                                MiningLayerRenderer ? TEXT("true") : TEXT("false"),
                                bMiningLayerAnchorValid ? TEXT("true") : TEXT("false"));
                        }
                    }
                }

                if (CurrentToolMode != EBuildToolMode::Mine)
                {
                    UpdateGhost();
                }
                UpdateGridOverlay();

                if (CurrentToolMode == EBuildToolMode::Mine)
                {
                    UpdateMiningLayerRenderer();
                }

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

    CurrentHoveredTerrainZ = HitLocation.Z;

    // Gameplay/tool coordinate path. Build, mine, dig, and farm claim checks all
    // operate on exact voxel XY, then SettlementManager converts that voxel cell
    // to the strategic world-map tile.
    //
    // NOTE: CurrentOverlayVoxel is intentionally NOT set here from WorldToVoxel().
    // WorldToVoxel uses FloorToInt while VM->WorldToGrid3D uses round-to-nearest
    // (FloorToInt with +0.5 bias). Near tile edges they diverge by ±1, causing the
    // hover tile and confirm cell to mismatch. CurrentOverlayVoxel is synced to
    // NewVoxel below, after block-face and stacking adjustments are applied.
    FIntVector NewVoxel = VM->WorldToGrid3D(HitLocation);

    UE_LOG(LogTemp, Verbose,
        TEXT("[MouseGrid] RawHit=(%.1f,%.1f,%.1f) InitialGridCell=(%d,%d,%d)"),
        HitLocation.X, HitLocation.Y, HitLocation.Z,
        NewVoxel.X, NewVoxel.Y, NewVoxel.Z);

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
                if (Cell.X == NewVoxel.X && Cell.Y == NewVoxel.Y)
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

    if (CurrentMode == EBuildMode::Prefab && CurrentToolMode == EBuildToolMode::Place && !IsBuildVoxelInsidePlayerClaim(NewVoxel))
    {
        bHoveredCellValid = false;

        if (GhostActor)
        {
            GhostActor->SetGhostState(EBuildGhostState::Invalid);
            UpdateGhost();
        }

        return;
    }

    LastValidVoxel = NewVoxel;
    bHasLastValidVoxel = true;

    const bool bChanged =
        (NewVoxel != CurrentHoveredVoxel) ||
        !bHoveredCellValid;

    CurrentHoveredVoxel = NewVoxel;
    CurrentHoveredCell = FIntPoint(NewVoxel.X, NewVoxel.Y);
    // Sync overlay voxel to the final gameplay voxel so the hover tile shown to the
    // player is always the exact cell that a confirm/click will act on.
    // This must happen AFTER block-face offset and stacking adjustments are applied
    // to NewVoxel so all three cases (terrain, placed-block face, stacking) agree.
    CurrentOverlayVoxel = NewVoxel;
    bHoveredCellValid = true;
    LastHitZ = HitLocation.Z;
    CurrentHoveredTerrainZ = HitLocation.Z;

    UE_LOG(LogTemp, Verbose,
        TEXT("[MouseGrid] HoverSnap Raw=(%.1f,%.1f,%.1f) HoverVoxel=(%d,%d,%d) OverlayVoxel=(%d,%d,%d) SnappedWorld=(%.1f,%.1f,%.1f) Match=%s"),
        HitLocation.X, HitLocation.Y, HitLocation.Z,
        CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y, CurrentHoveredVoxel.Z,
        CurrentOverlayVoxel.X, CurrentOverlayVoxel.Y, CurrentOverlayVoxel.Z,
        (static_cast<float>(NewVoxel.X) + 0.5f) * 100.0f,
        (static_cast<float>(NewVoxel.Y) + 0.5f) * 100.0f,
        (static_cast<float>(NewVoxel.Z) + 0.5f) * 100.0f,
        (CurrentOverlayVoxel == CurrentHoveredVoxel) ? TEXT("YES") : TEXT("NO"));

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

    if (CurrentToolMode == EBuildToolMode::Farm)
    {
        SelectedCells3D.Empty();
        SelectedCells.Empty();
        FarmPaintedWorldCells.Reset();

        int32 SkippedUnclaimedPreviewCount = 0;
        for (const FIntVector& Voxel : LineCells3D)
        {
            const FIntPoint FarmCell(Voxel.X, Voxel.Y);
            if (!IsFarmCellInsidePlayerClaim(FarmCell))
            {
                ++SkippedUnclaimedPreviewCount;
                continue;
            }

            SelectedCells3D.AddUnique(Voxel);
            SelectedCells.AddUnique(FarmCell);
            FarmPaintedWorldCells.Add(FarmCell);
        }

        if (SkippedUnclaimedPreviewCount > 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] Farm drag preview filtered unclaimed cells. ClaimedPreview=%d SkippedUnclaimed=%d"),
                FarmPaintedWorldCells.Num(),
                SkippedUnclaimedPreviewCount);
        }
    }
    else
    {
        SelectedCells3D = LineCells3D;
        SelectedCells = LineCells2D;
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

    // Ignore mine-mode ghost cube and the grid overlay actor. Both have NoCollision
    // on all components, so traces cannot hit them in practice. Explicit exclusion
    // is belt-and-suspenders against future material/mesh changes that might re-enable
    // collision, and makes intent clear.
    if (ActiveMiningGhost)
    {
        TerrainParams.AddIgnoredActor(ActiveMiningGhost);
    }

    if (GridOverlay)
    {
        TerrainParams.AddIgnoredActor(GridOverlay);
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

    if (ActiveMiningGhost)
    {
        FullParams.AddIgnoredActor(ActiveMiningGhost);
    }

    if (GridOverlay)
    {
        FullParams.AddIgnoredActor(GridOverlay);
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
            UE_LOG(LogTemp, Verbose,
                TEXT("[MouseGrid] RaycastToTerrain → PlacedBlock Actor=%s Comp=%s Raw=(%.1f,%.1f,%.1f) SupportVoxel=(%d,%d,%d)"),
                FullHit.GetActor() ? *FullHit.GetActor()->GetName() : TEXT("None"),
                FullHit.GetComponent() ? *FullHit.GetComponent()->GetName() : TEXT("None"),
                FullHit.ImpactPoint.X, FullHit.ImpactPoint.Y, FullHit.ImpactPoint.Z,
                OutSupportBlockVoxel.X, OutSupportBlockVoxel.Y, OutSupportBlockVoxel.Z);
            return true;
        }

        if (IsPendingGhostSupportComponent(FullHit.GetComponent()))
        {
            bOutHitPlacedBlock = true;
            OutSupportBlockVoxel = VM->WorldToGrid3D(FullHit.GetComponent()->GetComponentLocation());
            OutHitLocation = FullHit.ImpactPoint;
            OutHitNormal = FullHit.ImpactNormal;
            UE_LOG(LogTemp, Verbose,
                TEXT("[MouseGrid] RaycastToTerrain → PendingGhostSupport Comp=%s Raw=(%.1f,%.1f,%.1f) SupportVoxel=(%d,%d,%d)"),
                FullHit.GetComponent() ? *FullHit.GetComponent()->GetName() : TEXT("None"),
                FullHit.ImpactPoint.X, FullHit.ImpactPoint.Y, FullHit.ImpactPoint.Z,
                OutSupportBlockVoxel.X, OutSupportBlockVoxel.Y, OutSupportBlockVoxel.Z);
            return true;
        }
    }

    if (bHitTerrain)
    {
        OutHitLocation = TerrainHit.ImpactPoint;
        OutHitNormal = TerrainHit.ImpactNormal;
        const FIntVector SnapVoxel = VM->WorldToGrid3D(TerrainHit.ImpactPoint);
        UE_LOG(LogTemp, Verbose,
            TEXT("[MouseGrid] RaycastToTerrain → Terrain Actor=%s Comp=%s Raw=(%.1f,%.1f,%.1f) SnapVoxel=(%d,%d,%d)"),
            TerrainHit.GetActor() ? *TerrainHit.GetActor()->GetName() : TEXT("None"),
            TerrainHit.GetComponent() ? *TerrainHit.GetComponent()->GetName() : TEXT("None"),
            TerrainHit.ImpactPoint.X, TerrainHit.ImpactPoint.Y, TerrainHit.ImpactPoint.Z,
            SnapVoxel.X, SnapVoxel.Y, SnapVoxel.Z);
        return true;
    }

    UE_LOG(LogTemp, Verbose, TEXT("[MouseGrid] RaycastToTerrain → No hit."));
    return false;
}

bool UBuildModeManager::CanPlaceDataTableBuildPiece(FString& OutFailureReason) const
{
    if (!bDataTablePreviewActive || CurrentMode != EBuildMode::Prefab || CurrentToolMode != EBuildToolMode::Place)
    {
        OutFailureReason = TEXT("DataTable preview mode is not active.");
        return false;
    }

    if (bDataTablePreviewIsPrefab)
    {
        OutFailureReason = TEXT("DataTable prefab placement not implemented yet.");
        return false;
    }

    if (DataTablePreviewSourceID.IsNone())
    {
        OutFailureReason = TEXT("DataTable build piece placement failed: missing source ID.");
        return false;
    }

    if (!DataTablePreviewMesh)
    {
        OutFailureReason = TEXT("DataTable build piece placement failed: missing mesh.");
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        OutFailureReason = TEXT("DataTable build piece placement failed: world unavailable.");
        return false;
    }

    if (!IsValid(GhostActor))
    {
        OutFailureReason = TEXT("DataTable build piece placement failed: missing ghost.");
        return false;
    }

    if (!bHoveredCellValid)
    {
        OutFailureReason = TEXT("Hover a valid placement location first.");
        return false;
    }

    FString TerrainFailure;
    if (!ValidateGroundTerrainPlacement(CurrentHoveredVoxel, TerrainFailure))
    {
        OutFailureReason = TerrainFailure;
        if (OutFailureReason.IsEmpty())
        {
            OutFailureReason = TEXT("DataTable build piece placement failed: invalid terrain.");
        }
        return false;
    }

    if (!IsBuildVoxelInsidePlayerClaim(CurrentHoveredVoxel))
    {
        OutFailureReason = TEXT("Cannot build here. This land is not claimed by your colony.");
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] DataTable BuildPiece placement blocked: unclaimed land. SourceID=%s Voxel=(%d,%d,%d)"),
            *DataTablePreviewSourceID.ToString(),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z);
        return false;
    }

    const FTransform PlacementTransform = GhostActor->GetActorTransform();
    if (PlacementTransform.ContainsNaN())
    {
        OutFailureReason = TEXT("DataTable build piece placement failed: invalid transform.");
        return false;
    }

    if (PlacementTransform.GetScale3D().IsNearlyZero())
    {
        OutFailureReason = TEXT("DataTable build piece placement failed: invalid transform scale.");
        return false;
    }

    OutFailureReason.Empty();
    return true;
}

bool UBuildModeManager::CommitDataTableBuildPiecePlacement()
{
    FString FailureReason;
    if (!CanPlaceDataTableBuildPiece(FailureReason))
    {
        if (FailureReason == TEXT("DataTable prefab placement not implemented yet."))
        {
            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] DataTable prefab placement not implemented yet. SourceID=%s"),
                *DataTablePreviewSourceID.ToString());
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] DataTable BuildPiece placement failed. SourceID=%s Reason=%s"),
                *DataTablePreviewSourceID.ToString(),
                *FailureReason);
        }

        if (FailureReason.IsEmpty())
        {
            FailureReason = TEXT("DataTable build piece placement failed.");
        }

        BroadcastFeedback(FailureReason);
        return false;
    }

    UpdateGhost();

    const FTransform PlacementTransform = GhostActor->GetActorTransform();
    UWorld* World = GetWorld();
    if (!World)
    {
        FailureReason = TEXT("DataTable build piece placement failed: world unavailable.");
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] DataTable BuildPiece placement failed. SourceID=%s Reason=%s"),
            *DataTablePreviewSourceID.ToString(),
            *FailureReason);
        BroadcastFeedback(FailureReason);
        return false;
    }

    if (PlacementTransform.ContainsNaN() || PlacementTransform.GetScale3D().IsNearlyZero())
    {
        FailureReason = TEXT("DataTable build piece placement failed: invalid transform.");
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] DataTable BuildPiece placement failed. SourceID=%s Reason=%s"),
            *DataTablePreviewSourceID.ToString(),
            *FailureReason);
        BroadcastFeedback(FailureReason);
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = NAME_None;

    AStaticMeshActor* PlacedActor = World->SpawnActor<AStaticMeshActor>(
        AStaticMeshActor::StaticClass(),
        PlacementTransform,
        SpawnParams);

    if (!PlacedActor)
    {
        FailureReason = TEXT("DataTable build piece placement failed: spawn failed.");
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] DataTable BuildPiece placement failed. SourceID=%s Reason=%s Mesh=%s Location=%s"),
            *DataTablePreviewSourceID.ToString(),
            *FailureReason,
            *DataTablePreviewMesh->GetName(),
            *PlacementTransform.GetLocation().ToString());
        BroadcastFeedback(FailureReason);
        return false;
    }

    UStaticMeshComponent* MeshComp = PlacedActor->GetStaticMeshComponent();
    if (!MeshComp)
    {
        FailureReason = TEXT("DataTable build piece placement failed: mesh component missing.");
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] DataTable BuildPiece placement failed. SourceID=%s Reason=%s Actor=%s"),
            *DataTablePreviewSourceID.ToString(),
            *FailureReason,
            *PlacedActor->GetName());
        PlacedActor->Destroy();
        BroadcastFeedback(FailureReason);
        return false;
    }

    MeshComp->SetMobility(EComponentMobility::Movable);
    MeshComp->SetStaticMesh(DataTablePreviewMesh);
    // Placed actor intentionally keeps the mesh's real materials — no ghost override.
    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Temporary placed actor spawned with real materials: SourceID=%s Mesh=%s Location=%s"),
        *DataTablePreviewSourceID.ToString(),
        *DataTablePreviewMesh->GetName(),
        *PlacementTransform.GetLocation().ToString());
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
    MeshComp->SetGenerateOverlapEvents(false);
    MeshComp->SetCastShadow(true);

    PlacedActor->SetActorTransform(PlacementTransform);
    PlacedActor->SetActorEnableCollision(false);
    PlacedActor->Tags.AddUnique(FName(TEXT("WorldForgeDataTableBuildPiece")));
    PlacedActor->Tags.AddUnique(DataTablePreviewSourceID);

#if WITH_EDITOR
    PlacedActor->SetActorLabel(FString::Printf(TEXT("DT_BuildPiece_%s"), *DataTablePreviewSourceID.ToString()));
#endif

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] DataTable BuildPiece placement succeeded. SourceID=%s Mesh=%s Location=%s Rotation=%s"),
        *DataTablePreviewSourceID.ToString(),
        *DataTablePreviewMesh->GetName(),
        *PlacementTransform.GetLocation().ToString(),
        *PlacementTransform.Rotator().ToString());
    BroadcastFeedback(FString::Printf(TEXT("Placed build piece: %s"), *DataTablePreviewSourceID.ToString()));

    // Patch 2D: Remove confirmed build overlay for this voxel.
    // The DataTable placement path in HandleConfirm returns early before calling
    // CommitSelectedCellsToConfirmedOverlay, so no confirmed cell is typically present here.
    // The call is included so removal fires correctly if the overlay is ever added to this path,
    // and to produce a consistent log regardless.
    {
        UGridSelectionSubsystem* GridSS = World ? World->GetSubsystem<UGridSelectionSubsystem>() : nullptr;
        const bool bOverlayRemoved = GridSS
            ? GridSS->RemoveConfirmedDesignationCell(CurrentHoveredVoxel, EGridDesignationType::Build)
            : false;

        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] [2D] DataTable build placement overlay removal. SourceID=%s Mesh=%s Voxel=(%d,%d,%d) Removed=%s"),
            *DataTablePreviewSourceID.ToString(),
            *DataTablePreviewMesh->GetName(),
            CurrentHoveredVoxel.X,
            CurrentHoveredVoxel.Y,
            CurrentHoveredVoxel.Z,
            bOverlayRemoved ? TEXT("true") : TEXT("false"));
    }

    return true;
}

bool UBuildModeManager::CommitPrefabBuild()
{
    if (bDataTablePreviewActive && CurrentMode == EBuildMode::Prefab && CurrentToolMode == EBuildToolMode::Place)
    {
        return CommitDataTableBuildPiecePlacement();
    }

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

bool UBuildModeManager::SelectNextAvailableFarmSeed(int32 SettlementID)
{
    USettlementManager* SM = GetSettlementManager();
    if (!SM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BuildMode] SelectNextAvailableFarmSeed: SettlementManager unavailable."));
        SelectedFarmCropRow = NAME_None;
        return false;
    }

    const TArray<FName> SeedOrder = SM->GetStartingSeedInventoryOrder(SettlementID);
    for (const FName& CropRow : SeedOrder)
    {
        const int32 Qty = SM->GetStartingSeedInventoryAmount(SettlementID, CropRow);
        if (Qty > 0)
        {
            SelectedFarmCropRow = CropRow;
            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] SelectNextAvailableFarmSeed: auto-selected CropRow=%s Qty=%d Settlement=%d"),
                *CropRow.ToString(), Qty, SettlementID);
            return true;
        }
    }

    SelectedFarmCropRow = NAME_None;
    UE_LOG(LogTemp, Warning,
        TEXT("[BuildMode] SelectNextAvailableFarmSeed: no seeds available. Settlement=%d"),
        SettlementID);
    return false;
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
        int32 WorldTileID = INDEX_NONE;
        FIntPoint WorldTileXY(INDEX_NONE, INDEX_NONE);
        int32 PlayerSettlementID = INDEX_NONE;
        int32 ClaimedWorldTileCount = 0;
        const bool bClaimed = SettlementManager->DoesPlayerSettlementClaimVoxelCellDetailed(
            WorldCell,
            WorldTileID,
            WorldTileXY,
            PlayerSettlementID,
            ClaimedWorldTileCount);

        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Farm claim check: FarmCell=(%d,%d) WorldTile=(%d,%d) WorldTileID=%d SettlementID=%d ClaimedTileCount=%d Claimed=%s"),
            WorldCell.X,
            WorldCell.Y,
            WorldTileXY.X,
            WorldTileXY.Y,
            WorldTileID,
            PlayerSettlementID,
            ClaimedWorldTileCount,
            bClaimed ? TEXT("true") : TEXT("false"));

        if (bClaimed)
        {
            ClaimedWorldCells.Add(WorldCell);
            continue;
        }

        ++SkippedUnclaimedCount;
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Farm cell skipped: unclaimed land. FarmCell=(%d,%d) WorldTile=(%d,%d) WorldTileID=%d SettlementID=%d ClaimedTileCount=%d"),
            WorldCell.X,
            WorldCell.Y,
            WorldTileXY.X,
            WorldTileXY.Y,
            WorldTileID,
            PlayerSettlementID,
            ClaimedWorldTileCount);
    }

    if (ClaimedWorldCells.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Farm zone blocked: no selected cells inside claimed land."));
        BroadcastFeedback(TEXT("Cannot farm here. Selected tiles are outside claimed territory."));
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

    ECropCategory Category = ECropCategory::Grains;
    FName CropRow = NAME_None;
    const int32 RequestedTileCount = UniqueLocalCells.Num();
    // 1 seed per farm tile: each crop plot costs exactly 1 seed to designate.
    const int32 RequiredSeedCount = RequestedTileCount;

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] CommitFarmZone pre-check: RawTiles=%d ClaimedTiles=%d SelectedFarmCropRow=%s RequiredSeeds=%d SettlementID=%d"),
        RequestedWorldCells.Num(),
        ClaimedWorldCells.Num(),
        SelectedFarmCropRow.IsNone() ? TEXT("None") : *SelectedFarmCropRow.ToString(),
        RequiredSeedCount,
        SettlementID);

    if (!SelectedFarmCropRow.IsNone())
    {
        bool bWasCropRowValidated = false;
        const UWildHarvestAdapter* WildHarvestAdapter = GetWorld()
            ? GetWorld()->GetSubsystem<UWildHarvestAdapter>()
            : nullptr;

        if (!IsCropRowKnownToWildHarvest(WildHarvestAdapter, SelectedFarmCropRow, bWasCropRowValidated))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] CommitFarmZone blocked: selected crop row invalid or missing. SelectedFarmCropRow=%s Validated=%s Settlement=%d"),
                *SelectedFarmCropRow.ToString(),
                bWasCropRowValidated ? TEXT("true") : TEXT("false"),
                SettlementID);
            BroadcastFeedback(FString::Printf(
                TEXT("Selected %s seed cannot be planted. Select another seed."),
                *GetCropDisplayName(SelectedFarmCropRow)));
            return false;
        }

        const int32 SelectedSeedQuantity = SettlementManager->GetStartingSeedInventoryAmount(SettlementID, SelectedFarmCropRow);

        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Selected seed inventory check: CropRow=%s Available=%d RequiredSeeds=%d TileCount=%d"),
            *SelectedFarmCropRow.ToString(),
            SelectedSeedQuantity,
            RequiredSeedCount,
            RequestedTileCount);

        if (SelectedSeedQuantity >= RequiredSeedCount)
        {
            CropRow = SelectedFarmCropRow;
            Category = FarmingSub->ResolveCategoryForCropRow(CropRow);

            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] Using selected farm crop seed for zone: CropRow=%s Available=%d RequiredSeeds=%d TileCount=%d Category=%d"),
                *CropRow.ToString(),
                SelectedSeedQuantity,
                RequiredSeedCount,
                RequestedTileCount,
                static_cast<int32>(Category));
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] Selected farm crop seed insufficient at commit: CropRow=%s Settlement=%d Available=%d RequiredSeeds=%d TileCount=%d"),
                *SelectedFarmCropRow.ToString(),
                SettlementID,
                SelectedSeedQuantity,
                RequiredSeedCount,
                RequestedTileCount);
            BroadcastFeedback(FString::Printf(
                TEXT("Creating this farm zone needs %d %s seed(s). Available: %d."),
                RequiredSeedCount,
                *GetCropDisplayName(SelectedFarmCropRow),
                SelectedSeedQuantity));
            return false;
        }
    }

    if (CropRow.IsNone())
    {
        CropRow = FarmingSub->ResolveDefaultPlayerFarmCropRow(SettlementID, Category);

        if (CropRow.IsNone())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] CommitFarmZone blocked: no crop row resolved and no player seed selection. Settlement=%d TileCount=%d RequiredSeeds=%d"),
                SettlementID,
                RequestedTileCount,
                RequiredSeedCount);
            BroadcastFeedback(TEXT("No seeds available. Select a seed before creating a farm zone."));
            return false;
        }

        bool bWasDefaultCropRowValidated = false;
        const UWildHarvestAdapter* WildHarvestAdapter = GetWorld()
            ? GetWorld()->GetSubsystem<UWildHarvestAdapter>()
            : nullptr;

        if (!IsCropRowKnownToWildHarvest(WildHarvestAdapter, CropRow, bWasDefaultCropRowValidated))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] CommitFarmZone blocked: default crop row invalid or missing. CropRow=%s Validated=%s Settlement=%d TileCount=%d RequiredSeeds=%d"),
                *CropRow.ToString(),
                bWasDefaultCropRowValidated ? TEXT("true") : TEXT("false"),
                SettlementID,
                RequestedTileCount,
                RequiredSeedCount);
            BroadcastFeedback(TEXT("Default farm crop is unavailable. Select a seed before creating a farm zone."));
            return false;
        }

        const int32 DefaultSeedQuantity = SettlementManager->GetStartingSeedInventoryAmount(SettlementID, CropRow);
        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Using default crop: CropRow=%s Available=%d RequiredSeeds=%d TileCount=%d"),
            *CropRow.ToString(),
            DefaultSeedQuantity,
            RequiredSeedCount,
            RequestedTileCount);

        if (DefaultSeedQuantity < RequiredSeedCount)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[BuildMode] Default farm crop seed insufficient at commit: CropRow=%s Settlement=%d Available=%d RequiredSeeds=%d TileCount=%d"),
                *CropRow.ToString(),
                SettlementID,
                DefaultSeedQuantity,
                RequiredSeedCount,
                RequestedTileCount);
            BroadcastFeedback(FString::Printf(
                TEXT("Creating this farm zone needs %d %s seed(s). Available: %d."),
                RequiredSeedCount,
                *GetCropDisplayName(CropRow),
                DefaultSeedQuantity));
            return false;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] CommitFarmZone final crop: CropRow=%s Category=%d TileCount=%d RequiredSeeds=%d"),
        *CropRow.ToString(),
        static_cast<int32>(Category),
        RequestedTileCount,
        RequiredSeedCount);

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Calling CreateFarmZoneFromCells: CropRow=%s Category=%d TileCount=%d Settlement=%d"),
        *CropRow.ToString(),
        static_cast<int32>(Category),
        UniqueLocalCells.Num(),
        SettlementID);

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

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Created farm zone ZoneID=%d CropRow=%s TileCount=%d RequiredSeeds=%d"),
        ZoneID,
        *CropRow.ToString(),
        RequestedTileCount,
        RequiredSeedCount);

    // Consume seeds from TemporarySeedInventories now that the zone is confirmed.
    {
        const int32 SeedsBefore = SettlementManager->GetStartingSeedInventoryAmount(SettlementID, CropRow);
        const bool bConsumed = SettlementManager->ConsumeStartingSeedInventory(SettlementID, CropRow, RequiredSeedCount);
        const int32 SeedsAfter = SettlementManager->GetStartingSeedInventoryAmount(SettlementID, CropRow);

        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Farm seed consumption: ZoneID=%d CropRow=%s TileCount=%d SeedsBefore=%d Consuming=%d SeedsAfter=%d Success=%s"),
            ZoneID,
            *CropRow.ToString(),
            RequestedTileCount,
            SeedsBefore,
            RequiredSeedCount,
            SeedsAfter,
            bConsumed ? TEXT("true") : TEXT("false"));

        if (bConsumed)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[BuildMode] Consumed %d %s seed(s). ZoneID=%d CropRow=%s TileCount=%d"),
                RequiredSeedCount,
                *GetCropDisplayName(CropRow),
                ZoneID,
                *CropRow.ToString(),
                RequestedTileCount);
        }
        else
        {
            UE_LOG(LogTemp, Error,
                TEXT("[BuildMode] Farm zone created but seed consumption failed. ZoneID=%d CropRow=%s TileCount=%d RequiredSeeds=%d"),
                ZoneID,
                *CropRow.ToString(),
                RequestedTileCount,
                RequiredSeedCount);
        }
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
        TEXT("[BuildMode] Exact farm zone committed. ZoneID=%d ExactCells=%d RawRequested=%d SkippedUnclaimed=%d BoundsLocal=(%d,%d)->(%d,%d) FarmGridOrigin=(%d,%d) Crop=%s SeedsConsumed=%d Settlement=%d"),
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
        RequiredSeedCount,
        SettlementID);

    // Do not clear BuildMode/GridSelection state here. HandleConfirm() must first
    // copy the active grid selection into the persistent confirmed overlay layer.
    // Cleanup happens in HandleConfirm after CommitSelectedCellsToConfirmedOverlay().

    BroadcastFeedback(FString::Printf(
        TEXT("Farm zone created: %d tile(s) planted with %s (%d seed(s) used)."),
        UniqueLocalCells.Num(),
        *GetCropDisplayName(CropRow),
        RequiredSeedCount));

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

    if (bDataTablePreviewActive && CurrentMode == EBuildMode::Prefab && CurrentToolMode == EBuildToolMode::Place)
    {
        const FVector PreviewWorldPos = VM->GridToWorld(
            FIntPoint(CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y),
            CurrentHoveredVoxel.Z
        );

        // Position GhostMesh (the actual preview mesh) directly at the hover voxel.
        // Clear any stale cell-pool highlights left over from other modes.
        GhostActor->ClearAllHighlights();
        GhostActor->SetActorLocation(PreviewWorldPos);
        GhostActor->SetGhostState(bHoveredCellValid ? EBuildGhostState::Valid : EBuildGhostState::Invalid);

        // Force the GhostMesh visible for DataTable preview mode.
        // ClearAllHighlights() hides GhostMesh and SetGhostState() only sets color,
        // so without this the preview mesh stays invisible.
        GhostActor->SetPreviewMeshVisible(true);

        static double LastDataTablePreviewGhostLogTime = -1000.0;
        if (UWorld* World = GetWorld())
        {
            const double Now = World->GetTimeSeconds();
            if (Now - LastDataTablePreviewGhostLogTime >= 1.0)
            {
                LastDataTablePreviewGhostLogTime = Now;
                UE_LOG(LogTemp, Log,
                    TEXT("[BuildMode] DataTable preview ghost update: SourceID=%s Mesh=%s GhostActor=%s HoverValid=%s Voxel=(%d,%d,%d) WorldLocation=%s"),
                    *DataTablePreviewSourceID.ToString(),
                    DataTablePreviewMesh ? *DataTablePreviewMesh->GetPathName() : TEXT("None"),
                    GhostActor ? TEXT("valid") : TEXT("invalid"),
                    bHoveredCellValid ? TEXT("true") : TEXT("false"),
                    CurrentHoveredVoxel.X,
                    CurrentHoveredVoxel.Y,
                    CurrentHoveredVoxel.Z,
                    *PreviewWorldPos.ToString());
                GhostActor->LogPreviewDiagnostics(TEXT("UpdateGhost DataTablePreview"));
            }
        }
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
        UE_LOG(LogTemp, Warning, TEXT("[CUBE-MINING-VISUAL] RendererInit Result=NoWorld"));
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
        // Option D / Option V: the renderer is a TRUE 200uu voxel-space drawer. Use the
        // live gameplay voxel size (GetRenderVoxelSize, normally 200) so cap tiles and
        // ghosts are placed in the same space the cap terrain query samples. The old
        // hardcoded 100 placed everything at half scale.
        float RendererCellSizeUU = 200.0f;
        if (UWorld* CW = GetWorld())
        {
            if (const UVoxelWorldManager* VM = CW->GetSubsystem<UVoxelWorldManager>())
            {
                const float RVS = VM->GetRenderVoxelSize();
                if (RVS > KINDA_SMALL_NUMBER)
                {
                    RendererCellSizeUU = RVS;
                }
            }
        }
        MiningLayerRenderer->SetCellSizeUU(RendererCellSizeUU);
        MiningLayerRenderer->SetRendererVisible(false);
        UE_LOG(LogTemp, Log, TEXT("[CUBE-MINING-VISUAL] RendererInit Result=Spawned CellSizeUU=%.1f"), RendererCellSizeUU);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[CUBE-MINING-VISUAL] RendererInit Result=Failed"));
    }
}

void UBuildModeManager::DestroyMiningLayerRenderer(const TCHAR* Reason)
{
    RestoreMiningPresentationVisibility();

    if (MiningLayerRenderer)
    {
        MiningLayerRenderer->ClearLayer();
        MiningLayerRenderer->Destroy();
        MiningLayerRenderer = nullptr;
    }

    bMiningLayerAnchorValid = false;
    MiningLayerAnchorVoxel = FIntVector::ZeroValue;
    MiningLayerAnchorZ = MIN_int32;
    MiningLayerAnchorWorldXY = FVector2D::ZeroVector;
    bLastMiningVisualProofLogValid = false;

    UE_LOG(LogTemp, Log,
        TEXT("[CUBE-MINING-VISUAL] RendererClear Reason=%s"),
        Reason ? Reason : TEXT("Unknown"));
}

void UBuildModeManager::UpdateMiningLayerRenderer()
{
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
        MiningLayerAnchorWorldXY = FVector2D::ZeroVector;
        bLastMiningVisualProofLogValid = false;
        return;
    }

    InitializeMiningLayerRenderer();
    if (!MiningLayerRenderer)
    {
        return;
    }

    ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    const int32 ActiveZ = GetActiveVoxelLayerZ();
    const int32 Relative = (LayerView && LayerView->HasActiveVoxelLayer())
        ? LayerView->GetRelativeLayerOffset()
        : 0;
    const bool bUnderground = Relative < 0;

    if (!bHoveredCellValid && !bHasLastValidVoxel && !bMiningLayerAnchorValid)
    {
        MiningLayerRenderer->ClearLayer();
        if (!bLastMiningVisualProofLogValid)
        {
            UE_LOG(LogTemp, Log, TEXT("[CUBE-MINING-VISUAL] RendererClear Reason=NoAnchor"));
            bLastMiningVisualProofLogValid = true;
        }
        return;
    }

    const int32 DioramaRadiusCells = 10;
    const bool bHadMiningLayerAnchor = bMiningLayerAnchorValid;
    const bool bLayerChanged = !bHadMiningLayerAnchor || MiningLayerAnchorZ != ActiveZ;

    if (bLayerChanged || !bMiningLayerAnchorValid)
    {
        FIntVector NewAnchor = bHoveredCellValid
            ? CurrentHoveredVoxel
            : (bHasLastValidVoxel ? LastValidVoxel : MiningLayerAnchorVoxel);
        NewAnchor.Z = ActiveZ;

        MiningLayerAnchorVoxel = NewAnchor;
        MiningLayerAnchorZ = ActiveZ;
        bMiningLayerAnchorValid = true;

        FVector CamLoc = FVector::ZeroVector;
        if (UWorld* AnchorWorld = GetWorld())
        {
            if (APlayerController* AnchorPC = AnchorWorld->GetFirstPlayerController())
            {
                if (APawn* AnchorPawn = AnchorPC->GetPawn())
                {
                    CamLoc = AnchorPawn->GetActorLocation();
                }
            }
        }
        MiningLayerAnchorWorldXY = FVector2D(CamLoc.X, CamLoc.Y);

        MiningLayerRenderer->ForceNextRebuild();

        UE_LOG(LogTemp, Log,
            TEXT("[BuildMode] Mining diorama anchor SET. Anchor=(%d,%d,%d) ActiveZ=%d Reason=%s"),
            MiningLayerAnchorVoxel.X, MiningLayerAnchorVoxel.Y, MiningLayerAnchorVoxel.Z,
            ActiveZ,
            bHadMiningLayerAnchor ? TEXT("LayerChanged") : TEXT("InitialEntry"));

        UE_LOG(LogTemp, Log,
            TEXT("[MineModeCameraAudit] AnchorSet VoxelXY=(%d,%d) AnchorWorldXY=(%.1f,%.1f)"),
            MiningLayerAnchorVoxel.X, MiningLayerAnchorVoxel.Y,
            MiningLayerAnchorWorldXY.X, MiningLayerAnchorWorldXY.Y);
    }

    // MiningLayerAnchorVoxel is already true 200uu voxel space. The renderer,
    // selected cells, and confirmed cells all use that same coordinate space.
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

    if (MiningLayerRenderer && ConfirmedMineCells.Num() > 0)
    {
        const int32 BaseRadius = DioramaRadiusCells;
        const int32 MaxRadius = BaseRadius + 20;
        const int32 ExpandBuffer = 5;

        int32 DesiredRadius = MiningLayerRenderer->VisibleRadius;

        for (const FIntVector& Cell : ConfirmedMineCells)
        {
            if (Cell.Z != ActiveZ) { continue; }
            // Option V: ConfirmedMineCells and CenterVoxel are both true voxel space.
            const int32 DX = FMath::Abs(Cell.X - CenterVoxel.X);
            const int32 DY = FMath::Abs(Cell.Y - CenterVoxel.Y);
            const int32 MaxDist = FMath::Max(DX, DY);
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

    if (bMiningLayerAnchorValid && ConfirmedMineCells.Num() > 0)
    {
        const int32 CurrentRadius = MiningLayerRenderer
            ? MiningLayerRenderer->VisibleRadius
            : DioramaRadiusCells;
        const int32 NudgeTrigger = CurrentRadius - 2;
        FIntVector NudgeOffset = FIntVector::ZeroValue;

        for (const FIntVector& Cell : ConfirmedMineCells)
        {
            if (Cell.Z != ActiveZ) { continue; }
            // Option V: voxel-space distance from the voxel-space center.
            const int32 DX = Cell.X - CenterVoxel.X;
            const int32 DY = Cell.Y - CenterVoxel.Y;

            if (DX > NudgeTrigger) NudgeOffset.X = FMath::Max(NudgeOffset.X, DX - NudgeTrigger);
            else if (DX < -NudgeTrigger) NudgeOffset.X = FMath::Min(NudgeOffset.X, DX + NudgeTrigger);

            if (DY > NudgeTrigger) NudgeOffset.Y = FMath::Max(NudgeOffset.Y, DY - NudgeTrigger);
            else if (DY < -NudgeTrigger) NudgeOffset.Y = FMath::Min(NudgeOffset.Y, DY + NudgeTrigger);
        }

        if (NudgeOffset.X != 0 || NudgeOffset.Y != 0)
        {
            // NudgeOffset is in voxels and the persisted anchor is also voxel-space.
            CenterVoxel.X += NudgeOffset.X;
            CenterVoxel.Y += NudgeOffset.Y;
            MiningLayerAnchorVoxel = CenterVoxel;
            MiningLayerAnchorVoxel.Z = ActiveZ;

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

    TArray<FIntVector> SelectedPreviewCells;
    if (UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem())
    {
        // Selected Mine cells are already true 200uu voxels. Filter to the active
        // layer for this frame before handing them to the renderer.
        TArray<FIntVector> SelectedVoxels;
        GridSelection->GetSelectedVoxelsForRender(SelectedVoxels);
        SelectedPreviewCells.Reserve(SelectedVoxels.Num() + SelectedCells3D.Num());

        for (const FIntVector& V : SelectedVoxels)
        {
            if (V.Z == ActiveZ)
            {
                SelectedPreviewCells.AddUnique(V);
            }
        }
    }

    for (const FIntVector& SelectedCell : SelectedCells3D)
    {
        if (SelectedCell.Z == ActiveZ)
        {
            SelectedPreviewCells.AddUnique(SelectedCell);
        }
    }

    UpdateMiningPresentationVisibility(CenterVoxel, ActiveZ, bUnderground);

    MiningLayerRenderer->SetLayerState(
        CenterVoxel,
        ActiveZ,
        SelectedPreviewCells,
        ConfirmedMineCells);

    const int32 CapTerrain = MiningLayerRenderer->GetLastTerrainInstanceCount();
    const int32 SelectedGhost = MiningLayerRenderer->GetLastSelectedGhostInstanceCount();
    const int32 ConfirmedGhost = MiningLayerRenderer->GetLastConfirmedGhostInstanceCount();
    const int32 OverlaySelected = SelectedPreviewCells.Num();
    const int32 VolumeGhost = bUnderground ? SelectedPreviewCells.Num() : 0;
    const bool bRendererReady = MiningLayerRenderer != nullptr;
    const bool bCutawayActive = bUnderground;

    const bool bProofChanged =
        !bLastMiningVisualProofLogValid ||
        LastMiningVisualProofActiveZ != ActiveZ ||
        LastMiningVisualProofRelative != Relative ||
        LastMiningVisualProofCapTerrain != CapTerrain ||
        LastMiningVisualProofSelectedGhost != SelectedGhost ||
        LastMiningVisualProofConfirmedGhost != ConfirmedGhost ||
        LastMiningVisualProofOverlaySelected != OverlaySelected ||
        LastMiningVisualProofVolumeGhost != VolumeGhost ||
        bLastMiningVisualProofCutaway != bCutawayActive ||
        bLastMiningVisualProofRenderer != bRendererReady;

    if (bProofChanged)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CUBE-MINING-VISUAL] RendererUpdate ActiveZ=%d Relative=%d"),
            ActiveZ,
            Relative);

        UE_LOG(LogTemp, Log,
            TEXT("[CUBE-MINING-VISUAL] ActiveZ=%d Relative=%d Cutaway=%s Renderer=%s CapTerrain=%d SelectedGhost=%d ConfirmedGhost=%d OverlaySelected=%d VolumeGhost=%d"),
            ActiveZ,
            Relative,
            bCutawayActive ? TEXT("true") : TEXT("false"),
            bRendererReady ? TEXT("true") : TEXT("false"),
            CapTerrain,
            SelectedGhost,
            ConfirmedGhost,
            OverlaySelected,
            VolumeGhost);

        bLastMiningVisualProofLogValid = true;
        LastMiningVisualProofActiveZ = ActiveZ;
        LastMiningVisualProofRelative = Relative;
        LastMiningVisualProofCapTerrain = CapTerrain;
        LastMiningVisualProofSelectedGhost = SelectedGhost;
        LastMiningVisualProofConfirmedGhost = ConfirmedGhost;
        LastMiningVisualProofOverlaySelected = OverlaySelected;
        LastMiningVisualProofVolumeGhost = VolumeGhost;
        bLastMiningVisualProofCutaway = bCutawayActive;
        bLastMiningVisualProofRenderer = bRendererReady;
    }
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
    // Option V: VisibleRadius is in true voxels now; use the live 200uu voxel size so the
    // camera bound matches the voxel-space diorama (was a stale 100).
    float CellSizeUU = 200.0f;
    if (UWorld* CW = GetWorld())
    {
        if (const UVoxelWorldManager* VM = CW->GetSubsystem<UVoxelWorldManager>())
        {
            const float RVS = VM->GetRenderVoxelSize();
            if (RVS > KINDA_SMALL_NUMBER) { CellSizeUU = RVS; }
        }
    }
    const float HalfBound = static_cast<float>(MiningLayerRenderer->VisibleRadius / 2 + 1) * CellSizeUU;

    const float AnchorWorldX = MiningLayerAnchorWorldXY.X;
    const float AnchorWorldY = MiningLayerAnchorWorldXY.Y;

    FVector Loc = CamPawn->GetActorLocation();
    const float OriginalX = Loc.X;
    const float OriginalY = Loc.Y;
    const float ClampedX = FMath::Clamp(Loc.X, AnchorWorldX - HalfBound, AnchorWorldX + HalfBound);
    const float ClampedY = FMath::Clamp(Loc.Y, AnchorWorldY - HalfBound, AnchorWorldY + HalfBound);

    if (FMath::Abs(ClampedX - Loc.X) > 0.5f || FMath::Abs(ClampedY - Loc.Y) > 0.5f)
    {
        Loc.X = ClampedX;
        Loc.Y = ClampedY;
        CamPawn->SetActorLocation(Loc, false, nullptr, ETeleportType::TeleportPhysics);
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[MineModeCameraAudit] CameraClamp PawnBefore=(%.1f,%.1f) AnchorWorld=(%.1f,%.1f) PawnAfter=(%.1f,%.1f)"),
        OriginalX,
        OriginalY,
        AnchorWorldX,
        AnchorWorldY,
        ClampedX,
        ClampedY);
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
        Style.TileColor = FLinearColor(0.15f, 0.4f, 0.9f, 0.30f);
        Style.BorderColor = FLinearColor(0.3f, 0.6f, 1.0f, 0.75f);
        Style.HoverColor = FLinearColor(0.5f, 0.7f, 1.0f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.4f, 0.5f, 0.9f, 0.20f);
        Style.IconTexture = BuildIconTexture;
        Style.IconScale = 0.45f;
        GridOverlay->RegisterModeStyle(EBuildToolMode::Place, Style);
    }

    // Farm — green + wheat
    {
        FGridOverlayStyle Style;
        Style.TileColor = FLinearColor(0.2f, 0.7f, 0.15f, 0.30f);
        Style.BorderColor = FLinearColor(0.3f, 0.85f, 0.2f, 0.75f);
        Style.HoverColor = FLinearColor(0.4f, 0.9f, 0.3f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.3f, 0.8f, 0.2f, 0.20f);
        Style.IconTexture = FarmIconTexture;
        Style.IconScale = 0.45f;
        GridOverlay->RegisterModeStyle(EBuildToolMode::Farm, Style);
    }

    // Mine — grey + pickaxe
    {
        FGridOverlayStyle Style;
        Style.TileColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.30f);
        Style.BorderColor = FLinearColor(0.65f, 0.65f, 0.65f, 0.75f);
        Style.HoverColor = FLinearColor(0.8f, 0.8f, 0.7f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.6f, 0.6f, 0.5f, 0.20f);
        Style.IconTexture = MineIconTexture;
        Style.IconScale = 0.40f;
        GridOverlay->RegisterModeStyle(EBuildToolMode::Mine, Style);
    }

    // Dig — orange, no icon (dig ghost handles its own visuals)
    {
        FGridOverlayStyle Style;
        Style.TileColor = FLinearColor(0.9f, 0.6f, 0.1f, 0.30f);
        Style.BorderColor = FLinearColor(1.0f, 0.7f, 0.2f, 0.75f);
        Style.HoverColor = FLinearColor(1.0f, 0.8f, 0.3f, 0.55f);
        Style.DragPreviewColor = FLinearColor(0.9f, 0.7f, 0.2f, 0.20f);
        Style.IconTexture = PrefabIconTexture; // optional, nullptr OK
        Style.IconScale = 0.0f;
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
        UE_LOG(LogTemp, Verbose,
            TEXT("[MouseGrid] GridOverlay hover → OverlayVoxel=(%d,%d,%d) HoverVoxel=(%d,%d,%d) TerrainZ=%.1f Match=%s"),
            CurrentOverlayVoxel.X, CurrentOverlayVoxel.Y, CurrentOverlayVoxel.Z,
            CurrentHoveredVoxel.X, CurrentHoveredVoxel.Y, CurrentHoveredVoxel.Z,
            CurrentHoveredTerrainZ,
            (CurrentOverlayVoxel == CurrentHoveredVoxel) ? TEXT("YES") : TEXT("NO"));
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

    // Option V: CenterVoxel is true voxel space; use the live 200uu voxel size (was 100).
    float CellSize = 200.0f;
    if (const UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
    {
        const float RVS = VM->GetRenderVoxelSize();
        if (RVS > KINDA_SMALL_NUMBER) { CellSize = RVS; }
    }
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
        const bool bOutsideXY =
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
    TSet<FIntVector> FarmClaimedSelectionVoxels;
    int32 FarmUnclaimedPreviewCount = 0;
    const int32 ActiveMineZ = (ActiveMiningZ != INDEX_NONE) ? ActiveMiningZ : GetActiveVoxelLayerZ();

    ULayerViewSubsystem* LayerView = GetLayerViewSubsystem();
    const bool bIsUGMine = (CurrentToolMode == EBuildToolMode::Mine) &&
        LayerView && LayerView->HasActiveVoxelLayer() && LayerView->GetRelativeLayerOffset() < 0;

    for (const FGridSelectionCell& GridCell : GridCells)
    {
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

            if (!IsFarmCellInsidePlayerClaim(ToolCell))
            {
                ++FarmUnclaimedPreviewCount;
                continue;
            }

            FarmPaintedWorldCells.Add(ToolCell);
            FarmClaimedSelectionVoxels.Add(GridCell.VoxelCoord);
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

        if (CurrentToolMode == EBuildToolMode::Mine)
        {
            if (bIsUGMine)
            {
                SelectedCells3D.AddUnique(GridCell.VoxelCoord);
            }
            else
            {
                SelectedCells3D.AddUnique(FIntVector(GridCell.VoxelCoord.X, GridCell.VoxelCoord.Y, ActiveMineZ));
            }
        }
        else
        {
            SelectedCells3D.AddUnique(GridCell.VoxelCoord);
        }

        SelectedCells.AddUnique(ToolCell);

        if (Ctx)
        {
            Ctx->AddCell(ToolCell);
        }
    }

    if (CurrentToolMode == EBuildToolMode::Farm && FarmUnclaimedPreviewCount > 0)
    {
        GridSelection->RetainSelectedCells(FarmClaimedSelectionVoxels);

        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Farm preview filtered unclaimed cells. ClaimedPreview=%d SkippedUnclaimed=%d"),
            FarmClaimedSelectionVoxels.Num(),
            FarmUnclaimedPreviewCount);
    }

    const int32 Count = CurrentToolMode == EBuildToolMode::Farm
        ? FarmPaintedWorldCells.Num()
        : SelectedCells3D.Num();

    OnCellCountChanged.Broadcast(Count);

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        if (bIsUGMine)
        {
            ActiveMiningZ = LayerView->GetActiveVoxelLayerZ();
            GridSelection->SetMiningVolumePreviewCells(SelectedCells3D);

            const FIntVector FirstGrid = GridCells.Num() > 0 ? GridCells[0].VoxelCoord : FIntVector::ZeroValue;
            const FIntVector FirstManager = SelectedCells3D.Num() > 0 ? SelectedCells3D[0] : FIntVector::ZeroValue;
            UE_LOG(LogTemp, Log,
                TEXT("[CUBE-MINING-V2-UG] SyncPreserveUG GridSelected=%d ManagerSelected=%d LayerActiveZ=%d FirstGrid=(%d,%d,%d) FirstManager=(%d,%d,%d)"),
                GridCells.Num(),
                SelectedCells3D.Num(),
                ActiveMiningZ,
                FirstGrid.X, FirstGrid.Y, FirstGrid.Z,
                FirstManager.X, FirstManager.Y, FirstManager.Z);
        }
        else
        {
            // Surface Mine shows flat tiles; volume cubes are for underground only.
            GridSelection->ClearMiningVolumePreviewCells();
            const FIntVector FirstSelected = SelectedCells3D.Num() > 0 ? SelectedCells3D[0] : FIntVector::ZeroValue;
            UE_LOG(LogTemp, Log,
                TEXT("[MineSelectionAudit] SyncSelected Count=%d ActiveZ=%d First=(%d,%d,%d)"),
                SelectedCells3D.Num(),
                ActiveMineZ,
                FirstSelected.X, FirstSelected.Y, FirstSelected.Z);
        }
    }
    else
    {
        GridSelection->ClearMiningVolumePreviewCells();
    }

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        UpdateMiningLayerRenderer();
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

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        UpdateMiningLayerRenderer();
    }
}

void UBuildModeManager::AddOrToggleCurrentTileSelection(bool bToggleExisting)
{
    if (!bHoveredCellValid)
    {
        return;
    }

    const bool bAlreadySelected2D = SelectedCells.Contains(CurrentHoveredCell);
    const bool bAlreadySelected3D = SelectedCells3D.Contains(CurrentHoveredVoxel);
    const bool bWillRemoveExisting =
        bToggleExisting &&
        bToggleSelectedCellsOnReselect &&
        (bAlreadySelected2D || bAlreadySelected3D);

    if (CurrentToolMode == EBuildToolMode::Farm && !bWillRemoveExisting && !IsFarmCellInsidePlayerClaim(CurrentHoveredCell))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[BuildMode] Farm selection rejected: unclaimed land. FarmCell=(%d,%d)"),
            CurrentHoveredCell.X,
            CurrentHoveredCell.Y);
        BroadcastFeedback(TEXT("Cannot farm here. This land is not claimed by your colony."));
        return;
    }

    if (bWillRemoveExisting)
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

    if (CurrentToolMode == EBuildToolMode::Mine)
    {
        UpdateMiningLayerRenderer();
    }
}

bool UBuildModeManager::CommitMineDesignations()
{
    UE_LOG(LogTemp, Log, TEXT("[CUBE-MINING-01-STEP1I] CommitMineDesignations active."));

    if (CurrentToolMode != EBuildToolMode::Mine)
    {
        return false;
    }

    UGridSelectionSubsystem* GridSelection = GetGridSelectionSubsystem();
    if (!GridSelection)
    {
        BroadcastFeedback(TEXT("Grid selection unavailable."));
        return false;
    }

    TArray<FGridSelectionCell> GridCells;

    // CUBE-MINING-V3-FLOOD: confirm the FULL persistent multi-layer plan, not just the
    // current active layer's SelectedCells. GetSelectedCells returns only the layer the
    // player is viewing, which is wiped on every layer change — so committing it created
    // designations for one layer only (the Z=16 surface cells seen in the logs) and the
    // painted Z=15/Z=14/Z=13 stack was silently dropped. The persistent plan carries
    // every committed cell across all layers. Fall back to the active-layer selection
    // only if the persistent plan is empty (e.g. a pure single-layer surface mine that
    // never crossed a layer boundary and so never populated the persistent set).
    GridSelection->GetPersistentMinePlanCells(GridCells);
    const int32 PersistentPlanCount = GridCells.Num();

    int32 ActiveLayerSelectedCount = 0;
    {
        TArray<FGridSelectionCell> ActiveLayerCells;
        GridSelection->GetSelectedCells(ActiveLayerCells);
        ActiveLayerSelectedCount = ActiveLayerCells.Num();

        if (GridCells.Num() == 0)
        {
            GridCells = MoveTemp(ActiveLayerCells);
        }
        else
        {
            // Merge any current-layer cells the player just painted but that have not yet
            // been folded into the persistent set. De-duplicated by voxel coordinate.
            TSet<FIntVector> Seen;
            Seen.Reserve(GridCells.Num());
            for (const FGridSelectionCell& C : GridCells)
            {
                Seen.Add(C.VoxelCoord);
            }
            for (const FGridSelectionCell& C : ActiveLayerCells)
            {
                if (!Seen.Contains(C.VoxelCoord))
                {
                    GridCells.Add(C);
                    Seen.Add(C.VoxelCoord);
                }
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[CUBE-MINING-V3-FLOOD] CommitSource PersistentPlanCells=%d CurrentLayerSelected=%d TotalToCommit=%d"),
        PersistentPlanCount, ActiveLayerSelectedCount, GridCells.Num());

    {
        ULayerViewSubsystem* LV_Commit = GetLayerViewSubsystem();
        const bool bIsUGCommit = LV_Commit && LV_Commit->HasActiveVoxelLayer() && LV_Commit->GetRelativeLayerOffset() < 0;
        if (bIsUGCommit)
        {
            ActiveMiningZ = LV_Commit->GetActiveVoxelLayerZ();
            const FIntVector FirstSelected = GridCells.Num() > 0 ? GridCells[0].VoxelCoord : FIntVector::ZeroValue;
            UE_LOG(LogTemp, Log,
                TEXT("[CUBE-MINING-V2-UG] ConfirmUGZ ActiveZ=%d FirstSelected=(%d,%d,%d)"),
                ActiveMiningZ,
                FirstSelected.X, FirstSelected.Y, FirstSelected.Z);
        }
    }

    const int32 SettlementID = GetEffectiveSettlementID();
    UE_LOG(LogTemp, Log,
        TEXT("[CUBE-MINING-01] CommitMine fired. SelectedCells=%d ActiveMiningZ=%d SettlementID=%d"),
        GridCells.Num(),
        ActiveMiningZ,
        SettlementID);

    if (GridCells.Num() == 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CUBE-MINING-01] CommitMine done. Created=0 SkippedDuplicate=0 SkippedUnclaimed=0 SkippedInvalid=0"));
        BroadcastFeedback(TEXT("No mine tiles selected."));
        return false;
    }

    UMiningSubsystem* MiningSub = GetMiningSubsystem();
    if (!MiningSub)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CUBE-MINING-01] CommitMine done. Created=0 SkippedDuplicate=0 SkippedUnclaimed=0 SkippedInvalid=%d"),
            GridCells.Num());
        BroadcastFeedback(TEXT("Mining system unavailable."));
        return false;
    }

    if (SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CUBE-MINING-01] CommitMine done. Created=0 SkippedDuplicate=0 SkippedUnclaimed=0 SkippedInvalid=%d"),
            GridCells.Num());
        BroadcastFeedback(TEXT("Cannot mine yet. Player settlement is unavailable."));
        return false;
    }

    UVoxelWorldManager* VM = GetVoxelManager();
    UWorldMapSubsystem* WorldMap = GetWorld() ? GetWorld()->GetSubsystem<UWorldMapSubsystem>() : nullptr;

    TArray<FMiningDesignation> ExistingDesignations;
    MiningSub->GetAllDesignations(SettlementID, ExistingDesignations);

    TSet<FIntVector> ExistingMineVoxels;
    ExistingMineVoxels.Reserve(ExistingDesignations.Num());
    for (const FMiningDesignation& Designation : ExistingDesignations)
    {
        if (Designation.State == EMiningDesignationState::Cancelled ||
            Designation.State == EMiningDesignationState::Failed)
        {
            continue;
        }

        ExistingMineVoxels.Add(Designation.GridPosition);
        ExistingMineVoxels.Add(Designation.OriginalGridPosition);
    }

    TSet<FIntVector> SeenTargets;
    TMap<FIntVector, float> CreatedOverlayCells;
    TSet<int32> CreatedLayers;
    int32 CreatedCount = 0;
    int32 SkippedDuplicateCount = 0;
    int32 SkippedUnclaimedCount = 0;
    int32 SkippedInvalidCount = 0;
    const int32 Step = 0;

    for (const FGridSelectionCell& Cell : GridCells)
    {
        // CUBE-MINING-V2-CONFIRM-GRID-TARGET:
        // GridSelection now stores the exact voxel/grid cell the player painted.
        // Do NOT convert X/Y back into strategic terrain-tile coordinates for the
        // final mining target. Terrain tiles are larger than voxel cells, so that
        // collapse caused multi-cell mine selections to become one duplicate target.
        const FIntVector TargetVoxel = Cell.VoxelCoord;

        FVector CellWorldPos = FVector::ZeroVector;
        FIntPoint TerrainTile(INDEX_NONE, INDEX_NONE);
        bool bHasTerrainTile = false;

        if (VM)
        {
            CellWorldPos = VM->GridToWorld(FIntPoint(TargetVoxel.X, TargetVoxel.Y), TargetVoxel.Z);
        }

        if (WorldMap)
        {
            int32 TerrainTileX = INDEX_NONE;
            int32 TerrainTileY = INDEX_NONE;
            if (WorldMap->WorldPositionToTileXY(CellWorldPos, TerrainTileX, TerrainTileY))
            {
                TerrainTile = FIntPoint(TerrainTileX, TerrainTileY);
                bHasTerrainTile = true;
            }
        }

        int32 SurfaceZForLog = INDEX_NONE;
        if (VM)
        {
            VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(TargetVoxel.X, TargetVoxel.Y), SurfaceZForLog);
        }

        UE_LOG(LogTemp, Log,
            TEXT("[CUBE-MINING-V2] MineConfirmGridTarget VisualGrid=(%d,%d,%d) World=(%.1f,%.1f,%.1f) TerrainTile=(%d,%d) HasTerrainTile=%s SurfaceZForLog=%d FinalVoxel=(%d,%d,%d)"),
            Cell.VoxelCoord.X, Cell.VoxelCoord.Y, Cell.VoxelCoord.Z,
            CellWorldPos.X, CellWorldPos.Y, CellWorldPos.Z,
            TerrainTile.X, TerrainTile.Y,
            bHasTerrainTile ? TEXT("true") : TEXT("false"),
            SurfaceZForLog,
            TargetVoxel.X, TargetVoxel.Y, TargetVoxel.Z);

        if (SeenTargets.Contains(TargetVoxel) || ExistingMineVoxels.Contains(TargetVoxel))
        {
            ++SkippedDuplicateCount;
            UE_LOG(LogTemp, Verbose,
                TEXT("[CUBE-MINING-V2] MineConfirmGridTarget skipped duplicate. FinalVoxel=(%d,%d,%d)"),
                TargetVoxel.X, TargetVoxel.Y, TargetVoxel.Z);
            continue;
        }
        SeenTargets.Add(TargetVoxel);

        // CUBE-MINING-V2-CLAIM-TILE:
        // Do not pre-check settlement claim here using TargetVoxel.X/Y.
        // Surface Mine targets are voxel/grid cells, while settlement ownership is
        // validated against the resolved world-map tile inside MiningSubsystem.
        // Keeping the old pre-check here would reject valid CubePrototype selections
        // before CreateMiningDesignation can perform the correct tile-space claim check.

        int32 OutDesignationID = INDEX_NONE;
        EMineDesignationResult CreateResult = EMineDesignationResult::CreateFailed;
        const bool bCreated = MiningSub->CreateMiningDesignationEx(
            SettlementID,
            TargetVoxel,
            Step,
            OutDesignationID,
            CreateResult);

        if (bCreated && OutDesignationID != INDEX_NONE)
        {
            ++CreatedCount;
            CreatedOverlayCells.Add(TargetVoxel, Cell.TerrainZ);
            CreatedLayers.Add(TargetVoxel.Z);
            UE_LOG(LogTemp, Log,
                TEXT("[CUBE-MINING-01] Designation created. Voxel=(%d,%d,%d) DesignationID=%d"),
                TargetVoxel.X,
                TargetVoxel.Y,
                TargetVoxel.Z,
                OutDesignationID);
        }
        else
        {
            // CUBE-MINING-V3-CLAIM: bucket the failure by its real reason instead of
            // counting every failure as "invalid" (which previously hid unclaimed-land
            // rejections).
            switch (CreateResult)
            {
            case EMineDesignationResult::Unclaimed:
                ++SkippedUnclaimedCount;
                break;
            case EMineDesignationResult::Duplicate:
                ++SkippedDuplicateCount;
                break;
            case EMineDesignationResult::InvalidTarget:
            case EMineDesignationResult::CreateFailed:
            default:
                ++SkippedInvalidCount;
                break;
            }

            UE_LOG(LogTemp, Warning,
                TEXT("[CUBE-MINING-V2] MineConfirmGridTarget create failed. FinalVoxel=(%d,%d,%d) Reason=%d"),
                TargetVoxel.X,
                TargetVoxel.Y,
                TargetVoxel.Z,
                static_cast<int32>(CreateResult));
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[CUBE-MINING-01] CommitMine done. Created=%d SkippedDuplicate=%d SkippedUnclaimed=%d SkippedInvalid=%d"),
        CreatedCount,
        SkippedDuplicateCount,
        SkippedUnclaimedCount,
        SkippedInvalidCount);

    // CUBE-MINING-V3-FLOOD: single proof line separating the three concepts so a log
    // reader can confirm the FULL plan committed, not just the active layer.
    UE_LOG(LogTemp, Log,
        TEXT("[CUBE-MINING-V3-FLOOD] ConfirmMinePlan CurrentLayerSelected=%d PersistentPlanCells=%d Created=%d SkippedDuplicate=%d SkippedUnclaimed=%d SkippedInvalid=%d"),
        ActiveLayerSelectedCount,
        PersistentPlanCount,
        CreatedCount,
        SkippedDuplicateCount,
        SkippedUnclaimedCount,
        SkippedInvalidCount);

    UE_LOG(LogTemp, Log,
        TEXT("[BuildMode] Mine designations committed. Created=%d Selected=%d SkippedDuplicate=%d SkippedUnclaimed=%d SkippedInvalid=%d Settlement=%d"),
        CreatedCount,
        GridCells.Num(),
        SkippedDuplicateCount,
        SkippedUnclaimedCount,
        SkippedInvalidCount,
        SettlementID);

    if (CreatedCount <= 0)
    {
        BroadcastFeedback(TEXT("No mining designations created."));
        return false;
    }

    if (GridOverlay && CreatedOverlayCells.Num() > 0)
    {
        GridOverlay->SetConfirmedDesignationCells(EGridDesignationType::Mine, CreatedOverlayCells);
        UE_LOG(LogTemp, Log,
            TEXT("[CUBE-MINING-01] Overlay confirmed. ConfirmedCells=%d"),
            CreatedOverlayCells.Num());
    }

    if (GridSelection)
    {
        GridSelection->ClearSelectedCells();
        GridSelection->ClearPreviewCells();
        GridSelection->ClearMiningVolumePreviewCells();
        // CUBE-MINING-V3-FLOOD: the whole multi-layer plan is now backend designations,
        // so retire the planning anchors. Without this the next mine session would
        // inherit every cell from this one as a phantom anchor.
        GridSelection->ClearMinePlan();
    }

    const int32 DistinctLayerCount = CreatedLayers.Num();

    SelectedCells.Empty();
    SelectedCells3D.Empty();
    FarmPaintedWorldCells.Empty();

    if (UPlayerBuildContext* Ctx = GetBuildContext())
    {
        Ctx->ClearSelection();
    }

    BroadcastFeedback(FString::Printf(
        TEXT("Mining designated: %d voxel(s) across %d layer(s)."),
        CreatedCount,
        DistinctLayerCount));

    UpdateMiningLayerRenderer();

    return true;
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