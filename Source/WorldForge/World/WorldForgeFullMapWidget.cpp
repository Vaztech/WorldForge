#include "WorldForgeFullMapWidget.h"

#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "WorldMapSubsystem.h"
#include "ColonyManager.h"
#include "../Voxel/VoxelWorldManager.h"

namespace
{
    static UTexture2D* GetPreferredFullMapTexture(UWorldMapSubsystem* MapSubsystem, bool bPreferDetailedMap)
    {
        if (!MapSubsystem)
        {
            return nullptr;
        }

        if (bPreferDetailedMap)
        {
            if (UTexture2D* DetailedTexture = MapSubsystem->GetOrCreateDetailedMapTexture())
            {
                return DetailedTexture;
            }
        }

        if (UTexture2D* BiomeTexture = MapSubsystem->GetOrCreateMapTexture())
        {
            return BiomeTexture;
        }

        if (!bPreferDetailedMap)
        {
            return MapSubsystem->GetOrCreateDetailedMapTexture();
        }

        return nullptr;
    }

    static bool IsTileClaimedByCivilization(UWorldMapSubsystem* MapSubsystem, int32 TileIndex)
    {
        if (!MapSubsystem || TileIndex == INDEX_NONE)
        {
            return false;
        }

        FWorldTileData Tile;
        if (!MapSubsystem->GetTileData(TileIndex, Tile))
        {
            return false;
        }

        return Tile.ControllingCivID >= 0;
    }
}

// ============================================================
void UWorldForgeFullMapWidget::NativeConstruct()
{
    Super::NativeConstruct();

    MapSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    CachedPlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

    UE_LOG(LogTemp, Warning,
        TEXT("[FullMapWidget] Constructed | IMG_Map=%s BRD_SelectedCell=%s TXT_SelectedCell=%s BTN_Confirm=%s BTN_Close=%s"),
        IMG_Map ? TEXT("OK") : TEXT("NULL"),
        BRD_SelectedCell ? TEXT("OK") : TEXT("NULL"),
        TXT_SelectedCell ? TEXT("OK") : TEXT("NULL"),
        BTN_Confirm ? TEXT("OK") : TEXT("NULL"),
        BTN_Close ? TEXT("OK") : TEXT("NULL"));

    if (IMG_Map && MapSubsystem)
    {
        constexpr bool bPreferDetailedMap = false;

        if (UTexture2D* MapTexture = GetPreferredFullMapTexture(MapSubsystem, bPreferDetailedMap))
        {
            IMG_Map->SetBrushFromTexture(MapTexture, true);
        }
    }

    if (BRD_SelectedCell)
    {
        BRD_SelectedCell->SetRenderOpacity(0.0f);
    }

    // ── Core buttons ─────────────────────────────────────────────
    if (BTN_Confirm)
    {
        BTN_Confirm->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleConfirmClicked);
        BTN_Confirm->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleConfirmClicked);
    }

    if (BTN_Close)
    {
        BTN_Close->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleCloseClicked);
        BTN_Close->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleCloseClicked);
    }

    // ── Overlay toggle checkboxes (new DF-style multi-layer) ─────
    if (CHK_Political)
    {
        CHK_Political->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandlePoliticalToggled);
        CHK_Political->OnCheckStateChanged.AddDynamic(this, &UWorldForgeFullMapWidget::HandlePoliticalToggled);
    }

    if (CHK_Economy)
    {
        CHK_Economy->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleEconomyToggled);
        CHK_Economy->OnCheckStateChanged.AddDynamic(this, &UWorldForgeFullMapWidget::HandleEconomyToggled);
    }

    if (CHK_Cosmic)
    {
        CHK_Cosmic->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleCosmicToggled);
        CHK_Cosmic->OnCheckStateChanged.AddDynamic(this, &UWorldForgeFullMapWidget::HandleCosmicToggled);
    }

    if (CHK_Trade)
    {
        CHK_Trade->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleTradeToggled);
        CHK_Trade->OnCheckStateChanged.AddDynamic(this, &UWorldForgeFullMapWidget::HandleTradeToggled);
    }

    if (CHK_Underground)
    {
        CHK_Underground->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleUndergroundToggled);
        CHK_Underground->OnCheckStateChanged.AddDynamic(this, &UWorldForgeFullMapWidget::HandleUndergroundToggled);
    }

    if (CHK_Fog)
    {
        CHK_Fog->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleFogToggled);
        CHK_Fog->OnCheckStateChanged.AddDynamic(this, &UWorldForgeFullMapWidget::HandleFogToggled);
    }

    if (CHK_Resource)
    {
        CHK_Resource->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleResourceToggled);
        CHK_Resource->OnCheckStateChanged.AddDynamic(this, &UWorldForgeFullMapWidget::HandleResourceToggled);
    }

    // ── Legacy overlay buttons (backwards compat) ────────────────
    if (BTN_Terrain)
    {
        BTN_Terrain->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleTerrainOverlayClicked);
        BTN_Terrain->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleTerrainOverlayClicked);
    }

    if (BTN_Political)
    {
        BTN_Political->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandlePoliticalOverlayClicked);
        BTN_Political->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandlePoliticalOverlayClicked);
    }

    if (BTN_Economy)
    {
        BTN_Economy->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleEconomyOverlayClicked);
        BTN_Economy->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleEconomyOverlayClicked);
    }

    if (BTN_Cosmic)
    {
        BTN_Cosmic->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleCosmicOverlayClicked);
        BTN_Cosmic->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleCosmicOverlayClicked);
    }

    if (BTN_Trade)
    {
        BTN_Trade->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleTradeOverlayClicked);
        BTN_Trade->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleTradeOverlayClicked);
    }

    if (BTN_Underground)
    {
        BTN_Underground->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleUndergroundOverlayClicked);
        BTN_Underground->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleUndergroundOverlayClicked);
    }

    bHasValidSelection = false;
    SelectedTileX = INDEX_NONE;
    SelectedTileY = INDEX_NONE;
    SelectedTileIndex = INDEX_NONE;

    ViewCenterX = 0.5f;
    ViewCenterY = 0.5f;
    Zoom = 1.0f;

    if (IMG_Map)
    {
        IMG_Map->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        ApplyMapTransform();
    }

    // Sync checkbox states to the subsystem's current layer mask.
    SyncCheckboxesToOverlayState();

    EnsureValidInitialSelection();
    UpdateSelectionVisual();
    UpdateSelectionText();

    SetIsFocusable(true);
    EnterMapSelectionInputMode();
    SetKeyboardFocus();
}

