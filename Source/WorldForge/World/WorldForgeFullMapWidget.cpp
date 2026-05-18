#include "WorldForgeFullMapWidget.h"

#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "WorldMapSubsystem.h"
#include "ColonyManager.h"
#include "SettlementManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
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

    static FSlateBrush MakeRuntimeBoxBrush(const FLinearColor& Tint, const FMargin& Margin = FMargin(0.0f))
    {
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::Box;
        Brush.Margin = Margin;
        Brush.TintColor = FSlateColor(Tint);
        return Brush;
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
        constexpr bool bPreferDetailedMap = true;

        if (UTexture2D* MapTexture = GetPreferredFullMapTexture(MapSubsystem, bPreferDetailedMap))
        {
            IMG_Map->SetBrushFromTexture(MapTexture, false);
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

    // -- Bottom command-bar toggle buttons --------------------------------
    //
    // BTN_MapLayers   -> toggles PNL_Overlays    (map layer checkboxes)
    // BTN_SiteDetails -> toggles SBX_TileDetails (selected site info)
    // BTN_MapKey      -> toggles PNL_MapKey      (color legend)
    //
    // Bound here in C++ NativeConstruct so we don't rely on fragile
    // Blueprint event wiring. The handlers below only change panel visibility;
    // they do not alter selection or HandleConfirmClicked behavior.
    // ------------------------------------------------------------------
    if (BTN_MapLayers)
    {
        BTN_MapLayers->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleMapLayersClicked);
        BTN_MapLayers->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleMapLayersClicked);
    }

    if (BTN_SiteDetails)
    {
        BTN_SiteDetails->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleSiteDetailsClicked);
        BTN_SiteDetails->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleSiteDetailsClicked);
    }

    if (BTN_MapKey)
    {
        BTN_MapKey->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleMapKeyClicked);
        BTN_MapKey->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleMapKeyClicked);
    }

    if (TXT_DebugClaimTileLabel)
    {
        TXT_DebugClaimTileLabel->SetText(FText::FromString(TEXT("Claim Tile")));
    }

    if (BTN_DebugClaimTile)
    {
        BTN_DebugClaimTile->OnClicked.RemoveDynamic(this, &UWorldForgeFullMapWidget::HandleDebugClaimTileClicked);
        BTN_DebugClaimTile->OnClicked.AddDynamic(this, &UWorldForgeFullMapWidget::HandleDebugClaimTileClicked);
    }

    // Runtime layout/style normalization keeps the C++ class resilient to
    // stale CanvasPanel offsets in the Widget Blueprint.
    ApplyRuntimeStyles();

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

    ApplyRuntimeLayout(GetCachedGeometry());

    if (IMG_Map)
    {
        if (UCanvasPanelSlot* MapCanvasSlot = Cast<UCanvasPanelSlot>(IMG_Map->Slot))
        {
            MapCanvasSlot->SetAutoSize(false);
        }

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

    ApplyRuntimeLayout(MyGeometry);
    ApplyRuntimeStyles();
    ApplyResponsiveTextScaling(MyGeometry);
    UpdateSelectionVisual();

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
int32 UWorldForgeFullMapWidget::NativePaint(
    const FPaintArgs& Args,
    const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32 LayerId,
    const FWidgetStyle& InWidgetStyle,
    bool bParentEnabled) const
{
    int32 NextLayer = Super::NativePaint(
        Args,
        AllottedGeometry,
        MyCullingRect,
        OutDrawElements,
        LayerId,
        InWidgetStyle,
        bParentEnabled);

    // Keep a paint-pass fallback for the very first frames where IMG_Map may
    // not yet have live cached geometry. Resize alignment now uses the live
    // IMG_Map geometry first because this cached paint geometry can be one
    // layout behind after a PIE/game-window size change.
    if (IMG_Map)
    {
        CachedIMGMapPaintGeometry = IMG_Map->GetCachedGeometry();
    }

    DrawMapInsetShadow(AllottedGeometry, OutDrawElements, NextLayer);

    if (bShowTileGridOverlay)
    {
        DrawTileGridOverlay(AllottedGeometry, OutDrawElements, NextLayer);
    }

    if (bHasHoveredTile)
    {
        FString HoverReason;
        const bool bHoverValid = MapSubsystem && MapSubsystem->IsTileValidForEmbark(HoveredTileX, HoveredTileY, HoverReason);
        DrawTileOutline(AllottedGeometry, OutDrawElements, NextLayer, HoveredTileX, HoveredTileY, bHoverValid ? HoveredTileColor : InvalidTileColor, 2.0f);
    }

    if (bShowCapitalMarkers)
    {
        DrawCapitalMarkers(AllottedGeometry, OutDrawElements, NextLayer);
    }

    if (bShowSelectedTileRedX)
    {
        DrawSelectedTileX(AllottedGeometry, OutDrawElements, NextLayer);
    }

    DrawTileDetailsPanelBorder(AllottedGeometry, OutDrawElements, NextLayer);

    return NextLayer;
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

    int32 ClickTileX = INDEX_NONE;
    int32 ClickTileY = INDEX_NONE;
    int32 ClickTileIndex = INDEX_NONE;

    if (!MouseEventToTileXY(InMouseEvent, ClickTileX, ClickTileY, ClickTileIndex, true))
    {
        return FReply::Unhandled();
    }

    SetSelectedTileFromXY(ClickTileX, ClickTileY, ClickTileIndex);

    UE_LOG(LogTemp, Log,
        TEXT("[FullMap] Selected exact tile: X=%d Y=%d Index=%d Valid=%s Reason=%s"),
        SelectedTileX,
        SelectedTileY,
        SelectedTileIndex,
        bSelectedTileEmbarkValid ? TEXT("true") : TEXT("false"),
        *SelectedTileBlockReason);

    UpdateSelectionVisual();
    UpdateSelectionText();
    return FReply::Handled();
}

// ============================================================
FReply UWorldForgeFullMapWidget::NativeOnMouseMove(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent)
{
    if (!MapSubsystem || !IMG_Map)
    {
        return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
    }

    int32 NewTileX = INDEX_NONE;
    int32 NewTileY = INDEX_NONE;
    int32 NewTileIndex = INDEX_NONE;

    if (MouseEventToTileXY(InMouseEvent, NewTileX, NewTileY, NewTileIndex))
    {
        const bool bChanged = !bHasHoveredTile ||
            HoveredTileX != NewTileX ||
            HoveredTileY != NewTileY ||
            HoveredTileIndex != NewTileIndex;

        HoveredTileX = NewTileX;
        HoveredTileY = NewTileY;
        HoveredTileIndex = NewTileIndex;
        bHasHoveredTile = true;

        if (bChanged && TXT_ResourceInfo)
        {
            TXT_ResourceInfo->SetText(FText::FromString(
                MapSubsystem->BuildTileSurveyText(HoveredTileX, HoveredTileY, bDebugRevealTileSurveyInfo)));
        }
    }
    else
    {
        bHasHoveredTile = false;
    }

    return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
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

    if (Key == EKeys::C)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FullMapDebugClaim] C key pressed."));
        TryDebugClaimHoveredOrSelectedTile();
        return FReply::Handled();
    }

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
bool UWorldForgeFullMapWidget::TryDebugClaimHoveredOrSelectedTile()
{
    LastDebugClaimMessage.Empty();

    int32 TargetTileX = INDEX_NONE;
    int32 TargetTileY = INDEX_NONE;
    int32 TargetTileIndex = INDEX_NONE;
    const bool bUsingHoveredTile = bHasHoveredTile && HoveredTileIndex != INDEX_NONE;

    if (bUsingHoveredTile)
    {
        TargetTileX = HoveredTileX;
        TargetTileY = HoveredTileY;
        TargetTileIndex = HoveredTileIndex;
    }
    else if (bHasValidSelection && SelectedTileIndex != INDEX_NONE)
    {
        TargetTileX = SelectedTileX;
        TargetTileY = SelectedTileY;
        TargetTileIndex = SelectedTileIndex;
    }
    else
    {
        LastDebugClaimMessage = TEXT("Claim failed: hover or select a tile first.");
        UpdateSelectionText();
        UE_LOG(LogTemp, Warning, TEXT("[FullMapDebugClaim] %s"), *LastDebugClaimMessage);
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        LastDebugClaimMessage = TEXT("Claim failed: world missing.");
        UpdateSelectionText();
        UE_LOG(LogTemp, Warning, TEXT("[FullMapDebugClaim] %s"), *LastDebugClaimMessage);
        return false;
    }

    USettlementManager* SettlementMgr = World->GetSubsystem<USettlementManager>();
    if (!SettlementMgr)
    {
        LastDebugClaimMessage = TEXT("Claim failed: settlement manager missing.");
        UpdateSelectionText();
        UE_LOG(LogTemp, Warning, TEXT("[FullMapDebugClaim] %s"), *LastDebugClaimMessage);
        return false;
    }

    // Temporary developer-only test hook. Polished player-facing claim UI will come later.
    FString OutReason;
    if (!SettlementMgr->ClaimWorldTileForPlayerSettlement(TargetTileIndex, OutReason))
    {
        if (OutReason.IsEmpty())
        {
            OutReason = TEXT("unknown reason");
        }

        LastDebugClaimMessage = FString::Printf(TEXT("Claim failed: %s"), *OutReason);
        UpdateSelectionText();
        UE_LOG(LogTemp, Warning, TEXT("[FullMapDebugClaim] %s"), *LastDebugClaimMessage);
        return false;
    }

    SetSelectedTileFromXY(TargetTileX, TargetTileY, TargetTileIndex);
    LastDebugClaimMessage = FString::Printf(TEXT("Claimed tile %d."), TargetTileIndex);

    UE_LOG(LogTemp, Warning, TEXT("[FullMapDebugClaim] %s"), *LastDebugClaimMessage);

    RefreshMapTexture();
    UpdateSelectionVisual();
    UpdateSelectionText();
    return true;
}

