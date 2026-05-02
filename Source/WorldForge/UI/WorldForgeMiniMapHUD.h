#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "WorldForgeMiniMapHUD.generated.h"

class UProcessorManager;
class USettlementManager;
class UVoxelWorldManager;

UCLASS(Blueprintable)
class WORLDFORGE_API AWorldForgeMiniMapHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowMiniMap = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowPawnDots = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowCameraMarker = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowTitle = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FVector2D MiniMapPosition = FVector2D(30.0f, 30.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FVector2D MiniMapSize = FVector2D(280.0f, 280.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FIntPoint WorldCellMin = FIntPoint(0, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1", UIMin = "1"))
    FIntPoint WorldCellDimensions = FIntPoint(256, 256);

    // 🔥 NEW: Zoom (controls how much world is visible)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float MiniMapZoom = 32.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor BackgroundColor = FLinearColor(0.02f, 0.02f, 0.02f, 0.95f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor BorderColor = FLinearColor(0.85f, 0.85f, 0.85f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor PlayerDotColor = FLinearColor(0.10f, 0.45f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor AIDotColor = FLinearColor(1.0f, 0.15f, 0.15f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor CameraColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor OutOfBoundsColor = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float PawnDotSize = 8.0f;

    // 🔥 NEW: Off-screen indicator size
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "2.0", UIMin = "2.0"))
    float EdgeIndicatorSize = 10.0f;

    // 🔥 NEW: Padding from edge
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    float EdgePadding = 6.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float CameraMarkerSize = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "0.1", UIMin = "0.1"))
    float TitleScale = 0.9f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FString TitleText = TEXT("WorldForge MiniMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap|Debug")
    bool bShowDebugStats = true;

    // v4.0: Show camera Z altitude and terrain info
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowZLevel = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowTerrainInfo = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor ZLevelColor = FLinearColor(0.9f, 0.9f, 0.2f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor TerrainInfoColor = FLinearColor(0.7f, 0.8f, 1.0f, 1.0f);

private:
    FVector2D WorldCellToMiniMap(const FIntPoint& WorldCell) const;
    bool IsWorldCellInBounds(const FIntPoint& WorldCell) const;

    // Clamp to minimap bounds (for edge indicators)
    FVector2D ClampToMiniMapBounds(const FVector2D& Point) const;

    void DrawMiniMapBackground();
    void DrawMiniMapBorder();
    void DrawMiniMapTitle();
    void DrawPawnDots(const USettlementManager* SettlementManager, const UProcessorManager* ProcessorManager);
    void DrawCameraLocation(const UVoxelWorldManager* VoxelWorldManager);
    void DrawDebugStats(const UProcessorManager* ProcessorManager, const UVoxelWorldManager* VoxelWorldManager);
    void DrawZLevelIndicator(const UVoxelWorldManager* VoxelWorldManager);
    void DrawTerrainInfo(const UVoxelWorldManager* VoxelWorldManager);

    FLinearColor GetDotColorForSettlement(int32 SettlementID) const;
};