// ============================================================
void UWorldForgeFullMapWidget::NativeDestruct()
{
    bPanUp = false;
    bPanDown = false;
    bPanLeft = false;
    bPanRight = false;
    bPanFast = false;

    RestoreGameInputMode();
    Super::NativeDestruct();
}

// ============================================================
void UWorldForgeFullMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    float PanX = 0.0f;
    float PanY = 0.0f;

    if (bPanRight) PanX += 1.0f;
    if (bPanLeft)  PanX -= 1.0f;
    if (bPanDown)  PanY += 1.0f;
    if (bPanUp)    PanY -= 1.0f;

    if (PanX != 0.0f || PanY != 0.0f)
    {
        const float Speed = PanSpeed
            * (bPanFast ? FastPanMultiplier : 1.0f)
            * InDeltaTime
            / FMath::Max(Zoom, 0.001f);

        ViewCenterX += PanX * Speed;
        ViewCenterY += PanY * Speed;

        ClampViewCenter();
        ApplyMapTransform();
        UpdateSelectionVisual();
    }
}

// ============================================================
FReply UWorldForgeFullMapWidget::NativeOnMouseButtonDown(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent)
{
    if (!MapSubsystem || !IMG_Map)
    {
        return FReply::Unhandled();
    }

    const FGeometry MapGeometry = IMG_Map->GetCachedGeometry();
    const FVector2D LocalPos = MapGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
    const FVector2D MapSize = MapGeometry.GetLocalSize();

    if (MapSize.X <= 0.0f || MapSize.Y <= 0.0f)
    {
        return FReply::Unhandled();
    }

    const bool bInsideMap =
        LocalPos.X >= 0.0f && LocalPos.X <= MapSize.X &&
        LocalPos.Y >= 0.0f && LocalPos.Y <= MapSize.Y;

    if (!bInsideMap)
    {
        return FReply::Unhandled();
    }

    const FVector2D WidgetUV(
        LocalPos.X / MapSize.X,
        LocalPos.Y / MapSize.Y);

    const FVector2D UV = WidgetUVToWorldUV(WidgetUV);

    FIntPoint ResolvedTileXY(INDEX_NONE, INDEX_NONE);
    FIntPoint SelectedVoxelXY = FIntPoint::ZeroValue;
    int32 ResolvedIndex = INDEX_NONE;

    if (!MapSubsystem->WorldUVToBestSpawnTile(UV, ResolvedTileXY, ResolvedIndex, SelectedVoxelXY))
    {
        bHasValidSelection = false;
        SelectedTileX = INDEX_NONE;
        SelectedTileY = INDEX_NONE;
        SelectedTileIndex = INDEX_NONE;

        if (BRD_SelectedCell)
        {
            BRD_SelectedCell->SetRenderOpacity(0.0f);
        }

        UpdateSelectionText();
        return FReply::Handled();
    }

    SelectedTileX = ResolvedTileXY.X;
    SelectedTileY = ResolvedTileXY.Y;
    SelectedTileIndex = ResolvedIndex;
    bHasValidSelection = true;

    UE_LOG(LogTemp, Log,
        TEXT("[FullMap] Selected tile: X=%d Y=%d Index=%d WorldUV=(%.3f,%.3f) Voxel=(%d,%d) Zoom=%.2f"),
        SelectedTileX,
        SelectedTileY,
        SelectedTileIndex,
        UV.X,
        UV.Y,
        SelectedVoxelXY.X,
        SelectedVoxelXY.Y,
        Zoom);

    UpdateSelectionVisual();
    UpdateSelectionText();
    return FReply::Handled();
}

