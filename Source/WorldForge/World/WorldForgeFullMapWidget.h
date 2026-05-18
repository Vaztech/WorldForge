// WorldForgeFullMapWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Reply.h"
#include "WorldMapSubsystem.h"
#include "WorldForgeFullMapWidget.generated.h"

class UImage;
class UBorder;
class UTextBlock;
class UButton;
class UCheckBox;
class USizeBox;
class UWidget;
class UWorldMapSubsystem;
class APlayerController;
struct FGeometry;
struct FPointerEvent;
struct FKeyEvent;

UCLASS()
class WORLDFORGE_API UWorldForgeFullMapWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual int32 NativePaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;

    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

protected:
    UPROPERTY(meta = (BindWidget), BlueprintReadOnly)
    UImage* IMG_Map = nullptr;

    UPROPERTY(meta = (BindWidget), BlueprintReadOnly)
    UBorder* BRD_SelectedCell = nullptr;

    UPROPERTY(meta = (BindWidget), BlueprintReadOnly)
    UTextBlock* TXT_SelectedCell = nullptr;

    UPROPERTY(meta = (BindWidget), BlueprintReadOnly)
    UButton* BTN_Confirm = nullptr;

    UPROPERTY(meta = (BindWidget), BlueprintReadOnly)
    UButton* BTN_Close = nullptr;

    // ------------------------------------------------------------------
    // Overlay toggle checkboxes (DF-style multi-layer system).
    //
    // Add matching UCheckBox names in the WBP:
    //
    //   CHK_Political
    //   CHK_Economy
    //   CHK_Cosmic
    //   CHK_Trade
    //   CHK_Underground
    //   CHK_Fog
    //
    // All are BindWidgetOptional so the C++ compiles/runs even if the
    // WBP hasn't been updated yet.  Terrain is always on (no toggle).
    // ------------------------------------------------------------------
    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UCheckBox* CHK_Political = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UCheckBox* CHK_Economy = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UCheckBox* CHK_Cosmic = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UCheckBox* CHK_Trade = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UCheckBox* CHK_Underground = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UCheckBox* CHK_Fog = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UCheckBox* CHK_Resource = nullptr;

    // Resource tooltip text block — shows per-tile category breakdown
    // when the player selects a tile while the Resource overlay is active.
    // Add a UTextBlock named TXT_ResourceInfo in the WBP (optional).
    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_ResourceInfo = nullptr;

    // ------------------------------------------------------------------
    // Bottom command-bar toggle buttons.
    //
    // These three buttons live in the bottom bar (HBox_BottomBar) and
    // toggle popup/detail panels. All are BindWidgetOptional so the
    // widget compiles even if the WBP hasn't been updated yet.
    // ------------------------------------------------------------------
    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_MapLayers = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_SiteDetails = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_MapKey = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_DebugClaimTile = nullptr;

    // ------------------------------------------------------------------
    // Panels toggled by the bottom-bar buttons above.
    //
    //   PNL_Overlays      – Map Layers dropdown (holds overlay checkboxes)
    //   SBX_TileDetails   – Site Details card (selected tile info, top-right)
    //   PNL_MapKey         – Map Key legend (color key, bottom-left popup)
    //   PNL_BottomBar      – The bottom command bar itself (styled opaque)
    // ------------------------------------------------------------------
    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UBorder* PNL_Overlays = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    USizeBox* SBX_TileDetails = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UBorder* PNL_TileDetails = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UBorder* PNL_MapKey = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UBorder* PNL_BottomBar = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UBorder* PNL_BottomBlocker = nullptr;

    // ------------------------------------------------------------------
    // Existing non-map UI text widgets.
    //
    // These are adjusted at runtime with bounded font sizes/wrap widths so
    // resizing the PIE window does not make the full-map panels overflow or
    // balloon. IMG_Map and map overlay drawing are deliberately excluded.
    // ------------------------------------------------------------------
    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_CloseLabel = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_SelectedCell2 = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_MapLayersLabel = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_SiteDetailsLabel = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_MapKeyLabel = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_DebugClaimTileLabel = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* Political = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* Economy = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* Cosmic = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* Trade = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* Underground = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* Fog = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_KeyTitle = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_KeyWater = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_KeyLand = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_KeyMountains = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_KeyCoast = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_KeyCivs = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_KeySelected = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_CatTerrain = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_CatResources = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_CatCiv = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_CatEffects = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* TXT_CatEmbark = nullptr;

    // ------------------------------------------------------------------
    // Legacy overlay buttons (backwards compatibility).
    //
    // If the WBP still has BTN_Terrain / BTN_Political / etc., clicking
    // them will act as "solo this overlay" — clearing all other layers
    // and enabling just the clicked one (same as the old single-mode
    // behavior).
    //
    // Once you switch the WBP to checkboxes you can remove these.
    // ------------------------------------------------------------------
    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_Terrain = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_Political = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_Economy = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_Cosmic = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_Trade = nullptr;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
    UButton* BTN_Underground = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UWorldMapSubsystem> MapSubsystem = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<APlayerController> CachedPlayerController = nullptr;

    // ------------------------------------------------------------------
    // Selection state
    // ------------------------------------------------------------------
    int32 SelectedTileX = INDEX_NONE;
    int32 SelectedTileY = INDEX_NONE;
    int32 SelectedTileIndex = INDEX_NONE;
    bool  bHasValidSelection = false;
    bool  bInputLockedToUI = false;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    int32 KeyboardMoveStep = 4;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    int32 KeyboardMoveStepFast = 12;

    // ------------------------------------------------------------------
    // Pan / Zoom state
    // ------------------------------------------------------------------
    float ViewCenterX = 0.5f;
    float ViewCenterY = 0.5f;
    float Zoom = 1.0f;

    bool bPanUp = false;
    bool bPanDown = false;
    bool bPanLeft = false;
    bool bPanRight = false;
    bool bPanFast = false;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float PanSpeed = 0.35f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float FastPanMultiplier = 2.5f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float MinZoom = 1.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float MaxZoom = 8.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float ZoomStep = 0.5f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|FullMapUI", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float ResponsiveTextReferenceWidth = 1280.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|FullMapUI", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float ResponsiveTextReferenceHeight = 720.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|FullMapUI", meta = (ClampMin = "0.5", ClampMax = "1.0", UIMin = "0.5", UIMax = "1.0"))
    float ResponsiveTextMinScale = 0.74f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|FullMapUI", meta = (ClampMin = "1.0", ClampMax = "1.5", UIMin = "1.0", UIMax = "1.5"))
    float ResponsiveTextMaxScale = 1.10f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|FullMapUI", meta = (ClampMin = "32.0", UIMin = "32.0"))
    float BottomBarHeight = 64.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|FullMapUI", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float MapInsetShadowThickness = 2.0f;

    float LastResponsiveTextScale = -1.0f;
    float LastResponsiveTextWidth = -1.0f;
    float LastResponsiveTextHeight = -1.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Accuracy")
    bool bUseStableFullMapCoordinates = true;

    // Tile-grid based map selection. This avoids texture/UV drift by making
    // the full map a true GridWidth x GridHeight tile picker.
    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid")
    bool bUseTileGridSelection = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid")
    bool bShowTileGridOverlay = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid", meta = (ClampMin = "4", UIMin = "4"))
    int32 MajorGridLineInterval = 16;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid")
    FLinearColor MinorGridLineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.20f);

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid")
    FLinearColor MajorGridLineColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.28f);

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid")
    FLinearColor HoveredTileColor = FLinearColor(1.0f, 1.0f, 0.15f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid")
    FLinearColor InvalidTileColor = FLinearColor(1.0f, 0.1f, 0.1f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|TileGrid")
    bool bDebugRevealTileSurveyInfo = true;

    int32 HoveredTileX = INDEX_NONE;
    int32 HoveredTileY = INDEX_NONE;
    int32 HoveredTileIndex = INDEX_NONE;
    bool bHasHoveredTile = false;

    bool bSelectedTileEmbarkValid = false;
    FString SelectedTileBlockReason;
    FString LastDebugClaimMessage;

    // ------------------------------------------------------------------
    // Full-map markers
    // ------------------------------------------------------------------
    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Markers")
    bool bShowCapitalMarkers = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Markers")
    bool bShowSelectedTileRedX = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Markers", meta = (ClampMin = "4.0", UIMin = "4.0"))
    float CapitalMarkerSize = 18.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Markers", meta = (ClampMin = "4.0", UIMin = "4.0"))
    float SelectedTileXSize = 22.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Markers", meta = (ClampMin = "0.1", UIMin = "0.1"))
    float SelectedIndicatorScale = 1.20f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Markers")
    FLinearColor PlayerCapitalMarkerColor = FLinearColor(0.0f, 1.0f, 0.15f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map|Markers")
    FLinearColor SelectedTileXColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

    // ------------------------------------------------------------------
    virtual FReply NativeOnMouseButtonDown(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;

    UFUNCTION()
    void HandleConfirmClicked();

    UFUNCTION()
    void HandleCloseClicked();

    // ── Checkbox toggle handlers ─────────────────────────────────
    UFUNCTION()
    void HandlePoliticalToggled(bool bIsChecked);

    UFUNCTION()
    void HandleEconomyToggled(bool bIsChecked);

    UFUNCTION()
    void HandleCosmicToggled(bool bIsChecked);

    UFUNCTION()
    void HandleTradeToggled(bool bIsChecked);

    UFUNCTION()
    void HandleUndergroundToggled(bool bIsChecked);

    UFUNCTION()
    void HandleFogToggled(bool bIsChecked);

    UFUNCTION()
    void HandleResourceToggled(bool bIsChecked);

    // ── Bottom-bar toggle handlers ─────────────────────────────
    // Each handler toggles visibility of its associated popup panel.
    // Map Layers and Map Key are mutually exclusive popups.
    UFUNCTION()
    void HandleMapLayersClicked();

    UFUNCTION()
    void HandleSiteDetailsClicked();

    UFUNCTION()
    void HandleMapKeyClicked();

    UFUNCTION()
    void HandleDebugClaimTileClicked();

    // ── Legacy button handlers (solo-mode backwards compat) ──────
    UFUNCTION()
    void HandleTerrainOverlayClicked();

    UFUNCTION()
    void HandlePoliticalOverlayClicked();

    UFUNCTION()
    void HandleEconomyOverlayClicked();

    UFUNCTION()
    void HandleCosmicOverlayClicked();

    UFUNCTION()
    void HandleTradeOverlayClicked();

    UFUNCTION()
    void HandleUndergroundOverlayClicked();

    // Sets a single overlay layer and refreshes the map.
    void SetOverlayLayerAndRefresh(int32 LayerBit, bool bActive);

    // Legacy: clears all non-terrain overlays and enables one mode.
    void SetMapOverlayMode(EWorldMapOverlayMode NewMode);

    void RefreshMapTexture();

    // Sync checkbox checked states to match subsystem's ActiveOverlayLayers.
    void SyncCheckboxesToOverlayState();

    void UpdateSelectionVisual();
    void UpdateSelectionText();
    void UpdateResourceTooltip();

    void ApplyRuntimeLayout(const FGeometry& WidgetGeometry);
    void ApplyRuntimeStyles();
    float ResolveBottomBarHeight() const;
    void ApplyDetailTabHighlight() const;

    void EnterMapSelectionInputMode();
    void RestoreGameInputMode();

    void MoveSelectionBy(int32 DeltaX, int32 DeltaY, bool bFastMove);
    void ClampSelectionToMap();
    void EnsureValidInitialSelection();

    void ApplyMapTransform();

    struct FMapDisplayGeometry
    {
        FGeometry IMGMapGeometry;
        FVector2D IMGMapLocalSize = FVector2D::ZeroVector;
        FVector2D MapRectMin = FVector2D::ZeroVector;
        FVector2D MapRectSize = FVector2D::ZeroVector;
        bool bIsValid = false;
        bool bUsedAspectCorrectedRect = false;
    };

    // Returns the current IMG_Map geometry. After a resize, the live widget
    // geometry is preferred; the paint-pass cache is only a fallback before
    // IMG_Map has a valid non-zero cached size.
    bool GetCurrentIMGMapGeometry(FGeometry& OutMapGeometry) const;

    // Single source of truth for the visible map rectangle used by mouse,
    // hover, selection, grid, marker, and transform paths.
    bool GetCurrentMapDisplayGeometry(FMapDisplayGeometry& OutMapDisplayGeometry) const;

    FVector2D WidgetUVToWorldUV(const FVector2D& WidgetUV) const;
    bool TileXYToMapDisplayLocal(int32 TileX, int32 TileY, const FMapDisplayGeometry& MapDisplayGeometry, FVector2D& OutIMGMapLocal) const;
    bool TileXYToWidgetLocal(int32 TileX, int32 TileY, const FGeometry& AllottedGeometry, FVector2D& OutWidgetLocal) const;
    bool MapLocalToTileXY(const FVector2D& IMGMapLocal, const FMapDisplayGeometry& MapDisplayGeometry, int32& OutTileX, int32& OutTileY, int32& OutTileIndex, FVector2D* OutNormalizedMapUV = nullptr) const;
    bool MouseEventToTileXY(const FPointerEvent& InMouseEvent, int32& OutTileX, int32& OutTileY, int32& OutTileIndex, bool bLogClickConversion = false) const;
    bool IsScreenPositionInsideWidget(const UWidget* Widget, const FVector2D& ScreenPosition) const;
    bool IsScreenPositionOverBlockingMapPanel(const FVector2D& ScreenPosition) const;
    void SetSelectedTileFromXY(int32 TileX, int32 TileY, int32 TileIndex);
    bool TryDebugClaimHoveredOrSelectedTile();
    FLinearColor GetMarkerColorForSettlement(int32 SettlementID, int32 OwnerCivID) const;
    void DrawSelectedTileX(
        const FGeometry& AllottedGeometry,
        FSlateWindowElementList& OutDrawElements,
        int32& LayerId) const;
    void DrawCapitalMarkers(
        const FGeometry& AllottedGeometry,
        FSlateWindowElementList& OutDrawElements,
        int32& LayerId) const;
    void DrawMapInsetShadow(
        const FGeometry& AllottedGeometry,
        FSlateWindowElementList& OutDrawElements,
        int32& LayerId) const;
    void DrawTileDetailsPanelBorder(
        const FGeometry& AllottedGeometry,
        FSlateWindowElementList& OutDrawElements,
        int32& LayerId) const;
    void DrawTileGridOverlay(
        const FGeometry& AllottedGeometry,
        FSlateWindowElementList& OutDrawElements,
        int32& LayerId) const;
    void DrawTileOutline(
        const FGeometry& AllottedGeometry,
        FSlateWindowElementList& OutDrawElements,
        int32& LayerId,
        int32 TileX,
        int32 TileY,
        FLinearColor Color,
        float Thickness) const;

    void ClampViewCenter();
    void ApplyResponsiveTextScaling(const FGeometry& WidgetGeometry);
    void ApplyResponsiveTextBlock(UTextBlock* TextBlock, int32 BaseFontSize, float Scale, bool bAutoWrap, float WrapTextAt) const;

    // ------------------------------------------------------------------
    // Paint-pass cached geometry for IMG_Map.
    //
    // This cache can become stale after a resize. It remains only as an
    // early-frame fallback when IMG_Map->GetCachedGeometry() is unavailable
    // or zero-sized; it is no longer the preferred source for map math.
    // ------------------------------------------------------------------
    mutable FGeometry CachedIMGMapPaintGeometry;
};
