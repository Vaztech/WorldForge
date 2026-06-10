#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeMiniMapHUD.generated.h"

class UProcessorManager;
class USettlementManager;
class UVoxelWorldManager;
class UWorldMapSubsystem;

UCLASS(Blueprintable)
class WORLDFORGE_API AWorldForgeMiniMapHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiniMap")
    void SetMiniMapVisible(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|HUD")
    void SetLegacyHudSuppressedForFullMap(bool bSuppress);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|MiniMap")
    void ToggleMiniMapVisible();

    UFUNCTION(BlueprintPure, Category = "WorldForge|MiniMap")
    bool IsMiniMapVisible() const { return bShowMiniMap; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|HUD")
    bool IsLegacyHudSuppressedForFullMap() const { return bSuppressLegacyHudForFullMap; }

protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bDrawLegacyMiniMap = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowMiniMap = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowPawnDots = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowCameraMarker = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowDirectionIndicator = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowCivTerritory = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowPlayerColonyMarker = true;

    // Show visible/offscreen markers for active NPC civilization settlements.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowNPCSettlementMarkers = true;

    // If true, pawns outside the current minimap zoom are clamped to the edge
    // using their settlement color instead of disappearing/turning gray.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowOffscreenPawnIndicators = true;

    // When true, forward on the camera appears as up on the minimap.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap|Orientation")
    bool bOrientMiniMapToCamera = false;

    // Draw terrain as rotated cells instead of using the north-up map texture.
    // This keeps terrain, pawns, colony markers, and civ territories aligned.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap|Orientation")
    bool bUseCellDrawnMiniMap = false;

    // Testing mode: show every generated civilization territory on the minimap.
    // Turn this off for final fog-of-war/discovery behavior.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap|Debug")
    bool bDebugRevealAllCivTerritory = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowTitle = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FVector2D MiniMapPosition = FVector2D(30.0f, 30.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FVector2D MiniMapSize = FVector2D(280.0f, 280.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FIntPoint WorldCellMin = FIntPoint(0, 0);

    // DEPRECATED (MiniMapFix): Hard-coded 256-grid assumption — broke on 512/1024 worlds.
    // Grid dimensions are now read from UWorldMapSubsystem::GridWidth/GridHeight, which are
    // always set authoritatively by the pipeline.  This property is kept for Blueprint
    // compatibility only and is NOT used by any C++ minimap or HUD logic.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1", UIMin = "1"))
    FIntPoint WorldCellDimensions = FIntPoint(256, 256);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float MiniMapZoom = 32.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor BackgroundColor = FLinearColor(0.02f, 0.02f, 0.02f, 0.95f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor BorderColor = FLinearColor(0.85f, 0.85f, 0.85f, 1.0f);

    // Player colony / player colonists are always green.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor PlayerDotColor = FLinearColor(0.0f, 1.0f, 0.15f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor AIDotColor = FLinearColor(1.0f, 0.15f, 0.15f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor CameraColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor OutOfBoundsColor = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor DirectionIndicatorColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor DirectionIndicatorShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.9f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "1.0", UIMax = "1.0"))
    float CivTerritoryAlpha = 0.70f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "0.25", UIMin = "0.25"))
    float CivTerritoryMinPixelSize = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "0.25", UIMin = "0.25"))
    float TerrainCellMinPixelSize = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float PawnDotSize = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "2.0", UIMin = "2.0"))
    float EdgeIndicatorSize = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    float EdgePadding = 6.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float CameraMarkerSize = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "6.0", UIMin = "6.0"))
    float PlayerColonyMarkerSize = 14.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "6.0", UIMin = "6.0"))
    float NPCSettlementMarkerSize = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "8.0", UIMin = "8.0"))
    float DirectionIndicatorSize = 18.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap", meta = (ClampMin = "0.1", UIMin = "0.1"))
    float TitleScale = 0.9f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FString TitleText = TEXT("WorldForge MiniMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap|Debug")
    bool bShowDebugStats = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowZLevel = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    bool bShowTerrainInfo = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor ZLevelColor = FLinearColor(0.9f, 0.9f, 0.2f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|MiniMap")
    FLinearColor TerrainInfoColor = FLinearColor(0.7f, 0.8f, 1.0f, 1.0f);

private:
    bool bSuppressLegacyHudForFullMap = false;

    FVector2D WorldCellToMiniMap(const FIntPoint& WorldCell) const;
    bool IsWorldCellInBounds(const FIntPoint& WorldCell) const;
    FVector2D ClampToMiniMapBounds(const FVector2D& Point) const;

    bool GetCameraCellAndYaw(FIntPoint& OutCameraCell, float& OutYawDegrees) const;
    FLinearColor GetTerrainColorForTile(const FWorldTileData& Tile) const;

    void DrawMiniMapBackground();
    void DrawMiniMapTextureBackground();
    void DrawMiniMapCellBackground(const UWorldMapSubsystem* MapSubsystem, const UVoxelWorldManager* VoxelWorldManager);
    void DrawMiniMapBorder();
    void DrawMiniMapTitle();
    void DrawCivTerritories(const UWorldMapSubsystem* MapSubsystem, const UVoxelWorldManager* VoxelWorldManager);
    bool ShouldDrawCivTile(const FWorldTileData& Tile) const;
    void DrawPawnDots(const USettlementManager* SettlementManager, const UProcessorManager* ProcessorManager);
    void DrawPlayerColonyMarker(const USettlementManager* SettlementManager);
    void DrawNPCSettlementMarkers(const USettlementManager* SettlementManager);
    void DrawCameraLocation(const UVoxelWorldManager* VoxelWorldManager);
    void DrawDirectionIndicator();
    void DrawDebugStats(const UProcessorManager* ProcessorManager, const UVoxelWorldManager* VoxelWorldManager);
    void DrawZLevelIndicator(const UVoxelWorldManager* VoxelWorldManager);
    void DrawTerrainInfo(const UVoxelWorldManager* VoxelWorldManager);

    FLinearColor GetDotColorForSettlement(const USettlementManager* SettlementManager, int32 SettlementID) const;
};