// ============================================================
FReply UWorldForgeFullMapWidget::NativeOnMouseWheel(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent)
{
    const float Delta = InMouseEvent.GetWheelDelta();
    Zoom = FMath::Clamp(Zoom + Delta * ZoomStep, MinZoom, MaxZoom);

    ClampViewCenter();
    ApplyMapTransform();
    UpdateSelectionVisual();

    return FReply::Handled();
}

// ============================================================
FReply UWorldForgeFullMapWidget::NativeOnKeyDown(
    const FGeometry& InGeometry,
    const FKeyEvent& InKeyEvent)
{
    if (!MapSubsystem)
    {
        return FReply::Unhandled();
    }

    const FKey Key = InKeyEvent.GetKey();
    const bool bFastMove = InKeyEvent.IsShiftDown();

    if (Key == EKeys::W) { bPanUp = true; bPanFast = bFastMove; return FReply::Handled(); }
    if (Key == EKeys::S) { bPanDown = true; bPanFast = bFastMove; return FReply::Handled(); }
    if (Key == EKeys::A) { bPanLeft = true; bPanFast = bFastMove; return FReply::Handled(); }
    if (Key == EKeys::D) { bPanRight = true; bPanFast = bFastMove; return FReply::Handled(); }

    if (Key == EKeys::Up) { MoveSelectionBy(0, -1, bFastMove); return FReply::Handled(); }
    if (Key == EKeys::Down) { MoveSelectionBy(0, 1, bFastMove); return FReply::Handled(); }
    if (Key == EKeys::Left) { MoveSelectionBy(-1, 0, bFastMove); return FReply::Handled(); }
    if (Key == EKeys::Right) { MoveSelectionBy(1, 0, bFastMove); return FReply::Handled(); }

    if (Key == EKeys::Enter) { HandleConfirmClicked(); return FReply::Handled(); }
    if (Key == EKeys::Escape) { HandleCloseClicked(); return FReply::Handled(); }

    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ============================================================
FReply UWorldForgeFullMapWidget::NativeOnKeyUp(
    const FGeometry& InGeometry,
    const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();

    if (Key == EKeys::W) bPanUp = false;
    if (Key == EKeys::S) bPanDown = false;
    if (Key == EKeys::A) bPanLeft = false;
    if (Key == EKeys::D) bPanRight = false;

    if (!bPanUp && !bPanDown && !bPanLeft && !bPanRight)
    {
        bPanFast = false;
    }

    return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

// ============================================================
void UWorldForgeFullMapWidget::HandleConfirmClicked()
{
    if (!bHasValidSelection)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FullMap] Confirm clicked with no valid tile selected."));
        return;
    }

    if (!MapSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("[FullMap] Confirm failed: MapSubsystem missing."));
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[FullMap] Confirm failed: World missing."));
        return;
    }

    UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelMgr)
    {
        UE_LOG(LogTemp, Error, TEXT("[FullMap] Confirm failed: VoxelWorldManager missing."));
        return;
    }

    const FIntPoint DesiredCell(SelectedTileX, SelectedTileY);

    // ── Spawn rule: reject civ-claimed territory ─────────────────
    if (IsTileClaimedByCivilization(MapSubsystem, SelectedTileIndex))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FullMap] Confirm blocked: selected tile %d is already controlled."),
            SelectedTileIndex);

        if (TXT_SelectedCell)
        {
            TXT_SelectedCell->SetText(FText::FromString(
                TEXT("Cannot start here: this region is already claimed by another civilization.")
            ));
        }

        return;
    }

    FVector SpawnWorldLocation = FVector::ZeroVector;
    FIntPoint ResolvedCell = DesiredCell;

    const bool bFoundSpawn =
        VoxelMgr->TryGetNearestSpawnLocationForTerrainCell(
            DesiredCell,
            SpawnWorldLocation,
            ResolvedCell,
            48,
            VoxelMgr->TerrainSurfaceZOffset
        );

    // ── Spawn rule: check the RESOLVED tile too ──────────────────
    int32 ResolvedIndex = INDEX_NONE;
    if (bFoundSpawn && MapSubsystem->TileXYToIndex(ResolvedCell.X, ResolvedCell.Y, ResolvedIndex))
    {
        if (IsTileClaimedByCivilization(MapSubsystem, ResolvedIndex))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[FullMap] Confirm blocked: resolved tile %d is already controlled."),
                ResolvedIndex);

            if (TXT_SelectedCell)
            {
                TXT_SelectedCell->SetText(FText::FromString(
                    TEXT("Cannot start here: this region is already claimed by another civilization.")
                ));
            }

            return;
        }
    }

    if (!bFoundSpawn)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[FullMap] Confirm failed: (%d,%d) non-spawnable, no fallback found."),
            DesiredCell.X,
            DesiredCell.Y
        );
        return;
    }

    int32 FinalIndex = INDEX_NONE;
    if (!MapSubsystem->TileXYToIndex(ResolvedCell.X, ResolvedCell.Y, FinalIndex))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[FullMap] Confirm failed: unable to resolve final tile index for (%d,%d)."),
            ResolvedCell.X,
            ResolvedCell.Y
        );
        return;
    }

    SelectedTileX = ResolvedCell.X;
    SelectedTileY = ResolvedCell.Y;
    SelectedTileIndex = FinalIndex;
    bHasValidSelection = true;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[FullMap] Confirm success: Desired=(%d,%d) Resolved=(%d,%d) Index=%d Spawn=(%.1f, %.1f, %.1f)"),
        DesiredCell.X,
        DesiredCell.Y,
        SelectedTileX,
        SelectedTileY,
        SelectedTileIndex,
        SpawnWorldLocation.X,
        SpawnWorldLocation.Y,
        SpawnWorldLocation.Z
    );

    MapSubsystem->SetSelectedStartTile(SelectedTileX, SelectedTileY, SelectedTileIndex);
    MapSubsystem->RevealTile(SelectedTileIndex, EKnowledgeLevel::Surveyed, 1.0f);
    MapSubsystem->MarkMapTextureDirty();
    MapSubsystem->MarkDetailedMapTextureDirty();

    if (IMG_Map)
    {
        constexpr bool bPreferDetailedMap = false;

        if (UTexture2D* MapTexture = GetPreferredFullMapTexture(MapSubsystem, bPreferDetailedMap))
        {
            IMG_Map->SetBrushFromTexture(MapTexture, true);
        }
    }

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
    {
        if (APawn* PlayerPawn = PC->GetPawn())
        {
            const FVector CameraLocation = SpawnWorldLocation + FVector(0.0f, 0.0f, 1200.0f);
            PlayerPawn->SetActorLocation(CameraLocation);
            PlayerPawn->SetActorRotation(FRotator(-65.0f, 0.0f, 0.0f));
            PC->SetViewTarget(PlayerPawn);
        }
        else
        {
            PC->SetInitialLocationAndRotation(
                SpawnWorldLocation + FVector(0.0f, 0.0f, 1200.0f),
                FRotator(-65.0f, 0.0f, 0.0f)
            );
        }
    }

    for (TActorIterator<AColonyManager> It(World); It; ++It)
    {
        if (AColonyManager* ColonyManager = *It)
        {
            if (IsValid(ColonyManager))
            {
                ColonyManager->BeginBootstrapNow();
                break;
            }
        }
    }

    RemoveFromParent();
}