// ============================================================
void UWorldForgeFullMapWidget::HandleDebugClaimTileClicked()
{
    UE_LOG(LogTemp, Warning, TEXT("[FullMapDebugClaim] Claim Tile button clicked."));
    TryDebugClaimHoveredOrSelectedTile();
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

    if (!bSelectedTileEmbarkValid)
    {
        const FString Reason = SelectedTileBlockReason.IsEmpty() ? TEXT("Invalid embark tile.") : SelectedTileBlockReason;
        UE_LOG(LogTemp, Warning, TEXT("[FullMap] Confirm blocked: %s"), *Reason);
        if (TXT_SelectedCell)
        {
            TXT_SelectedCell->SetText(FText::FromString(FString::Printf(TEXT("Cannot embark here: %s"), *Reason)));
        }
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
        constexpr bool bPreferDetailedMap = true;

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
// Bottom command-bar toggle handlers.
//
// Map Layers and Map Key are mutually exclusive popup panels that
// appear above the bottom bar.  Opening one auto-closes the other
// so the map isn't cluttered.  Site Details is independent (top-right).
// ============================================================

// Bottom-bar panel toggle handlers. These only show/hide existing panels;
// they do not touch map generation, selection rules, or confirm behavior.
void UWorldForgeFullMapWidget::HandleMapLayersClicked()
{
    if (!PNL_Overlays)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FullMapWidget] BTN_MapLayers clicked but PNL_Overlays not bound."));
        return;
    }

    const bool bCurrentlyVisible = PNL_Overlays->IsVisible();
    PNL_Overlays->SetVisibility(bCurrentlyVisible
        ? ESlateVisibility::Collapsed
        : ESlateVisibility::SelfHitTestInvisible);

    // Close the Map Key popup when opening Map Layers (mutual exclusion).
    if (!bCurrentlyVisible && PNL_MapKey && PNL_MapKey->IsVisible())
    {
        PNL_MapKey->SetVisibility(ESlateVisibility::Collapsed);
    }

    UE_LOG(LogTemp, Log, TEXT("[FullMapWidget] Map Layers toggled %s"),
        bCurrentlyVisible ? TEXT("OFF") : TEXT("ON"));
}

void UWorldForgeFullMapWidget::HandleSiteDetailsClicked()
{
    if (!SBX_TileDetails)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FullMapWidget] BTN_SiteDetails clicked but SBX_TileDetails not bound."));
        return;
    }

    const bool bCurrentlyVisible = SBX_TileDetails->IsVisible();
    SBX_TileDetails->SetVisibility(bCurrentlyVisible
        ? ESlateVisibility::Collapsed
        : ESlateVisibility::SelfHitTestInvisible);

    UE_LOG(LogTemp, Log, TEXT("[FullMapWidget] Site Details toggled %s"),
        bCurrentlyVisible ? TEXT("OFF") : TEXT("ON"));
}

