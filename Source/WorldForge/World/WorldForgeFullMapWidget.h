#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Reply.h"
#include "WorldForgeFullMapWidget.generated.h"


class UImage;
class UBorder;
class UTextBlock;
class UButton;
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

    virtual FReply NativeOnKeyDown(
        const FGeometry& InGeometry,
        const FKeyEvent& InKeyEvent) override;

    virtual FReply NativeOnKeyUp(
        const FGeometry& InGeometry,
        const FKeyEvent& InKeyEvent) override;

    virtual FReply NativeOnMouseWheel(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;

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

    // Step sizes for arrow-key tile selection (unchanged from original)
    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    int32 KeyboardMoveStep = 4;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    int32 KeyboardMoveStepFast = 12;

    // ------------------------------------------------------------------
    // Pan / Zoom state
    //
    // ViewCenterX/Y: the point on the map texture (0..1 UV) that is
    //   currently centred on screen.  Default 0.5,0.5 = world centre.
    //
    // Zoom: 1 = whole map visible.  2 = 2× magnification, etc.
    //
    // WASD holds pan the view.
    // Shift+WASD pans faster.
    // Mouse wheel zooms.
    // Arrow keys still move the tile selection cursor.
    // ------------------------------------------------------------------
    float ViewCenterX = 0.5f;
    float ViewCenterY = 0.5f;
    float Zoom = 1.0f;

    bool bPanUp = false;
    bool bPanDown = false;
    bool bPanLeft = false;
    bool bPanRight = false;
    bool bPanFast = false;

    // Units per second at Zoom=1 (fraction of map width/height)
    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float PanSpeed = 0.35f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float FastPanMultiplier = 2.5f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float MinZoom = 1.0f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|Map")
    float MaxZoom = 8.0f;

    // How much Zoom changes per mouse-wheel click
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

    void UpdateSelectionVisual();
    void UpdateSelectionText();

    void EnterMapSelectionInputMode();
    void RestoreGameInputMode();

    // Arrow-key selection movement (unchanged from original)
    void MoveSelectionBy(int32 DeltaX, int32 DeltaY, bool bFastMove);
    void ClampSelectionToMap();
    void EnsureValidInitialSelection();

    // Apply the current Zoom + ViewCenter as a render transform on IMG_Map
    void ApplyMapTransform();

    // Convert a raw widget-local UV (0..1 in widget space) to a
    // world-map UV (0..1 in texture space) using the current pan/zoom.
    FVector2D WidgetUVToWorldUV(const FVector2D& WidgetUV) const;

    // Clamp ViewCenter so the view never shows outside the map edges
    void ClampViewCenter();
};