// ============================================================
void UWorldForgeFullMapWidget::HandleCloseClicked()
{
    RemoveFromParent();
}

// ============================================================
// Checkbox toggle handlers (new DF-style multi-overlay)
// ============================================================

void UWorldForgeFullMapWidget::HandlePoliticalToggled(bool bIsChecked)
{
    SetOverlayLayerAndRefresh(WorldForgeOverlay::Political, bIsChecked);
}

void UWorldForgeFullMapWidget::HandleEconomyToggled(bool bIsChecked)
{
    SetOverlayLayerAndRefresh(WorldForgeOverlay::Economy, bIsChecked);
}

void UWorldForgeFullMapWidget::HandleCosmicToggled(bool bIsChecked)
{
    SetOverlayLayerAndRefresh(WorldForgeOverlay::Cosmic, bIsChecked);
}

void UWorldForgeFullMapWidget::HandleTradeToggled(bool bIsChecked)
{
    SetOverlayLayerAndRefresh(WorldForgeOverlay::Trade, bIsChecked);
}

void UWorldForgeFullMapWidget::HandleUndergroundToggled(bool bIsChecked)
{
    SetOverlayLayerAndRefresh(WorldForgeOverlay::Underground, bIsChecked);
}

void UWorldForgeFullMapWidget::HandleFogToggled(bool bIsChecked)
{
    SetOverlayLayerAndRefresh(WorldForgeOverlay::Fog, bIsChecked);
}