void UWorldForgeFullMapWidget::HandleMapKeyClicked()
{
    if (!PNL_MapKey)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FullMapWidget] BTN_MapKey clicked but PNL_MapKey not bound."));
        return;
    }

    const bool bCurrentlyVisible = PNL_MapKey->IsVisible();
    PNL_MapKey->SetVisibility(bCurrentlyVisible
        ? ESlateVisibility::Collapsed
        : ESlateVisibility::SelfHitTestInvisible);

    // Close the Map Layers popup when opening Map Key (mutual exclusion).
    if (!bCurrentlyVisible && PNL_Overlays && PNL_Overlays->IsVisible())
    {
        PNL_Overlays->SetVisibility(ESlateVisibility::Collapsed);
    }

    UE_LOG(LogTemp, Log, TEXT("[FullMapWidget] Map Key toggled %s"),
        bCurrentlyVisible ? TEXT("OFF") : TEXT("ON"));
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
    ApplyDetailTabHighlight();

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

    constexpr bool bPreferDetailedMap = true;

    if (UTexture2D* MapTexture = GetPreferredFullMapTexture(MapSubsystem, bPreferDetailedMap))
    {
            IMG_Map->SetBrushFromTexture(MapTexture, false);
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
    if (!BRD_SelectedCell)
    {
        return;
    }

    if (!IMG_Map || !MapSubsystem || !bHasValidSelection)
    {
        BRD_SelectedCell->SetRenderOpacity(0.0f);
        return;
    }

    FMapDisplayGeometry MapDisplayGeometry;
    if (!GetCurrentMapDisplayGeometry(MapDisplayGeometry))
    {
        BRD_SelectedCell->SetRenderOpacity(0.0f);
        return;
    }

    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();
    if (GridWidth <= 0 || GridHeight <= 0)
    {
        BRD_SelectedCell->SetRenderOpacity(0.0f);
        return;
    }

    FVector2D TileCenterIMGMapLocal;
    if (!TileXYToMapDisplayLocal(SelectedTileX, SelectedTileY, MapDisplayGeometry, TileCenterIMGMapLocal))
    {
        BRD_SelectedCell->SetRenderOpacity(0.0f);
        return;
    }

    const float CellW = bUseStableFullMapCoordinates
        ? MapDisplayGeometry.MapRectSize.X / static_cast<float>(GridWidth)
        : MapDisplayGeometry.MapRectSize.X * Zoom / static_cast<float>(GridWidth);
    const float CellH = bUseStableFullMapCoordinates
        ? MapDisplayGeometry.MapRectSize.Y / static_cast<float>(GridHeight)
        : MapDisplayGeometry.MapRectSize.Y * Zoom / static_cast<float>(GridHeight);

    // Locator border uses the same tile -> map-display-local conversion as
    // the painted selected X. Convert through absolute space into the root
    // widget's local canvas coordinates before moving the border slot.
    const FVector2D TileMinIMGMapLocal(TileCenterIMGMapLocal.X - CellW * 0.5f, TileCenterIMGMapLocal.Y - CellH * 0.5f);
    const FVector2D TileMaxIMGMapLocal(TileCenterIMGMapLocal.X + CellW * 0.5f, TileCenterIMGMapLocal.Y + CellH * 0.5f);
    const FGeometry WidgetGeometry = GetCachedGeometry();
    const FVector2D TileMinWidgetLocal = WidgetGeometry.AbsoluteToLocal(
        MapDisplayGeometry.IMGMapGeometry.LocalToAbsolute(TileMinIMGMapLocal));
    const FVector2D TileMaxWidgetLocal = WidgetGeometry.AbsoluteToLocal(
        MapDisplayGeometry.IMGMapGeometry.LocalToAbsolute(TileMaxIMGMapLocal));

    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(BRD_SelectedCell->Slot))
    {
        CanvasSlot->SetPosition(TileMinWidgetLocal);
        CanvasSlot->SetSize(FVector2D(
            FMath::Max(1.0f, TileMaxWidgetLocal.X - TileMinWidgetLocal.X),
            FMath::Max(1.0f, TileMaxWidgetLocal.Y - TileMinWidgetLocal.Y)));
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

    FString Str = MapSubsystem
        ? MapSubsystem->BuildTileSurveyText(SelectedTileX, SelectedTileY, bDebugRevealTileSurveyInfo)
        : FString::Printf(TEXT("Tile X=%d Y=%d Index=%d"), SelectedTileX, SelectedTileY, SelectedTileIndex);

    Str += FString::Printf(TEXT("\nLayers=0x%02X"), MapSubsystem ? MapSubsystem->GetActiveOverlayLayers() : 0);

    // If the selected tile is near an active settlement/capital, show it
    // directly in the selection text. This makes it clear that political
    // territory and the active settlement spawn point are different things.
    if (UWorld* World = GetWorld())
    {
        if (const USettlementManager* SettlementMgr = World->GetSubsystem<USettlementManager>())
        {
            const FSettlementState* BestSettlement = nullptr;
            int32 BestDist = MAX_int32;

            for (const FSettlementState& Settlement : SettlementMgr->GetAllSettlements())
            {
                const FIntPoint MarkerCell = Settlement.WorldOrigin + FIntPoint(1, 1);
                const int32 Dist = FMath::Abs(MarkerCell.X - SelectedTileX) + FMath::Abs(MarkerCell.Y - SelectedTileY);

                if (Dist < BestDist)
                {
                    BestDist = Dist;
                    BestSettlement = &Settlement;
                }
            }

            if (BestSettlement && BestDist <= 4)
            {
                const bool bIsPlayer = SettlementMgr->HasPlayerSettlement() &&
                    BestSettlement->SettlementID == SettlementMgr->GetPlayerSettlementID();

                Str += FString::Printf(
                    TEXT("\n%s: %s | SettlementID=%d CivID=%d Origin=(%d,%d)"),
                    bIsPlayer ? TEXT("Player Colony") : TEXT("NPC Capital"),
                    *BestSettlement->Name,
                    BestSettlement->SettlementID,
                    BestSettlement->OwnerCivID,
                    BestSettlement->WorldOrigin.X,
                    BestSettlement->WorldOrigin.Y);
            }
        }
    }

    if (!LastDebugClaimMessage.IsEmpty())
    {
        Str += TEXT("\n");
        Str += LastDebugClaimMessage;
    }

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

    // Stable coordinate mode keeps the full map image untransformed so
    // mouse clicks, selected red X, and capital markers all use the same
    // exact 0..1 map coordinate space. This avoids the old mismatch where
    // the image render transform and marker math could drift apart.
    if (bUseStableFullMapCoordinates)
    {
        IMG_Map->SetRenderTransform(FWidgetTransform());
        return;
    }

    FMapDisplayGeometry MapDisplayGeometry;
    if (!GetCurrentMapDisplayGeometry(MapDisplayGeometry))
    {
        return;
    }

    FWidgetTransform T;
    T.Scale = FVector2D(Zoom, Zoom);
    T.Translation = FVector2D(
        -(ViewCenterX - 0.5f) * MapDisplayGeometry.MapRectSize.X * Zoom,
        -(ViewCenterY - 0.5f) * MapDisplayGeometry.MapRectSize.Y * Zoom);

    IMG_Map->SetRenderTransform(T);
}

// ============================================================
void UWorldForgeFullMapWidget::ApplyRuntimeLayout(const FGeometry& WidgetGeometry)
{
    const float BarHeight = ResolveBottomBarHeight();

    if (PNL_BottomBlocker)
    {
        PNL_BottomBlocker->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (PNL_BottomBar)
    {
        if (UCanvasPanelSlot* BottomBarSlot = Cast<UCanvasPanelSlot>(PNL_BottomBar->Slot))
        {
            BottomBarSlot->SetAutoSize(false);
            BottomBarSlot->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
            BottomBarSlot->SetAlignment(FVector2D(0.0f, 1.0f));
            BottomBarSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, BarHeight));
            BottomBarSlot->SetZOrder(90);
        }
    }

    if (IMG_Map)
    {
        if (UCanvasPanelSlot* MapSlot = Cast<UCanvasPanelSlot>(IMG_Map->Slot))
        {
            MapSlot->SetAutoSize(false);
            MapSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
            MapSlot->SetAlignment(FVector2D::ZeroVector);
            MapSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, BarHeight));
            MapSlot->SetZOrder(0);
        }
    }

    if (BRD_SelectedCell)
    {
        if (UCanvasPanelSlot* SelectionSlot = Cast<UCanvasPanelSlot>(BRD_SelectedCell->Slot))
        {
            SelectionSlot->SetZOrder(40);
        }
    }

    auto RaiseCanvasWidget = [](UWidget* Widget, int32 ZOrder)
    {
        if (Widget)
        {
            if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
            {
                Slot->SetZOrder(ZOrder);
            }
        }
    };

    RaiseCanvasWidget(PNL_Overlays, 100);
    RaiseCanvasWidget(SBX_TileDetails, 100);
    RaiseCanvasWidget(PNL_MapKey, 100);
}

// ============================================================
void UWorldForgeFullMapWidget::ApplyRuntimeStyles()
{
    if (PNL_BottomBar)
    {
        const FLinearColor BottomBarColor(0.030f, 0.030f, 0.040f, 1.000f);
        PNL_BottomBar->SetBrush(MakeRuntimeBoxBrush(BottomBarColor));
        PNL_BottomBar->SetBrushColor(BottomBarColor);
        PNL_BottomBar->SetPadding(FMargin(10.0f, 7.0f));
    }

    if (PNL_TileDetails)
    {
        const FLinearColor PanelColor(0.0f, 0.0f, 0.0f, 0.65f);
        PNL_TileDetails->SetBrush(MakeRuntimeBoxBrush(PanelColor));
        PNL_TileDetails->SetBrushColor(PanelColor);
        PNL_TileDetails->SetPadding(FMargin(12.0f, 10.0f));
        PNL_TileDetails->SetContentColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
    }

    if (PNL_Overlays)
    {
        const FLinearColor PopupColor(0.015f, 0.018f, 0.023f, 0.88f);
        PNL_Overlays->SetBrush(MakeRuntimeBoxBrush(PopupColor));
        PNL_Overlays->SetBrushColor(PopupColor);
        PNL_Overlays->SetPadding(FMargin(10.0f, 8.0f));
    }

    if (PNL_MapKey)
    {
        const FLinearColor PopupColor(0.015f, 0.018f, 0.023f, 0.88f);
        PNL_MapKey->SetBrush(MakeRuntimeBoxBrush(PopupColor));
        PNL_MapKey->SetBrushColor(PopupColor);
        PNL_MapKey->SetPadding(FMargin(10.0f, 8.0f));
    }

    if (PNL_BottomBlocker)
    {
        PNL_BottomBlocker->SetVisibility(ESlateVisibility::Collapsed);
    }

    ApplyDetailTabHighlight();
}

