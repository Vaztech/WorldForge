#include "UI/WorldForgeMiniMapHUD.h"

#include "ProcessorManager.h"
#include "SettlementManager.h"
#include "VoxelWorldManager.h"
#include "WorldForgeDataTypes.h"
#include "WorldMapSubsystem.h"

#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
    FLinearColor GetStableSettlementColor(int32 SettlementID)
    {
        static const FLinearColor Palette[] =
        {
            FLinearColor(0.95f, 0.10f, 0.10f, 1.0f), // red
            FLinearColor(0.10f, 0.35f, 1.00f, 1.0f), // blue
            FLinearColor(1.00f, 0.80f, 0.05f, 1.0f), // gold
            FLinearColor(0.75f, 0.20f, 1.00f, 1.0f), // purple
            FLinearColor(1.00f, 0.45f, 0.05f, 1.0f), // orange
            FLinearColor(0.05f, 0.85f, 0.95f, 1.0f), // cyan
            FLinearColor(1.00f, 0.20f, 0.60f, 1.0f), // rose
            FLinearColor(0.60f, 0.90f, 0.20f, 1.0f)  // lime
        };

        const int32 Index = FMath::Abs(SettlementID) % UE_ARRAY_COUNT(Palette);
        return Palette[Index];
    }
}

void AWorldForgeMiniMapHUD::SetLegacyHudSuppressedForFullMap(bool bSuppress)
{
    if (bSuppressLegacyHudForFullMap == bSuppress)
    {
        return;
    }

    bSuppressLegacyHudForFullMap = bSuppress;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[NavigatorHUD] Legacy WorldForge HUD %s."),
        bSuppressLegacyHudForFullMap ? TEXT("suppressed for full map") : TEXT("restored for gameplay"));
}

void AWorldForgeMiniMapHUD::SetMiniMapVisible(bool bVisible)
{
    bShowMiniMap = bVisible;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[MiniMapHUD] MiniMap %s."),
        bShowMiniMap ? TEXT("shown") : TEXT("hidden"));
}

void AWorldForgeMiniMapHUD::ToggleMiniMapVisible()
{
    SetMiniMapVisible(!bShowMiniMap);
}

void AWorldForgeMiniMapHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas)
    {
        return;
    }

    if (!bDrawLegacyMiniMap || bSuppressLegacyHudForFullMap)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (World && bShowMiniMap)
    {
        const USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
        const UProcessorManager* ProcessorManager = World->GetSubsystem<UProcessorManager>();
        const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();

        DrawMiniMapBackground();

        if (bShowCivTerritory)
        {
            DrawCivTerritories(World->GetSubsystem<UWorldMapSubsystem>(), VoxelWorldManager);
        }

        DrawMiniMapBorder();

        if (bShowTitle)
        {
            DrawMiniMapTitle();
        }

        if (SettlementManager && ProcessorManager && bShowPawnDots)
        {
            DrawPawnDots(SettlementManager, ProcessorManager);
        }

        if (SettlementManager && bShowNPCSettlementMarkers)
        {
            DrawNPCSettlementMarkers(SettlementManager);
        }

        if (SettlementManager && bShowPlayerColonyMarker)
        {
            DrawPlayerColonyMarker(SettlementManager);
        }

        if (bShowCameraMarker)
        {
            DrawCameraLocation(VoxelWorldManager);
        }

        if (bShowDirectionIndicator)
        {
            DrawDirectionIndicator();
        }

        if (bShowDebugStats)
        {
            DrawDebugStats(ProcessorManager, VoxelWorldManager);
        }

        if (bShowZLevel)
        {
            DrawZLevelIndicator(VoxelWorldManager);
        }

        if (bShowTerrainInfo)
        {
            DrawTerrainInfo(VoxelWorldManager);
        }
    }
}