void UWorldForgeFullMapWidget::HandleResourceToggled(bool bIsChecked)
{
    SetOverlayLayerAndRefresh(WorldForgeOverlay::Resource, bIsChecked);
}

// ============================================================
void UWorldForgeFullMapWidget::SetOverlayLayerAndRefresh(int32 LayerBit, bool bActive)
{
    if (!MapSubsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FullMapWidget] Cannot toggle overlay: MapSubsystem missing."));
        return;
    }

    MapSubsystem->SetOverlayLayerActive(LayerBit, bActive);

    RefreshMapTexture();
    UpdateSelectionVisual();
    UpdateSelectionText();

    UE_LOG(LogTemp, Log, TEXT("[FullMapWidget] Overlay layer 0x%02X %s. Active=0x%02X"),
        LayerBit,
        bActive ? TEXT("ON") : TEXT("OFF"),
        MapSubsystem->GetActiveOverlayLayers());
}

// ============================================================
// Legacy button handlers (solo-mode backwards compat)
// ============================================================

void UWorldForgeFullMapWidget::HandleTerrainOverlayClicked()
{
    SetMapOverlayMode(EWorldMapOverlayMode::Terrain);
}

void UWorldForgeFullMapWidget::HandlePoliticalOverlayClicked()
{
    SetMapOverlayMode(EWorldMapOverlayMode::Political);
}

void UWorldForgeFullMapWidget::HandleEconomyOverlayClicked()
{
    SetMapOverlayMode(EWorldMapOverlayMode::Economy);
}

void UWorldForgeFullMapWidget::HandleCosmicOverlayClicked()
{
    SetMapOverlayMode(EWorldMapOverlayMode::Cosmic);
}

void UWorldForgeFullMapWidget::HandleTradeOverlayClicked()
{
    SetMapOverlayMode(EWorldMapOverlayMode::Trade);
}

void UWorldForgeFullMapWidget::HandleUndergroundOverlayClicked()
{
    SetMapOverlayMode(EWorldMapOverlayMode::Underground);
}

// ============================================================
void UWorldForgeFullMapWidget::SetMapOverlayMode(EWorldMapOverlayMode NewMode)
{
    if (!MapSubsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FullMapWidget] Cannot set overlay: MapSubsystem missing."));
        return;
    }

    MapSubsystem->SetOverlayMode(NewMode);

    RefreshMapTexture();
    SyncCheckboxesToOverlayState();
    UpdateSelectionVisual();
    UpdateSelectionText();

    UE_LOG(LogTemp, Log, TEXT("[FullMapWidget] Overlay mode changed to %d (layers=0x%02X)"),
        static_cast<int32>(NewMode), MapSubsystem->GetActiveOverlayLayers());
}

// ============================================================
void UWorldForgeFullMapWidget::RefreshMapTexture()
{
    if (!IMG_Map || !MapSubsystem)
    {
        return;
    }

    constexpr bool bPreferDetailedMap = false;

    if (UTexture2D* MapTexture = GetPreferredFullMapTexture(MapSubsystem, bPreferDetailedMap))
    {
        IMG_Map->SetBrushFromTexture(MapTexture, true);
    }
}

// ============================================================
void UWorldForgeFullMapWidget::SyncCheckboxesToOverlayState()
{
    if (!MapSubsystem)
    {
        return;
    }

    const int32 Layers = MapSubsystem->GetActiveOverlayLayers();

    auto SetCheck = [](UCheckBox* Box, bool bOn)
        {
            if (Box)
            {
                Box->SetIsChecked(bOn);
            }
        };

    SetCheck(CHK_Political, (Layers & WorldForgeOverlay::Political) != 0);
    SetCheck(CHK_Economy, (Layers & WorldForgeOverlay::Economy) != 0);
    SetCheck(CHK_Cosmic, (Layers & WorldForgeOverlay::Cosmic) != 0);
    SetCheck(CHK_Trade, (Layers & WorldForgeOverlay::Trade) != 0);
    SetCheck(CHK_Underground, (Layers & WorldForgeOverlay::Underground) != 0);
    SetCheck(CHK_Fog, (Layers & WorldForgeOverlay::Fog) != 0);
    SetCheck(CHK_Resource, (Layers & WorldForgeOverlay::Resource) != 0);
}