// ============================================================
float UWorldForgeFullMapWidget::ResolveBottomBarHeight() const
{
    float Height = FMath::Max(32.0f, BottomBarHeight);

    if (PNL_BottomBar)
    {
        const FVector2D DesiredSize = PNL_BottomBar->GetDesiredSize();
        if (DesiredSize.Y > 1.0f)
        {
            Height = FMath::Max(Height, DesiredSize.Y);
        }
    }

    return FMath::Clamp(Height, 40.0f, 112.0f);
}

// ============================================================
void UWorldForgeFullMapWidget::ApplyDetailTabHighlight() const
{
    const bool bResourceActive = MapSubsystem && MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Resource);
    const bool bCivActive = MapSubsystem &&
        (MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Political) ||
         MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Economy) ||
         MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Trade));
    const bool bEffectsActive = MapSubsystem &&
        (MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Cosmic) ||
         MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Underground) ||
         MapSubsystem->IsOverlayLayerActive(WorldForgeOverlay::Fog));
    const bool bEmbarkActive = !bResourceActive && !bCivActive && !bEffectsActive &&
        bHasValidSelection && !bSelectedTileEmbarkValid;
    const bool bTerrainActive = !bResourceActive && !bCivActive && !bEffectsActive && !bEmbarkActive;

    auto ApplyTab = [](UTextBlock* TextBlock, bool bActive)
    {
        if (!TextBlock)
        {
            return;
        }

        TextBlock->SetColorAndOpacity(FSlateColor(bActive
            ? FLinearColor(1.0f, 0.78f, 0.24f, 1.0f)
            : FLinearColor(0.76f, 0.78f, 0.82f, 0.86f)));
        TextBlock->SetShadowOffset(bActive ? FVector2D(0.0f, 1.0f) : FVector2D::ZeroVector);
        TextBlock->SetShadowColorAndOpacity(bActive
            ? FLinearColor(0.0f, 0.0f, 0.0f, 0.80f)
            : FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
    };

    ApplyTab(TXT_CatTerrain, bTerrainActive);
    ApplyTab(TXT_CatResources, bResourceActive);
    ApplyTab(TXT_CatCiv, bCivActive);
    ApplyTab(TXT_CatEffects, bEffectsActive);
    ApplyTab(TXT_CatEmbark, bEmbarkActive);
}

// ============================================================
// Shared IMG_Map geometry helper.
//
// Every map-space operation uses this same geometry source so clicks,
// hover outlines, grid lines, capital markers, and selected-tile markers
// all agree after the PIE window is resized. Paint geometry is preferred
// because it matches the pixels that were actually drawn this frame; the
// widget cache is only used before NativePaint has populated the cache.
// ============================================================
bool UWorldForgeFullMapWidget::GetCurrentIMGMapGeometry(FGeometry& OutMapGeometry) const
{
    if (!IMG_Map)
    {
        return false;
    }

    const FGeometry WidgetGeometry = IMG_Map->GetCachedGeometry();
    const FVector2D WidgetSize = WidgetGeometry.GetLocalSize();
    if (WidgetSize.X > 0.0f && WidgetSize.Y > 0.0f)
    {
        OutMapGeometry = WidgetGeometry;
        return true;
    }

    const FVector2D PaintSize = CachedIMGMapPaintGeometry.GetLocalSize();
    if (PaintSize.X > 0.0f && PaintSize.Y > 0.0f)
    {
        OutMapGeometry = CachedIMGMapPaintGeometry;
        return true;
    }

    return false;
}

// ============================================================
bool UWorldForgeFullMapWidget::GetCurrentMapDisplayGeometry(FMapDisplayGeometry& OutMapDisplayGeometry) const
{
    OutMapDisplayGeometry = FMapDisplayGeometry();

    FGeometry MapGeometry;
    if (!GetCurrentIMGMapGeometry(MapGeometry))
    {
        return false;
    }

    const FVector2D IMGMapSize = MapGeometry.GetLocalSize();
    if (IMGMapSize.X <= 0.0f || IMGMapSize.Y <= 0.0f)
    {
        return false;
    }

    OutMapDisplayGeometry.IMGMapGeometry = MapGeometry;
    OutMapDisplayGeometry.IMGMapLocalSize = IMGMapSize;

    // For the full-map picker, IMG_Map is stretched to fill its allotted
    // widget area. The displayed map rect is therefore the entire IMG_Map
    // local rectangle; no centered/aspect-corrected sub-rect is used.
    OutMapDisplayGeometry.MapRectMin = FVector2D::ZeroVector;
    OutMapDisplayGeometry.MapRectSize = IMGMapSize;
    OutMapDisplayGeometry.bIsValid = true;
    OutMapDisplayGeometry.bUsedAspectCorrectedRect = false;
    return true;
}

// ============================================================
bool UWorldForgeFullMapWidget::IsScreenPositionInsideWidget(const UWidget* Widget, const FVector2D& ScreenPosition) const
{
    if (!Widget)
    {
        return false;
    }

    const ESlateVisibility WidgetVisibility = Widget->GetVisibility();
    if (WidgetVisibility == ESlateVisibility::Collapsed || WidgetVisibility == ESlateVisibility::Hidden)
    {
        return false;
    }

    const FGeometry WidgetGeometry = Widget->GetCachedGeometry();
    const FVector2D WidgetSize = WidgetGeometry.GetLocalSize();
    if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f)
    {
        return false;
    }

    const FVector2D LocalPosition = WidgetGeometry.AbsoluteToLocal(ScreenPosition);
    return LocalPosition.X >= 0.0f &&
        LocalPosition.X <= WidgetSize.X &&
        LocalPosition.Y >= 0.0f &&
        LocalPosition.Y <= WidgetSize.Y;
}

// ============================================================
bool UWorldForgeFullMapWidget::IsScreenPositionOverBlockingMapPanel(const FVector2D& ScreenPosition) const
{
    // These panels sit above the map in the WBP. If a click reaches the map
    // handler through an unhandled panel/background area, reject it instead of
    // clamping into an edge tile. This keeps bottom bar and popup clicks from
    // changing the selected map tile without changing button bindings.
    return IsScreenPositionInsideWidget(PNL_BottomBar, ScreenPosition) ||
        IsScreenPositionInsideWidget(PNL_Overlays, ScreenPosition) ||
        IsScreenPositionInsideWidget(SBX_TileDetails, ScreenPosition) ||
        IsScreenPositionInsideWidget(PNL_MapKey, ScreenPosition);
}

