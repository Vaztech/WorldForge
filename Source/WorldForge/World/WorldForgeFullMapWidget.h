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

    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

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

    void EnterMapSelectionInputMode();
    void RestoreGameInputMode();

    void MoveSelectionBy(int32 DeltaX, int32 DeltaY, bool bFastMove);
    void ClampSelectionToMap();
    void EnsureValidInitialSelection();

    void ApplyMapTransform();

    FVector2D WidgetUVToWorldUV(const FVector2D& WidgetUV) const;

    void ClampViewCenter();
};