// ============================================================
void UWorldForgeFullMapWidget::UpdateSelectionVisual()
{
    if (!BRD_SelectedCell || !IMG_Map || !MapSubsystem || !bHasValidSelection)
    {
        return;
    }

    const FVector2D MapSize = IMG_Map->GetCachedGeometry().GetLocalSize();
    if (MapSize.X <= 0.0f || MapSize.Y <= 0.0f)
    {
        return;
    }

    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();
    if (GridWidth <= 0 || GridHeight <= 0)
    {
        return;
    }

    const float TileU = (static_cast<float>(SelectedTileX) + 0.5f)
        / static_cast<float>(GridWidth);
    const float TileV = (static_cast<float>(SelectedTileY) + 0.5f)
        / static_cast<float>(GridHeight);

    const float ScreenU = 0.5f + Zoom * (TileU - ViewCenterX);
    const float ScreenV = 0.5f + Zoom * (TileV - ViewCenterY);

    if (ScreenU < 0.0f || ScreenU > 1.0f || ScreenV < 0.0f || ScreenV > 1.0f)
    {
        BRD_SelectedCell->SetRenderOpacity(0.0f);
        return;
    }

    const float CellW = MapSize.X * Zoom / static_cast<float>(GridWidth);
    const float CellH = MapSize.Y * Zoom / static_cast<float>(GridHeight);

    const FVector2D Position(
        ScreenU * MapSize.X - CellW * 0.5f,
        ScreenV * MapSize.Y - CellH * 0.5f);

    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(BRD_SelectedCell->Slot))
    {
        CanvasSlot->SetPosition(Position);
        CanvasSlot->SetSize(FVector2D(CellW, CellH));
    }

    BRD_SelectedCell->SetRenderOpacity(1.0f);
}

// ============================================================
void UWorldForgeFullMapWidget::UpdateSelectionText()
{
    if (!TXT_SelectedCell)
    {
        return;
    }

    if (!bHasValidSelection)
    {
        TXT_SelectedCell->SetText(FText::FromString(TEXT("No Tile Selected")));
        UpdateResourceTooltip();
        return;
    }

    const FString Str = FString::Printf(
        TEXT("Tile X=%d Y=%d Index=%d | Layers=0x%02X"),
        SelectedTileX,
        SelectedTileY,
        SelectedTileIndex,
        MapSubsystem ? MapSubsystem->GetActiveOverlayLayers() : 0);

    TXT_SelectedCell->SetText(FText::FromString(Str));

    UpdateResourceTooltip();
}

// ============================================================
void UWorldForgeFullMapWidget::ApplyMapTransform()
{
    if (!IMG_Map)
    {
        return;
    }

    const FVector2D MapSize = IMG_Map->GetCachedGeometry().GetLocalSize();

    FWidgetTransform T;
    T.Scale = FVector2D(Zoom, Zoom);
    T.Translation = FVector2D(
        -(ViewCenterX - 0.5f) * MapSize.X * Zoom,
        -(ViewCenterY - 0.5f) * MapSize.Y * Zoom);

    IMG_Map->SetRenderTransform(T);
}

// ============================================================
FVector2D UWorldForgeFullMapWidget::WidgetUVToWorldUV(const FVector2D& WidgetUV) const
{
    const float VisW = 1.0f / FMath::Max(Zoom, 0.001f);
    const float MinU = ViewCenterX - VisW * 0.5f;
    const float MinV = ViewCenterY - VisW * 0.5f;

    return FVector2D(
        FMath::Clamp(MinU + WidgetUV.X * VisW, 0.0f, 1.0f),
        FMath::Clamp(MinV + WidgetUV.Y * VisW, 0.0f, 1.0f));
}

// ============================================================
void UWorldForgeFullMapWidget::ClampViewCenter()
{
    const float Half = 0.5f / FMath::Max(Zoom, 0.001f);
    ViewCenterX = FMath::Clamp(ViewCenterX, Half, 1.0f - Half);
    ViewCenterY = FMath::Clamp(ViewCenterY, Half, 1.0f - Half);
}