// ============================================================
FVector2D UWorldForgeFullMapWidget::WidgetUVToWorldUV(const FVector2D& WidgetUV) const
{
    if (bUseStableFullMapCoordinates)
    {
        return FVector2D(
            FMath::Clamp(WidgetUV.X, 0.0f, 1.0f),
            FMath::Clamp(WidgetUV.Y, 0.0f, 1.0f));
    }

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
void UWorldForgeFullMapWidget::ApplyResponsiveTextScaling(const FGeometry& WidgetGeometry)
{
    const FVector2D WidgetSize = WidgetGeometry.GetLocalSize();
    if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f)
    {
        return;
    }

    const float ReferenceW = FMath::Max(1.0f, ResponsiveTextReferenceWidth);
    const float ReferenceH = FMath::Max(1.0f, ResponsiveTextReferenceHeight);
    const float RawScale = FMath::Min(WidgetSize.X / ReferenceW, WidgetSize.Y / ReferenceH);
    const float Scale = FMath::Clamp(
        RawScale,
        FMath::Min(ResponsiveTextMinScale, ResponsiveTextMaxScale),
        FMath::Max(ResponsiveTextMinScale, ResponsiveTextMaxScale));

    if (FMath::Abs(Scale - LastResponsiveTextScale) < 0.01f &&
        FMath::Abs(WidgetSize.X - LastResponsiveTextWidth) < 2.0f &&
        FMath::Abs(WidgetSize.Y - LastResponsiveTextHeight) < 2.0f)
    {
        return;
    }

    LastResponsiveTextScale = Scale;
    LastResponsiveTextWidth = WidgetSize.X;
    LastResponsiveTextHeight = WidgetSize.Y;

    const float DetailsWrapWidth = FMath::Clamp(WidgetSize.X * 0.26f, 220.0f, 430.0f);
    const float ResourceWrapWidth = FMath::Clamp(WidgetSize.X * 0.25f, 220.0f, 420.0f);
    const float MapKeyWrapWidth = FMath::Clamp(WidgetSize.X * 0.22f, 190.0f, 360.0f);
    const float ButtonOuterPad = FMath::Clamp(4.0f * Scale, 2.0f, 6.0f);
    const float ButtonHPad = FMath::Clamp(10.0f * Scale, 5.0f, 12.0f);
    const float ButtonVPad = FMath::Clamp(5.0f * Scale, 3.0f, 6.0f);

    auto ApplyBottomButtonLayout = [&](UButton* Button)
    {
        if (!Button)
        {
            return;
        }

        if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Button->Slot))
        {
            HorizontalSlot->SetPadding(FMargin(ButtonOuterPad, 0.0f));
        }

        if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->GetContentSlot()))
        {
            ButtonSlot->SetPadding(FMargin(ButtonHPad, ButtonVPad));
            ButtonSlot->SetHorizontalAlignment(HAlign_Center);
            ButtonSlot->SetVerticalAlignment(VAlign_Center);
        }
    };

    ApplyBottomButtonLayout(BTN_MapLayers);
    ApplyBottomButtonLayout(BTN_SiteDetails);
    ApplyBottomButtonLayout(BTN_MapKey);
    ApplyBottomButtonLayout(BTN_DebugClaimTile);
    ApplyBottomButtonLayout(BTN_Close);
    ApplyBottomButtonLayout(BTN_Confirm);

    // Runtime text scaling is limited to non-map UI. The map image and overlay
    // coordinate space are not wrapped, scaled, or moved here.
    ApplyResponsiveTextBlock(TXT_SelectedCell, 12, Scale, true, DetailsWrapWidth);
    ApplyResponsiveTextBlock(TXT_ResourceInfo, 12, Scale, true, ResourceWrapWidth);

    ApplyResponsiveTextBlock(TXT_MapLayersLabel, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_SiteDetailsLabel, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_MapKeyLabel, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_DebugClaimTileLabel, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_CloseLabel, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_SelectedCell2, 12, Scale, false, 0.0f);

    ApplyResponsiveTextBlock(Political, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(Economy, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(Cosmic, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(Trade, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(Underground, 12, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(Fog, 12, Scale, false, 0.0f);

    ApplyResponsiveTextBlock(TXT_KeyTitle, 14, Scale, true, MapKeyWrapWidth);
    ApplyResponsiveTextBlock(TXT_KeyWater, 12, Scale, true, MapKeyWrapWidth);
    ApplyResponsiveTextBlock(TXT_KeyLand, 12, Scale, true, MapKeyWrapWidth);
    ApplyResponsiveTextBlock(TXT_KeyMountains, 12, Scale, true, MapKeyWrapWidth);
    ApplyResponsiveTextBlock(TXT_KeyCoast, 12, Scale, true, MapKeyWrapWidth);
    ApplyResponsiveTextBlock(TXT_KeyCivs, 12, Scale, true, MapKeyWrapWidth);
    ApplyResponsiveTextBlock(TXT_KeySelected, 12, Scale, true, MapKeyWrapWidth);

    ApplyResponsiveTextBlock(TXT_CatTerrain, 11, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_CatResources, 11, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_CatCiv, 11, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_CatEffects, 11, Scale, false, 0.0f);
    ApplyResponsiveTextBlock(TXT_CatEmbark, 11, Scale, false, 0.0f);
}


// ============================================================
void UWorldForgeFullMapWidget::ApplyResponsiveTextBlock(UTextBlock* TextBlock, int32 BaseFontSize, float Scale, bool bAutoWrap, float WrapTextAt) const
{
    if (!TextBlock)
    {
        return;
    }

    FSlateFontInfo FontInfo = TextBlock->GetFont();
    FontInfo.Size = FMath::Clamp(FMath::RoundToInt(static_cast<float>(BaseFontSize) * Scale), 8, 18);
    TextBlock->SetFont(FontInfo);
    TextBlock->SetAutoWrapText(bAutoWrap);
    TextBlock->SetWrapTextAt(bAutoWrap ? WrapTextAt : 0.0f);
    TextBlock->SetMinDesiredWidth(0.0f);
}


// ============================================================
bool UWorldForgeFullMapWidget::TileXYToMapDisplayLocal(int32 TileX, int32 TileY, const FMapDisplayGeometry& MapDisplayGeometry, FVector2D& OutIMGMapLocal) const
{
    if (!IMG_Map || !MapSubsystem)
    {
        return false;
    }

    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();
    if (GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    if (!MapDisplayGeometry.bIsValid ||
        MapDisplayGeometry.MapRectSize.X <= 0.0f ||
        MapDisplayGeometry.MapRectSize.Y <= 0.0f)
    {
        return false;
    }

    const float TileU = (static_cast<float>(TileX) + 0.5f) / static_cast<float>(GridWidth);
    const float TileV = (static_cast<float>(TileY) + 0.5f) / static_cast<float>(GridHeight);

    float ScreenU = TileU;
    float ScreenV = TileV;

    if (!bUseStableFullMapCoordinates)
    {
        ScreenU = 0.5f + Zoom * (TileU - ViewCenterX);
        ScreenV = 0.5f + Zoom * (TileV - ViewCenterY);
    }

    if (ScreenU < 0.0f || ScreenU > 1.0f || ScreenV < 0.0f || ScreenV > 1.0f)
    {
        return false;
    }

    // Reverse of mouse conversion: tile -> normalized map UV -> IMG_Map local
    // position inside the same display rect used for click and grid math.
    OutIMGMapLocal = MapDisplayGeometry.MapRectMin + FVector2D(
        ScreenU * MapDisplayGeometry.MapRectSize.X,
        ScreenV * MapDisplayGeometry.MapRectSize.Y);
    return true;
}

// ============================================================
bool UWorldForgeFullMapWidget::TileXYToWidgetLocal(int32 TileX, int32 TileY, const FGeometry& AllottedGeometry, FVector2D& OutWidgetLocal) const
{
    FMapDisplayGeometry MapDisplayGeometry;
    if (!GetCurrentMapDisplayGeometry(MapDisplayGeometry))
    {
        return false;
    }

    FVector2D IMGMapLocal;
    if (!TileXYToMapDisplayLocal(TileX, TileY, MapDisplayGeometry, IMGMapLocal))
    {
        return false;
    }

    const FVector2D Absolute = MapDisplayGeometry.IMGMapGeometry.LocalToAbsolute(IMGMapLocal);
    OutWidgetLocal = AllottedGeometry.AbsoluteToLocal(Absolute);
    return true;
}

// ============================================================
FLinearColor UWorldForgeFullMapWidget::GetMarkerColorForSettlement(int32 SettlementID, int32 OwnerCivID) const
{
    if (UWorld* World = GetWorld())
    {
        if (const USettlementManager* SettlementMgr = World->GetSubsystem<USettlementManager>())
        {
            if (SettlementMgr->HasPlayerSettlement() && SettlementID == SettlementMgr->GetPlayerSettlementID())
            {
                return PlayerCapitalMarkerColor;
            }
        }
    }

    if (MapSubsystem && OwnerCivID >= 0)
    {
        return FLinearColor(MapSubsystem->GetCivColor(OwnerCivID));
    }

    static const FLinearColor FallbackColors[] =
    {
        FLinearColor(0.95f, 0.10f, 0.10f, 1.0f),
        FLinearColor(0.10f, 0.35f, 1.00f, 1.0f),
        FLinearColor(1.00f, 0.80f, 0.05f, 1.0f),
        FLinearColor(0.75f, 0.20f, 1.00f, 1.0f),
        FLinearColor(1.00f, 0.45f, 0.05f, 1.0f),
        FLinearColor(0.05f, 0.85f, 0.95f, 1.0f)
    };

    return FallbackColors[FMath::Abs(SettlementID) % UE_ARRAY_COUNT(FallbackColors)];
}

// ============================================================
void UWorldForgeFullMapWidget::DrawSelectedTileX(
    const FGeometry& AllottedGeometry,
    FSlateWindowElementList& OutDrawElements,
    int32& LayerId) const
{
    if (!bHasValidSelection)
    {
        return;
    }

    FVector2D P;
    if (!TileXYToWidgetLocal(SelectedTileX, SelectedTileY, AllottedGeometry, P))
    {
        return;
    }

    const float S = FMath::Max(4.0f, SelectedTileXSize * FMath::Max(0.1f, SelectedIndicatorScale));
    const FVector2D A(P.X - S * 0.5f, P.Y - S * 0.5f);
    const FVector2D B(P.X + S * 0.5f, P.Y + S * 0.5f);
    const FVector2D C(P.X + S * 0.5f, P.Y - S * 0.5f);
    const FVector2D D(P.X - S * 0.5f, P.Y + S * 0.5f);

    TArray<FVector2D> Line1{ A, B };
    TArray<FVector2D> Line2{ C, D };

    // Shadow/outline first.
    FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Line1, ESlateDrawEffect::None, FLinearColor::Black, true, 5.0f);
    FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Line2, ESlateDrawEffect::None, FLinearColor::Black, true, 5.0f);
    FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Line1, ESlateDrawEffect::None, SelectedTileXColor, true, 2.5f);
    FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Line2, ESlateDrawEffect::None, SelectedTileXColor, true, 2.5f);
}

// ============================================================
void UWorldForgeFullMapWidget::DrawCapitalMarkers(
    const FGeometry& AllottedGeometry,
    FSlateWindowElementList& OutDrawElements,
    int32& LayerId) const
{
    if (!GetWorld() || !MapSubsystem)
    {
        return;
    }

    const USettlementManager* SettlementMgr = GetWorld()->GetSubsystem<USettlementManager>();
    if (!SettlementMgr)
    {
        return;
    }

    const float S = FMath::Max(4.0f, CapitalMarkerSize * FMath::Max(0.1f, SelectedIndicatorScale));

    for (const FSettlementState& Settlement : SettlementMgr->GetAllSettlements())
    {
        const FIntPoint MarkerCell = Settlement.WorldOrigin + FIntPoint(1, 1);

        FVector2D P;
        if (!TileXYToWidgetLocal(MarkerCell.X, MarkerCell.Y, AllottedGeometry, P))
        {
            continue;
        }

        const FLinearColor Color = GetMarkerColorForSettlement(Settlement.SettlementID, Settlement.OwnerCivID);
        const bool bIsPlayer = SettlementMgr->HasPlayerSettlement() && Settlement.SettlementID == SettlementMgr->GetPlayerSettlementID();

        // Flag pole.
        TArray<FVector2D> Pole{ FVector2D(P.X, P.Y + S * 0.65f), FVector2D(P.X, P.Y - S * 0.65f) };
        FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Pole, ESlateDrawEffect::None, FLinearColor::Black, true, 3.0f);
        FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Pole, ESlateDrawEffect::None, Color, true, 1.5f);

        // Flag triangle shape.
        TArray<FVector2D> Flag{
            FVector2D(P.X, P.Y - S * 0.60f),
            FVector2D(P.X + S * 0.70f, P.Y - S * 0.35f),
            FVector2D(P.X, P.Y - S * 0.10f),
            FVector2D(P.X, P.Y - S * 0.60f)
        };
        FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Flag, ESlateDrawEffect::None, FLinearColor::Black, true, 4.0f);
        FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Flag, ESlateDrawEffect::None, Color, true, 2.5f);

        if (bIsPlayer)
        {
            // Small green house outline for player colony.
            TArray<FVector2D> House
            {
                FVector2D(P.X - S * 0.40f, P.Y + S * 0.40f),
                FVector2D(P.X,            P.Y + S * 0.05f),
                FVector2D(P.X + S * 0.40f, P.Y + S * 0.40f),
                FVector2D(P.X + S * 0.35f, P.Y + S * 0.80f),
                FVector2D(P.X - S * 0.35f, P.Y + S * 0.80f),
                FVector2D(P.X - S * 0.40f, P.Y + S * 0.40f)
            };
            FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), House, ESlateDrawEffect::None, FLinearColor::Black, true, 4.0f);
            FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), House, ESlateDrawEffect::None, Color, true, 2.0f);
        }
    }
}