bool AWorldForgeMiniMapHUD::GetCameraCellAndYaw(FIntPoint& OutCameraCell, float& OutYawDegrees) const
{
    const APlayerController* PC = GetOwningPlayerController();
    const UWorld* World = GetWorld();
    if (!PC || !World)
    {
        return false;
    }

    const UWorldMapSubsystem* MapSys = World->GetSubsystem<UWorldMapSubsystem>();
    if (!MapSys || !MapSys->IsInitialized())
    {
        return false;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    // MiniMapFix: Use WorldPositionToTileXY (authoritative) instead of
    // VoxelWorldManager::WorldToTerrainCell (depends on stale CachedTerrainGridWidth/Height).
    int32 TileX = 0;
    int32 TileY = 0;
    if (!MapSys->WorldPositionToTileXY(CameraLocation, TileX, TileY))
    {
        return false;
    }
    OutCameraCell = FIntPoint(TileX, TileY);
    OutYawDegrees = CameraRotation.Yaw;

    // Throttled diagnostic log — fires at most once every 2 seconds.
    static float LastLogTime_GetCameraCellAndYaw = -1.0f;
    const float  NowTime = World->GetRealTimeSeconds();
    if (NowTime - LastLogTime_GetCameraCellAndYaw > 2.0f)
    {
        LastLogTime_GetCameraCellAndYaw = NowTime;
        UE_LOG(LogTemp, Log,
            TEXT("[MiniMapFix] GetCameraCellAndYaw: CameraPos=(%.1f,%.1f,%.1f) => CameraCell=(%d,%d) Yaw=%.1f"),
            CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
            OutCameraCell.X, OutCameraCell.Y, OutYawDegrees);
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////
// Camera-centered mapping. If bOrientMiniMapToCamera is true, camera-forward
// points upward on the minimap. Otherwise the minimap stays north-up.
FVector2D AWorldForgeMiniMapHUD::WorldCellToMiniMap(const FIntPoint& WorldCell) const
{
    FIntPoint CameraCell;
    float CameraYaw = 0.0f;
    if (!GetCameraCellAndYaw(CameraCell, CameraYaw))
    {
        return MiniMapPosition;
    }

    const FVector2D Relative(
        static_cast<float>(WorldCell.X - CameraCell.X),
        static_cast<float>(WorldCell.Y - CameraCell.Y));

    float ScreenX = Relative.X;
    float ScreenY = Relative.Y;

    if (bOrientMiniMapToCamera)
    {
        const float YawRad = FMath::DegreesToRadians(CameraYaw);
        const FVector2D Forward(FMath::Cos(YawRad), FMath::Sin(YawRad));
        const FVector2D Right(-Forward.Y, Forward.X);

        // Right becomes screen +X. Camera-forward becomes screen -Y.
        ScreenX = FVector2D::DotProduct(Relative, Right);
        ScreenY = -FVector2D::DotProduct(Relative, Forward);
    }

    const float VisibleHalf = FMath::Max(1.0f, MiniMapZoom);
    const float NormalizedX = (ScreenX / VisibleHalf) * 0.5f + 0.5f;
    const float NormalizedY = (ScreenY / VisibleHalf) * 0.5f + 0.5f;

    return FVector2D(
        MiniMapPosition.X + (NormalizedX * MiniMapSize.X),
        MiniMapPosition.Y + (NormalizedY * MiniMapSize.Y)
    );
}

FVector2D AWorldForgeMiniMapHUD::ClampToMiniMapBounds(const FVector2D& Point) const
{
    const float MinX = MiniMapPosition.X + EdgePadding;
    const float MinY = MiniMapPosition.Y + EdgePadding;
    const float MaxX = MiniMapPosition.X + MiniMapSize.X - EdgePadding;
    const float MaxY = MiniMapPosition.Y + MiniMapSize.Y - EdgePadding;

    return FVector2D(
        FMath::Clamp(Point.X, MinX, MaxX),
        FMath::Clamp(Point.Y, MinY, MaxY)
    );
}

bool AWorldForgeMiniMapHUD::IsWorldCellInBounds(const FIntPoint& WorldCell) const
{
    return true;
}

FLinearColor AWorldForgeMiniMapHUD::GetTerrainColorForTile(const FWorldTileData& Tile) const
{
    const FString Biome = Tile.BiomeType.ToString();

    if (Biome.Contains(TEXT("DeepOcean"))) return FLinearColor(0.03f, 0.10f, 0.25f, 1.0f);
    if (Biome.Contains(TEXT("Ocean")))     return FLinearColor(0.04f, 0.15f, 0.35f, 1.0f);
    if (Biome.Contains(TEXT("Sea")))       return FLinearColor(0.05f, 0.18f, 0.42f, 1.0f);
    if (Biome.Contains(TEXT("Lake")))      return FLinearColor(0.06f, 0.22f, 0.50f, 1.0f);
    if (Biome.Contains(TEXT("River")))     return FLinearColor(0.08f, 0.26f, 0.58f, 1.0f);
    if (Biome.Contains(TEXT("Coastal")) || Biome.Contains(TEXT("Beach"))) return FLinearColor(0.68f, 0.62f, 0.43f, 1.0f);
    if (Biome.Contains(TEXT("Desert")) || Biome.Contains(TEXT("Sand")))   return FLinearColor(0.76f, 0.66f, 0.45f, 1.0f);
    if (Biome.Contains(TEXT("Snow")) || Biome.Contains(TEXT("Ice")))       return FLinearColor(0.78f, 0.84f, 0.88f, 1.0f);
    if (Biome.Contains(TEXT("Mountain")) || Biome.Contains(TEXT("Alpine"))) return FLinearColor(0.45f, 0.45f, 0.43f, 1.0f);
    if (Biome.Contains(TEXT("Canyon")))    return FLinearColor(0.45f, 0.28f, 0.18f, 1.0f);
    if (Biome.Contains(TEXT("Volcanic")) || Biome.Contains(TEXT("Lava"))) return FLinearColor(0.35f, 0.12f, 0.08f, 1.0f);
    if (Biome.Contains(TEXT("Marsh")) || Biome.Contains(TEXT("Swamp")))   return FLinearColor(0.16f, 0.25f, 0.14f, 1.0f);
    if (Biome.Contains(TEXT("Forest")) || Biome.Contains(TEXT("Woodland")) || Biome.Contains(TEXT("Taiga")) || Biome.Contains(TEXT("Jungle"))) return FLinearColor(0.09f, 0.28f, 0.10f, 1.0f);
    if (Biome.Contains(TEXT("Hills")))     return FLinearColor(0.23f, 0.35f, 0.17f, 1.0f);
    if (Biome.Contains(TEXT("Steppe")) || Biome.Contains(TEXT("Savanna")) || Biome.Contains(TEXT("Grass"))) return FLinearColor(0.35f, 0.45f, 0.18f, 1.0f);

    if (Tile.Elevation < 0.48f) return FLinearColor(0.05f, 0.15f, 0.34f, 1.0f);
    return FLinearColor(0.20f, 0.35f, 0.15f, 1.0f);
}

void AWorldForgeMiniMapHUD::DrawMiniMapBackground()
{
    DrawRect(BackgroundColor, MiniMapPosition.X, MiniMapPosition.Y, MiniMapSize.X, MiniMapSize.Y);

    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const UWorldMapSubsystem* MapSys = World->GetSubsystem<UWorldMapSubsystem>();
    const UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>();

    if (bUseCellDrawnMiniMap && MapSys && VoxelMgr)
    {
        DrawMiniMapCellBackground(MapSys, VoxelMgr);
    }
    else
    {
        DrawMiniMapTextureBackground();
    }
}

void AWorldForgeMiniMapHUD::DrawMiniMapCellBackground(const UWorldMapSubsystem* MapSubsystem, const UVoxelWorldManager* VoxelWorldManager)
{
    if (!MapSubsystem || !VoxelWorldManager || !MapSubsystem->IsInitialized())
    {
        return;
    }

    FIntPoint CameraCell;
    float CameraYaw = 0.0f;
    if (!GetCameraCellAndYaw(CameraCell, CameraYaw))
    {
        return;
    }

    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();
    if (GridWidth <= 0 || GridHeight <= 0)
    {
        return;
    }

    const int32 Radius = FMath::CeilToInt(FMath::Max(1.0f, MiniMapZoom * 1.45f));
    const int32 MinX = FMath::Clamp(CameraCell.X - Radius, 0, GridWidth - 1);
    const int32 MaxX = FMath::Clamp(CameraCell.X + Radius, 0, GridWidth - 1);
    const int32 MinY = FMath::Clamp(CameraCell.Y - Radius, 0, GridHeight - 1);
    const int32 MaxY = FMath::Clamp(CameraCell.Y + Radius, 0, GridHeight - 1);

    const float CellSizeX = FMath::Max(TerrainCellMinPixelSize, MiniMapSize.X / FMath::Max(1.0f, MiniMapZoom * 2.0f));
    const float CellSizeY = FMath::Max(TerrainCellMinPixelSize, MiniMapSize.Y / FMath::Max(1.0f, MiniMapZoom * 2.0f));
    const float HalfX = CellSizeX * 0.5f;
    const float HalfY = CellSizeY * 0.5f;

    for (int32 Y = MinY; Y <= MaxY; ++Y)
    {
        for (int32 X = MinX; X <= MaxX; ++X)
        {
            const int32 TileIndex = Y * GridWidth + X;
            FWorldTileData Tile;
            if (!MapSubsystem->GetTileData(TileIndex, Tile))
            {
                continue;
            }

            const FVector2D Point = WorldCellToMiniMap(FIntPoint(X, Y));
            if (Point.X < MiniMapPosition.X || Point.X > MiniMapPosition.X + MiniMapSize.X ||
                Point.Y < MiniMapPosition.Y || Point.Y > MiniMapPosition.Y + MiniMapSize.Y)
            {
                continue;
            }

            DrawRect(GetTerrainColorForTile(Tile), Point.X - HalfX, Point.Y - HalfY, CellSizeX, CellSizeY);
        }
    }
}

void AWorldForgeMiniMapHUD::DrawMiniMapTextureBackground()
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const UWorldMapSubsystem* MapSys = World->GetSubsystem<UWorldMapSubsystem>();
    if (!MapSys || !MapSys->IsInitialized())
    {
        return;
    }

    UTexture2D* MapTex = MapSys->GetCachedDetailedMapTexture();
    if (!MapTex)
    {
        MapTex = MapSys->GetCachedMapTexture();
    }

    if (!MapTex)
    {
        return;
    }

    const APlayerController* PC = GetOwningPlayerController();
    if (!PC)
    {
        return;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    // MiniMapFix: Use WorldPositionToTileXY instead of the stale-cache WorldToTerrainCell.
    int32 CamTileX = 0;
    int32 CamTileY = 0;
    if (!MapSys->WorldPositionToTileXY(CameraLocation, CamTileX, CamTileY))
    {
        return;
    }
    const FIntPoint CameraCell(CamTileX, CamTileY);
    const int32 GW = MapSys->GetGridWidth();
    const int32 GH = MapSys->GetGridHeight();

    if (GW <= 0 || GH <= 0)
    {
        return;
    }

    const float CamU = static_cast<float>(CameraCell.X) / static_cast<float>(GW);
    const float CamV = static_cast<float>(CameraCell.Y) / static_cast<float>(GH);

    const float HalfU = (MiniMapZoom * 0.5f) / static_cast<float>(GW);
    const float HalfV = (MiniMapZoom * 0.5f) / static_cast<float>(GH);

    float U0 = CamU - HalfU;
    float V0 = CamV - HalfV;
    float U1 = CamU + HalfU;
    float V1 = CamV + HalfV;

    const float FullUWidth = U1 - U0;
    const float FullVHeight = V1 - V0;

    float ScreenX = MiniMapPosition.X;
    float ScreenY = MiniMapPosition.Y;
    float ScreenW = MiniMapSize.X;
    float ScreenH = MiniMapSize.Y;

    if (FullUWidth <= KINDA_SMALL_NUMBER || FullVHeight <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    if (U0 < 0.0f)
    {
        const float Fraction = (-U0) / FullUWidth;
        ScreenX += Fraction * MiniMapSize.X;
        ScreenW -= Fraction * MiniMapSize.X;
        U0 = 0.0f;
    }

    if (V0 < 0.0f)
    {
        const float Fraction = (-V0) / FullVHeight;
        ScreenY += Fraction * MiniMapSize.Y;
        ScreenH -= Fraction * MiniMapSize.Y;
        V0 = 0.0f;
    }

    if (U1 > 1.0f)
    {
        const float Fraction = (U1 - 1.0f) / FullUWidth;
        ScreenW -= Fraction * MiniMapSize.X;
        U1 = 1.0f;
    }

    if (V1 > 1.0f)
    {
        const float Fraction = (V1 - 1.0f) / FullVHeight;
        ScreenH -= Fraction * MiniMapSize.Y;
        V1 = 1.0f;
    }

    if (ScreenW > 0.0f && ScreenH > 0.0f && (U1 - U0) > 0.0f && (V1 - V0) > 0.0f)
    {
        DrawTexture(MapTex, ScreenX, ScreenY, ScreenW, ScreenH, U0, V0, U1 - U0, V1 - V0, FLinearColor::White, BLEND_Opaque);
    }
}

void AWorldForgeMiniMapHUD::DrawMiniMapBorder()
{
    const float X = MiniMapPosition.X;
    const float Y = MiniMapPosition.Y;
    const float W = MiniMapSize.X;
    const float H = MiniMapSize.Y;

    DrawLine(X, Y, X + W, Y, BorderColor, 1.5f);
    DrawLine(X + W, Y, X + W, Y + H, BorderColor, 1.5f);
    DrawLine(X + W, Y + H, X, Y + H, BorderColor, 1.5f);
    DrawLine(X, Y + H, X, Y, BorderColor, 1.5f);
}

void AWorldForgeMiniMapHUD::DrawMiniMapTitle()
{
    DrawText(TitleText, FLinearColor::White, MiniMapPosition.X, MiniMapPosition.Y - 18.0f, nullptr, TitleScale, false);
}

bool AWorldForgeMiniMapHUD::ShouldDrawCivTile(const FWorldTileData& Tile) const
{
    if (Tile.ControllingCivID < 0)
    {
        return false;
    }

    if (this->bDebugRevealAllCivTerritory)
    {
        return true;
    }

    return Tile.KnowledgeState.KnowledgeLevel == EKnowledgeLevel::Rumored ||
        Tile.KnowledgeState.KnowledgeLevel == EKnowledgeLevel::Surveyed ||
        Tile.KnowledgeState.KnowledgeLevel == EKnowledgeLevel::Live;
}

void AWorldForgeMiniMapHUD::DrawCivTerritories(const UWorldMapSubsystem* MapSubsystem, const UVoxelWorldManager* VoxelWorldManager)
{
    if (!MapSubsystem || !VoxelWorldManager || !MapSubsystem->IsInitialized())
    {
        return;
    }

    FIntPoint CameraCell;
    float CameraYaw = 0.0f;
    if (!GetCameraCellAndYaw(CameraCell, CameraYaw))
    {
        return;
    }

    const int32 GridWidth = MapSubsystem->GetGridWidth();
    const int32 GridHeight = MapSubsystem->GetGridHeight();
    if (GridWidth <= 0 || GridHeight <= 0)
    {
        return;
    }

    const int32 Radius = FMath::CeilToInt(FMath::Max(1.0f, MiniMapZoom * 1.45f));
    const int32 MinX = FMath::Clamp(CameraCell.X - Radius, 0, GridWidth - 1);
    const int32 MaxX = FMath::Clamp(CameraCell.X + Radius, 0, GridWidth - 1);
    const int32 MinY = FMath::Clamp(CameraCell.Y - Radius, 0, GridHeight - 1);
    const int32 MaxY = FMath::Clamp(CameraCell.Y + Radius, 0, GridHeight - 1);

    const float CellPixelSizeX = FMath::Max(CivTerritoryMinPixelSize, MiniMapSize.X / FMath::Max(1.0f, MiniMapZoom * 2.0f));
    const float CellPixelSizeY = FMath::Max(CivTerritoryMinPixelSize, MiniMapSize.Y / FMath::Max(1.0f, MiniMapZoom * 2.0f));
    const float HalfX = CellPixelSizeX * 0.5f;
    const float HalfY = CellPixelSizeY * 0.5f;

    for (int32 Y = MinY; Y <= MaxY; ++Y)
    {
        for (int32 X = MinX; X <= MaxX; ++X)
        {
            const int32 TileIndex = Y * GridWidth + X;
            FWorldTileData Tile;
            if (!MapSubsystem->GetTileData(TileIndex, Tile) || !ShouldDrawCivTile(Tile))
            {
                continue;
            }

            const FVector2D Point = WorldCellToMiniMap(FIntPoint(X, Y));
            if (Point.X < MiniMapPosition.X || Point.X > MiniMapPosition.X + MiniMapSize.X ||
                Point.Y < MiniMapPosition.Y || Point.Y > MiniMapPosition.Y + MiniMapSize.Y)
            {
                continue;
            }

            const FColor CivColor = MapSubsystem->GetCivColor(Tile.ControllingCivID);
            FLinearColor DrawColor = FLinearColor(CivColor);
            DrawColor.A = CivTerritoryAlpha;

            DrawRect(DrawColor, Point.X - HalfX, Point.Y - HalfY, CellPixelSizeX, CellPixelSizeY);
        }
    }
}

void AWorldForgeMiniMapHUD::DrawPawnDots(const USettlementManager* SettlementManager, const UProcessorManager* ProcessorManager)
{
    if (!SettlementManager || !ProcessorManager)
    {
        return;
    }

    const TArray<FAgentData>& Agents = ProcessorManager->GetAllAgents();

    for (const FAgentData& Agent : Agents)
    {
        const FSettlementState* Settlement = SettlementManager->GetSettlement(Agent.SettlementID);
        if (!Settlement)
        {
            continue;
        }

        const FIntPoint WorldCell = Settlement->WorldOrigin + Agent.CurrentGridPos;
        const FVector2D RawPoint = WorldCellToMiniMap(WorldCell);

        const bool bInside =
            RawPoint.X >= MiniMapPosition.X &&
            RawPoint.X <= MiniMapPosition.X + MiniMapSize.X &&
            RawPoint.Y >= MiniMapPosition.Y &&
            RawPoint.Y <= MiniMapPosition.Y + MiniMapSize.Y;

        if (!bInside && !bShowOffscreenPawnIndicators)
        {
            continue;
        }

        const FVector2D DrawPoint = bInside ? RawPoint : ClampToMiniMapBounds(RawPoint);
        FLinearColor DotColor = GetDotColorForSettlement(SettlementManager, Agent.SettlementID);
        if (!bInside)
        {
            // Keep offscreen pawns readable by settlement/civ color.
            DotColor.A = 0.95f;
        }

        const float Size = bInside ? PawnDotSize : EdgeIndicatorSize;
        const float Half = Size * 0.5f;

        DrawRect(FLinearColor::Black, DrawPoint.X - Half - 1.0f, DrawPoint.Y - Half - 1.0f, Size + 2.0f, Size + 2.0f);
        DrawRect(DotColor, DrawPoint.X - Half, DrawPoint.Y - Half, Size, Size);
    }
}

void AWorldForgeMiniMapHUD::DrawPlayerColonyMarker(const USettlementManager* SettlementManager)
{
    if (!SettlementManager || !SettlementManager->HasPlayerSettlement())
    {
        return;
    }

    const int32 PlayerSettlementID = SettlementManager->GetPlayerSettlementID();
    const FSettlementState* Settlement = SettlementManager->GetSettlement(PlayerSettlementID);
    if (!Settlement)
    {
        return;
    }

    // Use the settlement origin plus a small formation offset as the colony marker.
    const FIntPoint MarkerCell = Settlement->WorldOrigin + FIntPoint(2, 2);
    const FVector2D P = WorldCellToMiniMap(MarkerCell);

    const bool bInside =
        P.X >= MiniMapPosition.X && P.X <= MiniMapPosition.X + MiniMapSize.X &&
        P.Y >= MiniMapPosition.Y && P.Y <= MiniMapPosition.Y + MiniMapSize.Y;

    if (!bInside)
    {
        return;
    }

    const float S = PlayerColonyMarkerSize;
    const float Half = S * 0.5f;
    const FLinearColor C = PlayerDotColor;

    // Black outline.
    DrawRect(FLinearColor::Black, P.X - Half - 2.0f, P.Y - Half - 1.0f, S + 4.0f, S + 5.0f);

    // House body.
    DrawRect(C, P.X - Half, P.Y - Half + 3.0f, S, S - 3.0f);

    // Roof.
    DrawLine(P.X - Half - 1.0f, P.Y - Half + 4.0f, P.X, P.Y - Half - 4.0f, FLinearColor::Black, 4.0f);
    DrawLine(P.X, P.Y - Half - 4.0f, P.X + Half + 1.0f, P.Y - Half + 4.0f, FLinearColor::Black, 4.0f);
    DrawLine(P.X - Half, P.Y - Half + 4.0f, P.X, P.Y - Half - 3.0f, C, 2.0f);
    DrawLine(P.X, P.Y - Half - 3.0f, P.X + Half, P.Y - Half + 4.0f, C, 2.0f);
}


void AWorldForgeMiniMapHUD::DrawNPCSettlementMarkers(const USettlementManager* SettlementManager)
{
    if (!SettlementManager)
    {
        return;
    }

    const int32 PlayerSettlementID = SettlementManager->HasPlayerSettlement()
        ? SettlementManager->GetPlayerSettlementID()
        : INDEX_NONE;

    for (const FSettlementState& Settlement : SettlementManager->GetAllSettlements())
    {
        if (Settlement.SettlementID < 0 || Settlement.SettlementID == PlayerSettlementID)
        {
            continue;
        }

        // Settlement marker uses a small offset so it sits near the local agent formation center.
        const FIntPoint MarkerCell = Settlement.WorldOrigin + FIntPoint(2, 2);
        const FVector2D RawPoint = WorldCellToMiniMap(MarkerCell);

        const bool bInside =
            RawPoint.X >= MiniMapPosition.X && RawPoint.X <= MiniMapPosition.X + MiniMapSize.X &&
            RawPoint.Y >= MiniMapPosition.Y && RawPoint.Y <= MiniMapPosition.Y + MiniMapSize.Y;

        const FVector2D P = bInside ? RawPoint : ClampToMiniMapBounds(RawPoint);
        FLinearColor C = GetStableSettlementColor(Settlement.SettlementID);
        C.A = bInside ? 1.0f : 0.90f;

        const float S = bInside ? NPCSettlementMarkerSize : FMath::Max(NPCSettlementMarkerSize, EdgeIndicatorSize + 2.0f);
        const float Half = S * 0.5f;

        // Black outline / pole.
        DrawLine(P.X - Half * 0.35f, P.Y + Half, P.X - Half * 0.35f, P.Y - Half, FLinearColor::Black, 3.0f);
        DrawRect(FLinearColor::Black, P.X - Half * 0.35f, P.Y - Half, S + 2.0f, Half + 3.0f);

        // Colored flag body.
        DrawLine(P.X - Half * 0.35f, P.Y + Half, P.X - Half * 0.35f, P.Y - Half, C, 1.5f);
        DrawRect(C, P.X - Half * 0.25f, P.Y - Half + 1.0f, S * 0.85f, Half);
    }
}

void AWorldForgeMiniMapHUD::DrawCameraLocation(const UVoxelWorldManager* VoxelWorldManager)
{
    const FVector2D Center(MiniMapPosition.X + MiniMapSize.X * 0.5f, MiniMapPosition.Y + MiniMapSize.Y * 0.5f);
    const float DotSize = FMath::Max(3.0f, CameraMarkerSize * 0.45f);
    const float Half = DotSize * 0.5f;

    DrawRect(FLinearColor::Black, Center.X - Half - 1.0f, Center.Y - Half - 1.0f, DotSize + 2.0f, DotSize + 2.0f);
    DrawRect(CameraColor, Center.X - Half, Center.Y - Half, DotSize, DotSize);
}

void AWorldForgeMiniMapHUD::DrawDirectionIndicator()
{
    const APlayerController* PC = GetOwningPlayerController();
    if (!PC)
    {
        return;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    const FVector2D Center(MiniMapPosition.X + MiniMapSize.X * 0.5f, MiniMapPosition.Y + MiniMapSize.Y * 0.5f);

    FVector2D Forward(0.0f, -1.0f);
    if (!bOrientMiniMapToCamera)
    {
        const float YawRadians = FMath::DegreesToRadians(CameraRotation.Yaw);
        Forward = FVector2D(FMath::Cos(YawRadians), FMath::Sin(YawRadians));
    }

    const FVector2D Right(-Forward.Y, Forward.X);
    const float ArrowLength = DirectionIndicatorSize;
    const float WingLength = DirectionIndicatorSize * 0.45f;
    const float WingWidth = DirectionIndicatorSize * 0.28f;

    const FVector2D Tip = Center + Forward * ArrowLength;
    const FVector2D Tail = Center - Forward * (ArrowLength * 0.45f);
    const FVector2D LeftWing = Tail + Right * WingWidth + Forward * WingLength;
    const FVector2D RightWing = Tail - Right * WingWidth + Forward * WingLength;

    DrawLine(Tail.X, Tail.Y, Tip.X, Tip.Y, DirectionIndicatorShadowColor, 4.0f);
    DrawLine(Tip.X, Tip.Y, LeftWing.X, LeftWing.Y, DirectionIndicatorShadowColor, 4.0f);
    DrawLine(Tip.X, Tip.Y, RightWing.X, RightWing.Y, DirectionIndicatorShadowColor, 4.0f);

    DrawLine(Tail.X, Tail.Y, Tip.X, Tip.Y, DirectionIndicatorColor, 2.0f);
    DrawLine(Tip.X, Tip.Y, LeftWing.X, LeftWing.Y, DirectionIndicatorColor, 2.0f);
    DrawLine(Tip.X, Tip.Y, RightWing.X, RightWing.Y, DirectionIndicatorColor, 2.0f);

    if (bOrientMiniMapToCamera)
    {
        DrawText(TEXT("FWD"), DirectionIndicatorColor, Center.X + 8.0f, Center.Y - DirectionIndicatorSize - 10.0f, nullptr, 0.65f, false);
    }
    else
    {
        DrawText(TEXT("N"), DirectionIndicatorColor, MiniMapPosition.X + MiniMapSize.X * 0.5f - 4.0f, MiniMapPosition.Y + 4.0f, nullptr, 0.75f, false);
        DrawText(TEXT("E"), DirectionIndicatorColor, MiniMapPosition.X + MiniMapSize.X - 14.0f, MiniMapPosition.Y + MiniMapSize.Y * 0.5f - 6.0f, nullptr, 0.75f, false);
        DrawText(TEXT("S"), DirectionIndicatorColor, MiniMapPosition.X + MiniMapSize.X * 0.5f - 4.0f, MiniMapPosition.Y + MiniMapSize.Y - 16.0f, nullptr, 0.75f, false);
        DrawText(TEXT("W"), DirectionIndicatorColor, MiniMapPosition.X + 5.0f, MiniMapPosition.Y + MiniMapSize.Y * 0.5f - 6.0f, nullptr, 0.75f, false);
    }
}

void AWorldForgeMiniMapHUD::DrawDebugStats(const UProcessorManager* ProcessorManager, const UVoxelWorldManager* VoxelWorldManager)
{
    const int32 AgentCount = ProcessorManager ? ProcessorManager->GetAllAgents().Num() : 0;
    DrawText(FString::Printf(TEXT("Agents: %d"), AgentCount), FLinearColor::Green, MiniMapPosition.X, MiniMapPosition.Y + MiniMapSize.Y + 5.0f, nullptr, 0.9f, false);
}

FLinearColor AWorldForgeMiniMapHUD::GetDotColorForSettlement(const USettlementManager* SettlementManager, int32 SettlementID) const
{
    if (SettlementManager && SettlementManager->HasPlayerSettlement() && SettlementID == SettlementManager->GetPlayerSettlementID())
    {
        return PlayerDotColor;
    }

    return GetStableSettlementColor(SettlementID);
}

void AWorldForgeMiniMapHUD::DrawZLevelIndicator(const UVoxelWorldManager* VoxelWorldManager)
{
    const APlayerController* PC = GetOwningPlayerController();
    if (!PC)
    {
        return;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    const float VoxelSize = VoxelWorldManager ? VoxelWorldManager->GetRenderVoxelSize() : 100.0f;
    const float CameraZVoxels = CameraLocation.Z / FMath::Max(1.0f, VoxelSize);

    float SurfaceZ = 0.0f;
    FString DepthLabel;

    if (VoxelWorldManager)
    {
        // MiniMapFix: Use WorldMapSubsystem::WorldPositionToTileXY (authoritative) instead of
        // VoxelWorldManager::WorldToTerrainCell (depends on stale CachedTerrainGridWidth/Height).
        FIntPoint CameraCell(0, 0);
        bool bGotCell = false;

        if (const UWorld* ZWorld = GetWorld())
        {
            if (const UWorldMapSubsystem* MapSys = ZWorld->GetSubsystem<UWorldMapSubsystem>())
            {
                if (MapSys->IsInitialized())
                {
                    int32 TileX = 0;
                    int32 TileY = 0;
                    if (MapSys->WorldPositionToTileXY(CameraLocation, TileX, TileY))
                    {
                        CameraCell = FIntPoint(TileX, TileY);
                        bGotCell = true;
                    }
                }
            }
        }

        if (!bGotCell)
        {
            // Fallback: subsystem not ready yet; stale result is acceptable for Z display.
            CameraCell = VoxelWorldManager->WorldToTerrainCell(CameraLocation);
        }

        FVector SurfaceWorldPos;
        if (VoxelWorldManager->TryGetSurfaceWorldLocation(CameraCell, SurfaceWorldPos))
        {
            SurfaceZ = SurfaceWorldPos.Z / FMath::Max(1.0f, VoxelSize);
        }
    }

    const float RelativeZ = CameraZVoxels - SurfaceZ;
    const bool bUnderground = RelativeZ < 0.0f;

    if (bUnderground)
    {
        DepthLabel = FString::Printf(TEXT("Z: %.0f  [UNDERGROUND %.0fv deep]"), CameraZVoxels, FMath::Abs(RelativeZ));
    }
    else
    {
        DepthLabel = FString::Printf(TEXT("Z: %.0f  [%.0fv above surface]"), CameraZVoxels, RelativeZ);
    }

    const float DrawX = MiniMapPosition.X;
    const float DrawY = MiniMapPosition.Y + MiniMapSize.Y + 20.0f;
    const FLinearColor DisplayColor = bUnderground ? FLinearColor(1.0f, 0.3f, 0.1f, 1.0f) : ZLevelColor;

    DrawText(DepthLabel, DisplayColor, DrawX, DrawY, nullptr, 0.85f, false);

    const float BarY = DrawY + 16.0f;
    const float BarWidth = MiniMapSize.X;
    const float BarHeight = 6.0f;

    DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.8f), DrawX, BarY, BarWidth, BarHeight);

    const float NormZ = FMath::Clamp((RelativeZ + 100.0f) / 200.0f, 0.0f, 1.0f);
    const float MarkerX = DrawX + NormZ * BarWidth;

    DrawLine(DrawX + BarWidth * 0.5f, BarY, DrawX + BarWidth * 0.5f, BarY + BarHeight, FLinearColor::White, 1.0f);
    DrawRect(DisplayColor, MarkerX - 2.0f, BarY - 1.0f, 4.0f, BarHeight + 2.0f);
}

void AWorldForgeMiniMapHUD::DrawTerrainInfo(const UVoxelWorldManager* VoxelWorldManager)
{
    const APlayerController* PC = GetOwningPlayerController();
    if (!PC)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    const UWorldMapSubsystem* MapSys = World->GetSubsystem<UWorldMapSubsystem>();
    if (!MapSys || !MapSys->IsInitialized())
    {
        return;
    }

    // MiniMapFix: Use the authoritative helper; ignore VoxelWorldManager stale-cache path.
    int32 CamTileX = 0;
    int32 CamTileY = 0;
    if (!MapSys->WorldPositionToTileXY(CameraLocation, CamTileX, CamTileY))
    {
        return;
    }
    const FIntPoint CameraCell(CamTileX, CamTileY);

    // Throttled diagnostic log — fires at most once every 2 seconds.
    static float LastLogTime_DrawTerrainInfo = -1.0f;
    const float  NowTime_DTI = World ? World->GetRealTimeSeconds() : -1.0f;
    if (NowTime_DTI >= 0.0f && NowTime_DTI - LastLogTime_DrawTerrainInfo > 2.0f)
    {
        LastLogTime_DrawTerrainInfo = NowTime_DTI;
        UE_LOG(LogTemp, Log,
            TEXT("[MiniMapFix] DrawTerrainInfo: CameraPos=(%.1f,%.1f,%.1f) => CameraCell=(%d,%d) Grid=%dx%d"),
            CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
            CameraCell.X, CameraCell.Y, MapSys->GetGridWidth(), MapSys->GetGridHeight());
    }

    const int32 TileIndex = CameraCell.Y * MapSys->GetGridWidth() + CameraCell.X;
    FWorldTileData TileData;
    if (!MapSys->GetTileData(TileIndex, TileData))
    {
        return;
    }

    FString BiomeName = TEXT("Unknown");
    if (TileData.BiomeType.IsValid())
    {
        BiomeName = TileData.BiomeType.GetTagName().ToString();
        int32 DotPos = INDEX_NONE;
        if (BiomeName.FindLastChar('.', DotPos))
        {
            BiomeName = BiomeName.Mid(DotPos + 1);
        }
    }

    const float DrawX = MiniMapPosition.X;
    float DrawY = MiniMapPosition.Y + MiniMapSize.Y + 46.0f;
    const float LineHeight = 14.0f;

    DrawText(FString::Printf(TEXT("Biome: %s"), *BiomeName), TerrainInfoColor, DrawX, DrawY, nullptr, 0.8f, false);
    DrawY += LineHeight;

    DrawText(FString::Printf(TEXT("Elev: %.2f  Temp: %.2f  Rain: %.2f"), TileData.Elevation, TileData.Temperature, TileData.Rainfall), TerrainInfoColor, DrawX, DrawY, nullptr, 0.8f, false);
    DrawY += LineHeight;

    const float CaveI = TileData.VoxelProfile.CaveIntensity;
    const float CanyonI = TileData.VoxelProfile.CanyonIntensity;

    FString UndergroundInfo;
    if (CaveI > 0.0f && CanyonI > 0.0f)
    {
        UndergroundInfo = FString::Printf(TEXT("Cave:%.1f Canyon:%.1f"), CaveI, CanyonI);
    }
    else if (CaveI > 0.0f)
    {
        const bool bLavaTube = TileData.VoxelProfile.CaveZScale < 0.5f;
        UndergroundInfo = bLavaTube
            ? FString::Printf(TEXT("Lava Tubes:%.1f"), CaveI)
            : FString::Printf(TEXT("Caves:%.1f Depth:%.0f-%.0fv"), CaveI, TileData.VoxelProfile.CaveMinDepth, TileData.VoxelProfile.CaveMaxDepth);
    }
    else if (CanyonI > 0.0f)
    {
        UndergroundInfo = FString::Printf(TEXT("Canyon:%.1f Depth:%.0fv"), CanyonI, TileData.VoxelProfile.CanyonMaxDepth);
    }
    else
    {
        UndergroundInfo = TEXT("No underground features");
    }

    const FLinearColor UGColor = (CaveI > 0.0f || CanyonI > 0.0f)
        ? FLinearColor(0.4f, 0.7f, 1.0f, 1.0f)
        : FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

    DrawText(UndergroundInfo, UGColor, DrawX, DrawY, nullptr, 0.8f, false);
    DrawY += LineHeight;

    DrawText(FString::Printf(TEXT("Cell: (%d, %d)  WorldZ: %.0f"), CameraCell.X, CameraCell.Y, CameraLocation.Z), FLinearColor(0.6f, 0.6f, 0.6f, 1.0f), DrawX, DrawY, nullptr, 0.75f, false);
}