// ============================================================
void UWorldForgeFullMapWidget::EnterMapSelectionInputMode()
{
    if (!CachedPlayerController)
    {
        return;
    }

    FInputModeUIOnly InputMode;
    InputMode.SetWidgetToFocus(TakeWidget());
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

    CachedPlayerController->SetInputMode(InputMode);
    CachedPlayerController->bShowMouseCursor = true;
    CachedPlayerController->SetIgnoreLookInput(true);
    CachedPlayerController->SetIgnoreMoveInput(true);

    bInputLockedToUI = true;

    UE_LOG(LogTemp, Log, TEXT("[FullMapWidget] Entered UI-only input mode."));
}

// ============================================================
void UWorldForgeFullMapWidget::RestoreGameInputMode()
{
    if (!bInputLockedToUI || !CachedPlayerController)
    {
        return;
    }

    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
    InputMode.SetHideCursorDuringCapture(false);

    CachedPlayerController->SetInputMode(InputMode);
    CachedPlayerController->bShowMouseCursor = true;
    CachedPlayerController->SetIgnoreLookInput(false);
    CachedPlayerController->SetIgnoreMoveInput(false);

    bInputLockedToUI = false;

    UE_LOG(LogTemp, Log, TEXT("[FullMapWidget] Restored game-and-ui input mode."));
}

// ============================================================
void UWorldForgeFullMapWidget::MoveSelectionBy(int32 DeltaX, int32 DeltaY, bool bFastMove)
{
    if (!MapSubsystem)
    {
        return;
    }

    EnsureValidInitialSelection();

    if (!bHasValidSelection)
    {
        return;
    }

    const int32 Step = bFastMove
        ? FMath::Max(1, KeyboardMoveStepFast)
        : FMath::Max(1, KeyboardMoveStep);

    int32 CandidateX = SelectedTileX + (DeltaX * Step);
    int32 CandidateY = SelectedTileY + (DeltaY * Step);

    CandidateX = FMath::Clamp(CandidateX, 0, FMath::Max(0, MapSubsystem->GetGridWidth() - 1));
    CandidateY = FMath::Clamp(CandidateY, 0, FMath::Max(0, MapSubsystem->GetGridHeight() - 1));

    FIntPoint FinalCell(CandidateX, CandidateY);

    if (UWorld* World = GetWorld())
    {
        if (UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (!VoxelMgr->IsSpawnableTerrainCell(FinalCell))
            {
                FIntPoint NearestSpawnable = FinalCell;
                if (VoxelMgr->FindNearestSpawnableTerrainCell(FinalCell, NearestSpawnable, 16))
                {
                    FinalCell = NearestSpawnable;
                }
            }
        }
    }

    int32 TileIndex = INDEX_NONE;
    if (MapSubsystem->TileXYToIndex(FinalCell.X, FinalCell.Y, TileIndex))
    {
        SelectedTileX = FinalCell.X;
        SelectedTileY = FinalCell.Y;
        SelectedTileIndex = TileIndex;
        bHasValidSelection = true;

        const float TileU =
            (static_cast<float>(SelectedTileX) + 0.5f) /
            static_cast<float>(MapSubsystem->GetGridWidth());

        const float TileV =
            (static_cast<float>(SelectedTileY) + 0.5f) /
            static_cast<float>(MapSubsystem->GetGridHeight());

        const float VisHalf = 0.5f / FMath::Max(Zoom, 0.001f);
        const bool bOffscreenX = (TileU < ViewCenterX - VisHalf) || (TileU > ViewCenterX + VisHalf);
        const bool bOffscreenY = (TileV < ViewCenterY - VisHalf) || (TileV > ViewCenterY + VisHalf);

        if (bOffscreenX || bOffscreenY)
        {
            ViewCenterX = TileU;
            ViewCenterY = TileV;
            ClampViewCenter();
            ApplyMapTransform();
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[FullMap] Arrow moved selection: X=%d Y=%d Index=%d Step=%d"),
            SelectedTileX,
            SelectedTileY,
            SelectedTileIndex,
            Step
        );
    }

    UpdateSelectionVisual();
    UpdateSelectionText();
}

// ============================================================
void UWorldForgeFullMapWidget::ClampSelectionToMap()
{
    if (!MapSubsystem)
    {
        return;
    }

    SelectedTileX = FMath::Clamp(SelectedTileX, 0, FMath::Max(0, MapSubsystem->GetGridWidth() - 1));
    SelectedTileY = FMath::Clamp(SelectedTileY, 0, FMath::Max(0, MapSubsystem->GetGridHeight() - 1));
}