// ============================================================
bool UWorldForgeFullMapWidget::MapLocalToTileXY(const FVector2D& IMGMapLocal, const FMapDisplayGeometry& MapDisplayGeometry, int32& OutTileX, int32& OutTileY, int32& OutTileIndex, FVector2D* OutNormalizedMapUV) const
{
    OutTileX = INDEX_NONE;
    OutTileY = INDEX_NONE;
    OutTileIndex = INDEX_NONE;

    if (!MapSubsystem ||
        !MapDisplayGeometry.bIsValid ||
        MapDisplayGeometry.MapRectSize.X <= 0.0f ||
        MapDisplayGeometry.MapRectSize.Y <= 0.0f)
    {
        return false;
    }

    const FVector2D RectMax = MapDisplayGeometry.MapRectMin + MapDisplayGeometry.MapRectSize;
    if (IMGMapLocal.X < MapDisplayGeometry.MapRectMin.X ||
        IMGMapLocal.X > RectMax.X ||
        IMGMapLocal.Y < MapDisplayGeometry.MapRectMin.Y ||
        IMGMapLocal.Y > RectMax.Y)
    {
        return false;
    }

    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();
    if (GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    // Convert IMG_Map local pixels into a 0..1 UV inside the full IMG_Map
    // local rect. This is the same rect used by grid, marker, hover, and
    // selection drawing.
    const FVector2D RawWidgetUV(
        (IMGMapLocal.X - MapDisplayGeometry.MapRectMin.X) / MapDisplayGeometry.MapRectSize.X,
        (IMGMapLocal.Y - MapDisplayGeometry.MapRectMin.Y) / MapDisplayGeometry.MapRectSize.Y);

    if (OutNormalizedMapUV)
    {
        *OutNormalizedMapUV = RawWidgetUV;
    }

    const FVector2D WidgetUV(
        FMath::Clamp(RawWidgetUV.X, 0.0f, 0.999999f),
        FMath::Clamp(RawWidgetUV.Y, 0.0f, 0.999999f));
    const FVector2D WorldUV = WidgetUVToWorldUV(WidgetUV);

    OutTileX = FMath::Clamp(FMath::FloorToInt(WorldUV.X * static_cast<float>(GridWidth)), 0, GridWidth - 1);
    OutTileY = FMath::Clamp(FMath::FloorToInt(WorldUV.Y * static_cast<float>(GridHeight)), 0, GridHeight - 1);

    return MapSubsystem->TileXYToIndex(OutTileX, OutTileY, OutTileIndex);
}

// ============================================================
// PATCH 3C: Click-to-tile coordinate conversion (resize-safe).
//
// Why a shared display rect is used:
//   Resize drift appears when click conversion, grid painting, and marker
//   drawing each choose their own idea of IMG_Map size. Cached paint geometry
//   can be stale after a PIE/game-window resize, so the live IMG_Map cached
//   geometry is the preferred source and CachedIMGMapPaintGeometry is only a
//   first-frame fallback.
//
// How local position becomes UV:
//   AbsoluteToLocal converts the desktop-space mouse coordinate into IMG_Map
//   local pixels. Subtracting MapRectMin and dividing by MapRectSize gives
//   normalized UV inside the actual displayed map rect.
//
// How UV becomes tile coordinates:
//   UV is multiplied by (GridWidth, GridHeight) and floored to get the
//   integer tile column/row.  This is done inside MapLocalToTileXY.
//
// Why this fixes window-resize offset:
//   Mouse, hover, grid, selected X, locator border, and capital markers all
//   use GetCurrentMapDisplayGeometry(), so they cannot disagree about the
//   visible map rectangle after resize/stretch/shrink.
// ============================================================
bool UWorldForgeFullMapWidget::MouseEventToTileXY(const FPointerEvent& InMouseEvent, int32& OutTileX, int32& OutTileY, int32& OutTileIndex, bool bLogClickConversion) const
{
    OutTileX = INDEX_NONE;
    OutTileY = INDEX_NONE;
    OutTileIndex = INDEX_NONE;

    auto LogClickConversion = [&](const FMapDisplayGeometry* MapDisplayGeometry, const FVector2D& MouseLocal, const FVector2D& UV, bool bRejected, const TCHAR* Reason)
    {
        if (!bLogClickConversion)
        {
            return;
        }

        const FVector2D IMGSize = MapDisplayGeometry ? MapDisplayGeometry->IMGMapLocalSize : FVector2D::ZeroVector;
        const FVector2D RectMin = MapDisplayGeometry ? MapDisplayGeometry->MapRectMin : FVector2D::ZeroVector;
        const FVector2D RectSize = MapDisplayGeometry ? MapDisplayGeometry->MapRectSize : FVector2D::ZeroVector;
        const FVector2D RectMax = RectMin + RectSize;
        const bool bUsedAspectRect = MapDisplayGeometry ? MapDisplayGeometry->bUsedAspectCorrectedRect : false;

        UE_LOG(LogTemp, Log,
            TEXT("[FullMapClick] Rejected=%s Reason=%s IMGSize=(%.1f,%.1f) RectMin=(%.1f,%.1f) RectMax=(%.1f,%.1f) RectSize=(%.1f,%.1f) Mouse=(%.1f,%.1f) UV=(%.4f,%.4f) Tile=(%d,%d) AspectCorrectedRectUsed=%s"),
            bRejected ? TEXT("true") : TEXT("false"),
            Reason,
            IMGSize.X,
            IMGSize.Y,
            RectMin.X,
            RectMin.Y,
            RectMax.X,
            RectMax.Y,
            RectSize.X,
            RectSize.Y,
            MouseLocal.X,
            MouseLocal.Y,
            UV.X,
            UV.Y,
            OutTileX,
            OutTileY,
            bUsedAspectRect ? TEXT("true") : TEXT("false"));
    };

    if (!IMG_Map || !MapSubsystem)
    {
        LogClickConversion(nullptr, FVector2D::ZeroVector, FVector2D::ZeroVector, true, TEXT("MissingMapWidgetOrSubsystem"));
        return false;
    }

    FMapDisplayGeometry MapDisplayGeometry;
    if (!GetCurrentMapDisplayGeometry(MapDisplayGeometry))
    {
        LogClickConversion(nullptr, FVector2D::ZeroVector, FVector2D::ZeroVector, true, TEXT("NoMapDisplayGeometry"));
        return false;
    }

    // Mouse conversion is always:
    // screen-space pointer -> IMG_Map local pixels -> full IMG_Map rect UV
    // -> world/map UV -> tile. Outside the displayed rect returns false
    // instead of clamping, so bottom-bar/popup clicks cannot select edge tiles.
    const FVector2D LocalPos = MapDisplayGeometry.IMGMapGeometry.AbsoluteToLocal(
        InMouseEvent.GetScreenSpacePosition());
    const FVector2D RectMax = MapDisplayGeometry.MapRectMin + MapDisplayGeometry.MapRectSize;
    const FVector2D RawUV(
        (LocalPos.X - MapDisplayGeometry.MapRectMin.X) / MapDisplayGeometry.MapRectSize.X,
        (LocalPos.Y - MapDisplayGeometry.MapRectMin.Y) / MapDisplayGeometry.MapRectSize.Y);

    const bool bOutsideMapRect =
        LocalPos.X < MapDisplayGeometry.MapRectMin.X ||
        LocalPos.X > RectMax.X ||
        LocalPos.Y < MapDisplayGeometry.MapRectMin.Y ||
        LocalPos.Y > RectMax.Y;

    const bool bOverBlockingPanel = IsScreenPositionOverBlockingMapPanel(InMouseEvent.GetScreenSpacePosition());

    if (bOverBlockingPanel)
    {
        LogClickConversion(&MapDisplayGeometry, LocalPos, RawUV, true, TEXT("BlockingPanel"));
        return false;
    }

    if (bOutsideMapRect)
    {
        LogClickConversion(&MapDisplayGeometry, LocalPos, RawUV, true, TEXT("OutsideMapRect"));
        return false;
    }

    FVector2D NormalizedMapUV = RawUV;
    const bool bConverted = MapLocalToTileXY(LocalPos, MapDisplayGeometry, OutTileX, OutTileY, OutTileIndex, &NormalizedMapUV);

    LogClickConversion(
        &MapDisplayGeometry,
        LocalPos,
        NormalizedMapUV,
        !bConverted,
        bConverted ? TEXT("OK") : TEXT("TileConversionFailed"));

    return bConverted;
}

// ============================================================
void UWorldForgeFullMapWidget::SetSelectedTileFromXY(int32 TileX, int32 TileY, int32 TileIndex)
{
    SelectedTileX = TileX;
    SelectedTileY = TileY;
    SelectedTileIndex = TileIndex;
    bHasValidSelection = MapSubsystem && TileIndex != INDEX_NONE;

    SelectedTileBlockReason.Empty();
    bSelectedTileEmbarkValid = bHasValidSelection &&
        MapSubsystem->IsTileValidForEmbark(SelectedTileX, SelectedTileY, SelectedTileBlockReason);

    if (!bSelectedTileEmbarkValid && SelectedTileBlockReason.IsEmpty())
    {
        SelectedTileBlockReason = TEXT("Invalid embark tile.");
    }
}

// ============================================================
void UWorldForgeFullMapWidget::DrawMapInsetShadow(
    const FGeometry& AllottedGeometry,
    FSlateWindowElementList& OutDrawElements,
    int32& LayerId) const
{
    FMapDisplayGeometry MapDisplayGeometry;
    if (!GetCurrentMapDisplayGeometry(MapDisplayGeometry))
    {
        return;
    }

    const float Thickness = FMath::Max(1.0f, MapInsetShadowThickness);
    const FVector2D Inset(Thickness * 0.5f, Thickness * 0.5f);
    const FVector2D MapMin = MapDisplayGeometry.MapRectMin + Inset;
    const FVector2D MapMax = MapDisplayGeometry.MapRectMin + MapDisplayGeometry.MapRectSize - Inset;

    if (MapMax.X <= MapMin.X || MapMax.Y <= MapMin.Y)
    {
        return;
    }

    auto ToWidgetLocal = [&](const FVector2D& MapLocal) -> FVector2D
    {
        const FVector2D Abs = MapDisplayGeometry.IMGMapGeometry.LocalToAbsolute(MapLocal);
        return AllottedGeometry.AbsoluteToLocal(Abs);
    };

    TArray<FVector2D> Box{
        ToWidgetLocal(FVector2D(MapMin.X, MapMin.Y)),
        ToWidgetLocal(FVector2D(MapMax.X, MapMin.Y)),
        ToWidgetLocal(FVector2D(MapMax.X, MapMax.Y)),
        ToWidgetLocal(FVector2D(MapMin.X, MapMax.Y)),
        ToWidgetLocal(FVector2D(MapMin.X, MapMin.Y))
    };

    FSlateDrawElement::MakeLines(
        OutDrawElements,
        ++LayerId,
        AllottedGeometry.ToPaintGeometry(),
        Box,
        ESlateDrawEffect::None,
        FLinearColor(0.0f, 0.0f, 0.0f, 0.72f),
        true,
        Thickness);
}

// ============================================================
void UWorldForgeFullMapWidget::DrawTileDetailsPanelBorder(
    const FGeometry& AllottedGeometry,
    FSlateWindowElementList& OutDrawElements,
    int32& LayerId) const
{
    const UWidget* PanelWidget = PNL_TileDetails ? Cast<UWidget>(PNL_TileDetails) : Cast<UWidget>(SBX_TileDetails);
    if (!PanelWidget || !PanelWidget->IsVisible())
    {
        return;
    }

    const FGeometry PanelGeometry = PanelWidget->GetCachedGeometry();
    const FVector2D PanelSize = PanelGeometry.GetLocalSize();
    if (PanelSize.X <= 1.0f || PanelSize.Y <= 1.0f)
    {
        return;
    }

    auto ToWidgetLocal = [&](const FVector2D& PanelLocal) -> FVector2D
    {
        const FVector2D Abs = PanelGeometry.LocalToAbsolute(PanelLocal);
        return AllottedGeometry.AbsoluteToLocal(Abs);
    };

    TArray<FVector2D> Box{
        ToWidgetLocal(FVector2D(1.0f, 1.0f)),
        ToWidgetLocal(FVector2D(PanelSize.X - 1.0f, 1.0f)),
        ToWidgetLocal(FVector2D(PanelSize.X - 1.0f, PanelSize.Y - 1.0f)),
        ToWidgetLocal(FVector2D(1.0f, PanelSize.Y - 1.0f)),
        ToWidgetLocal(FVector2D(1.0f, 1.0f))
    };

    FSlateDrawElement::MakeLines(
        OutDrawElements,
        ++LayerId,
        AllottedGeometry.ToPaintGeometry(),
        Box,
        ESlateDrawEffect::None,
        FLinearColor(1.0f, 0.84f, 0.42f, 0.30f),
        true,
        1.0f);
}

// ============================================================
void UWorldForgeFullMapWidget::DrawTileGridOverlay(
    const FGeometry& AllottedGeometry,
    FSlateWindowElementList& OutDrawElements,
    int32& LayerId) const
{
    if (!IMG_Map || !MapSubsystem)
    {
        return;
    }

    FMapDisplayGeometry MapDisplayGeometry;
    if (!GetCurrentMapDisplayGeometry(MapDisplayGeometry))
    {
        return;
    }

    const FVector2D MapRectMin = MapDisplayGeometry.MapRectMin;
    const FVector2D MapRectSize = MapDisplayGeometry.MapRectSize;
    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();

    if (MapRectSize.X <= 0.0f || MapRectSize.Y <= 0.0f || GridWidth <= 0 || GridHeight <= 0)
    {
        return;
    }

    const float TileW = MapRectSize.X / static_cast<float>(GridWidth);
    const float TileH = MapRectSize.Y / static_cast<float>(GridHeight);

    // Avoid drawing hundreds of tiny unreadable lines at low zoom. Always draw
    // major grid lines; minor lines appear only when individual tiles are large enough.
    const bool bDrawMinor = FMath::Min(TileW, TileH) >= 3.0f;
    const int32 MajorEvery = FMath::Max(1, MajorGridLineInterval);

    auto ToWidgetLocal = [&](const FVector2D& MapLocal) -> FVector2D
    {
        const FVector2D Abs = MapDisplayGeometry.IMGMapGeometry.LocalToAbsolute(MapLocal);
        return AllottedGeometry.AbsoluteToLocal(Abs);
    };

    for (int32 X = 0; X <= GridWidth; ++X)
    {
        const bool bMajor = (X % MajorEvery) == 0 || X == GridWidth;
        if (!bMajor && !bDrawMinor)
        {
            continue;
        }

        const float MX = MapRectMin.X + static_cast<float>(X) * TileW;
        TArray<FVector2D> Points{
            ToWidgetLocal(FVector2D(MX, MapRectMin.Y)),
            ToWidgetLocal(FVector2D(MX, MapRectMin.Y + MapRectSize.Y))
        };

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            ++LayerId,
            AllottedGeometry.ToPaintGeometry(),
            Points,
            ESlateDrawEffect::None,
            bMajor ? MajorGridLineColor : MinorGridLineColor,
            true,
            bMajor ? 1.25f : 0.5f);
    }

    for (int32 Y = 0; Y <= GridHeight; ++Y)
    {
        const bool bMajor = (Y % MajorEvery) == 0 || Y == GridHeight;
        if (!bMajor && !bDrawMinor)
        {
            continue;
        }

        const float MY = MapRectMin.Y + static_cast<float>(Y) * TileH;
        TArray<FVector2D> Points{
            ToWidgetLocal(FVector2D(MapRectMin.X, MY)),
            ToWidgetLocal(FVector2D(MapRectMin.X + MapRectSize.X, MY))
        };

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            ++LayerId,
            AllottedGeometry.ToPaintGeometry(),
            Points,
            ESlateDrawEffect::None,
            bMajor ? MajorGridLineColor : MinorGridLineColor,
            true,
            bMajor ? 1.25f : 0.5f);
    }
}

// ============================================================
void UWorldForgeFullMapWidget::DrawTileOutline(
    const FGeometry& AllottedGeometry,
    FSlateWindowElementList& OutDrawElements,
    int32& LayerId,
    int32 TileX,
    int32 TileY,
    FLinearColor Color,
    float Thickness) const
{
    if (!IMG_Map || !MapSubsystem || TileX < 0 || TileY < 0)
    {
        return;
    }

    FMapDisplayGeometry MapDisplayGeometry;
    if (!GetCurrentMapDisplayGeometry(MapDisplayGeometry))
    {
        return;
    }

    const FVector2D MapRectMin = MapDisplayGeometry.MapRectMin;
    const FVector2D MapRectSize = MapDisplayGeometry.MapRectSize;
    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();
    if (MapRectSize.X <= 0.0f || MapRectSize.Y <= 0.0f || GridWidth <= 0 || GridHeight <= 0)
    {
        return;
    }

    const float TileW = MapRectSize.X / static_cast<float>(GridWidth);
    const float TileH = MapRectSize.Y / static_cast<float>(GridHeight);
    const FVector2D Min(
        MapRectMin.X + static_cast<float>(TileX) * TileW,
        MapRectMin.Y + static_cast<float>(TileY) * TileH);
    const FVector2D Max(Min.X + TileW, Min.Y + TileH);

    auto ToWidgetLocal = [&](const FVector2D& MapLocal) -> FVector2D
    {
        const FVector2D Abs = MapDisplayGeometry.IMGMapGeometry.LocalToAbsolute(MapLocal);
        return AllottedGeometry.AbsoluteToLocal(Abs);
    };

    TArray<FVector2D> Box{
        ToWidgetLocal(FVector2D(Min.X, Min.Y)),
        ToWidgetLocal(FVector2D(Max.X, Min.Y)),
        ToWidgetLocal(FVector2D(Max.X, Max.Y)),
        ToWidgetLocal(FVector2D(Min.X, Max.Y)),
        ToWidgetLocal(FVector2D(Min.X, Min.Y))
    };

    FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Box, ESlateDrawEffect::None, FLinearColor::Black, true, Thickness + 2.0f);
    FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), Box, ESlateDrawEffect::None, Color, true, Thickness);
}

// ============================================================
void UWorldForgeFullMapWidget::EnterMapSelectionInputMode()
{
    SetIsFocusable(true);

    if (!CachedPlayerController)
    {
        SetKeyboardFocus();
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

    SetKeyboardFocus();

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
        SetSelectedTileFromXY(ExistingX, ExistingY, ExistingIndex);
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

    int32 InitialTileIndex = INDEX_NONE;
    if (MapSubsystem->TileXYToIndex(FinalCell.X, FinalCell.Y, InitialTileIndex))
    {
        SetSelectedTileFromXY(FinalCell.X, FinalCell.Y, InitialTileIndex);
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