// ============================================================
void UWorldForgeFullMapWidget::EnsureValidInitialSelection()
{
    if (!MapSubsystem || bHasValidSelection)
    {
        return;
    }

    int32 ExistingX = INDEX_NONE;
    int32 ExistingY = INDEX_NONE;
    int32 ExistingIndex = INDEX_NONE;

    if (MapSubsystem->GetSelectedStartTile(ExistingX, ExistingY, ExistingIndex))
    {
        SelectedTileX = ExistingX;
        SelectedTileY = ExistingY;
        SelectedTileIndex = ExistingIndex;
        bHasValidSelection = true;
        return;
    }

    FIntPoint DesiredCell(
        FMath::Max(0, MapSubsystem->GetGridWidth() / 2),
        FMath::Max(0, MapSubsystem->GetGridHeight() / 2)
    );

    FIntPoint FinalCell = DesiredCell;

    if (UWorld* World = GetWorld())
    {
        if (UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (!VoxelMgr->IsSpawnableTerrainCell(FinalCell))
            {
                FIntPoint NearestSpawnable = FinalCell;
                if (VoxelMgr->FindNearestSpawnableTerrainCell(FinalCell, NearestSpawnable, 64))
                {
                    FinalCell = NearestSpawnable;
                }
            }
        }
    }

    if (MapSubsystem->TileXYToIndex(FinalCell.X, FinalCell.Y, SelectedTileIndex))
    {
        SelectedTileX = FinalCell.X;
        SelectedTileY = FinalCell.Y;
        bHasValidSelection = true;
    }
}

// ============================================================
// Batch 3: Resource tooltip
//
// When the Resource overlay is active and a tile is selected,
// this populates TXT_ResourceInfo with the per-category breakdown.
// If TXT_ResourceInfo doesn't exist in the WBP yet, this is a no-op.
// ============================================================
void UWorldForgeFullMapWidget::UpdateResourceTooltip()
{
    if (!TXT_ResourceInfo)
    {
        return;
    }

    if (!MapSubsystem || !bHasValidSelection ||
        !MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Resource))
    {
        TXT_ResourceInfo->SetText(FText::GetEmpty());
        return;
    }

    FWorldTileData Tile;
    if (!MapSubsystem->GetTileData(SelectedTileIndex, Tile))
    {
        TXT_ResourceInfo->SetText(FText::GetEmpty());
        return;
    }

    const int32 IdxMetal = static_cast<int32>(EResourceCategory::Metal);
    const int32 IdxCrystal = static_cast<int32>(EResourceCategory::Crystal);
    const int32 IdxFossil = static_cast<int32>(EResourceCategory::Fossil);

    const auto& Density = Tile.ResourceSurvey.DensityByCategory;
    const auto& Depth = Tile.ResourceSurvey.PeakDepthByCategory;

    const float MetalPct = Density.IsValidIndex(IdxMetal) ? FMath::Clamp(Density[IdxMetal] * 100.0f, 0.0f, 100.0f) : 0.0f;
    const float CrystalPct = Density.IsValidIndex(IdxCrystal) ? FMath::Clamp(Density[IdxCrystal] * 100.0f, 0.0f, 100.0f) : 0.0f;
    const float FossilPct = Density.IsValidIndex(IdxFossil) ? FMath::Clamp(Density[IdxFossil] * 100.0f, 0.0f, 100.0f) : 0.0f;
    const float RichnessPct = FMath::Clamp(Tile.ResourceSurvey.OverallRichness * 100.0f, 0.0f, 100.0f);

    const bool bHasAny = (MetalPct > 0.1f || CrystalPct > 0.1f || FossilPct > 0.1f);

    if (!bHasAny)
    {
        TXT_ResourceInfo->SetText(FText::FromString(TEXT("No resource deposits detected.")));
        return;
    }

    FString Info = FString::Printf(TEXT("Resources (Richness: %.0f%%)\n"), RichnessPct);

    if (MetalPct > 0.1f)
    {
        const float MetalDepth = Depth.IsValidIndex(IdxMetal) ? Depth[IdxMetal] : 0.0f;
        Info += FString::Printf(TEXT("  Metal: %.0f%% | Peak depth: %.0f\n"), MetalPct, MetalDepth);
    }

    if (CrystalPct > 0.1f)
    {
        const float CrystalDepth = Depth.IsValidIndex(IdxCrystal) ? Depth[IdxCrystal] : 0.0f;
        Info += FString::Printf(TEXT("  Crystal: %.0f%% | Peak depth: %.0f\n"), CrystalPct, CrystalDepth);
    }

    if (FossilPct > 0.1f)
    {
        const float FossilDepth = Depth.IsValidIndex(IdxFossil) ? Depth[IdxFossil] : 0.0f;
        Info += FString::Printf(TEXT("  Fossil: %.0f%% | Peak depth: %.0f"), FossilPct, FossilDepth);
    }

    TXT_ResourceInfo->SetText(FText::FromString(Info));
}