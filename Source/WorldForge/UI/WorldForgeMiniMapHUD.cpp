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

    if (!bShowMiniMap || !Canvas)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
    const UProcessorManager* ProcessorManager = World->GetSubsystem<UProcessorManager>();
    const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();

    DrawMiniMapBackground();
    DrawMiniMapBorder();

    if (bShowTitle)
    {
        DrawMiniMapTitle();
    }

    if (SettlementManager && ProcessorManager && bShowPawnDots)
    {
        DrawPawnDots(SettlementManager, ProcessorManager);
    }

    if (bShowCameraMarker)
    {
        DrawCameraLocation(VoxelWorldManager);
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

//////////////////////////////////////////////////////////////////////////
// Camera-centered mapping WITH ZOOM
FVector2D AWorldForgeMiniMapHUD::WorldCellToMiniMap(const FIntPoint& WorldCell) const
{
    const APlayerController* PC = GetOwningPlayerController();
    if (!PC)
    {
        return MiniMapPosition;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    const UWorld* World = GetWorld();
    if (!World)
    {
        return MiniMapPosition;
    }

    const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelWorldManager)
    {
        return MiniMapPosition;
    }

    const FIntPoint CameraCell = VoxelWorldManager->WorldToTerrainCell(CameraLocation);

    const int32 RelativeX = WorldCell.X - CameraCell.X;
    const int32 RelativeY = WorldCell.Y - CameraCell.Y;

    const float VisibleHalf = FMath::Max(1.0f, MiniMapZoom);

    const float NormalizedX = (RelativeX / VisibleHalf) * 0.5f + 0.5f;
    const float NormalizedY = (RelativeY / VisibleHalf) * 0.5f + 0.5f;

    return FVector2D(
        MiniMapPosition.X + (NormalizedX * MiniMapSize.X),
        MiniMapPosition.Y + (NormalizedY * MiniMapSize.Y)
    );
}

//////////////////////////////////////////////////////////////////////////
// Clamp to minimap bounds (for off-screen indicators)
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

//////////////////////////////////////////////////////////////////////////

bool AWorldForgeMiniMapHUD::IsWorldCellInBounds(const FIntPoint& WorldCell) const
{
    return true;
}

void AWorldForgeMiniMapHUD::DrawMiniMapBackground()
{
    DrawRect(
        BackgroundColor,
        MiniMapPosition.X,
        MiniMapPosition.Y,
        MiniMapSize.X,
        MiniMapSize.Y
    );

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

    const UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>();
    const APlayerController* PC = GetOwningPlayerController();
    if (!PC || !VoxelMgr)
    {
        return;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    const FIntPoint CameraCell = VoxelMgr->WorldToTerrainCell(CameraLocation);
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
        DrawTexture(
            MapTex,
            ScreenX,
            ScreenY,
            ScreenW,
            ScreenH,
            U0,
            V0,
            U1 - U0,
            V1 - V0,
            FLinearColor::White,
            BLEND_Opaque
        );
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
    DrawText(
        TitleText,
        FLinearColor::White,
        MiniMapPosition.X,
        MiniMapPosition.Y - 18.0f,
        nullptr,
        TitleScale,
        false
    );
}

void AWorldForgeMiniMapHUD::DrawPawnDots(
    const USettlementManager* SettlementManager,
    const UProcessorManager* ProcessorManager)
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

        const FVector2D DrawPoint = bInside ? RawPoint : ClampToMiniMapBounds(RawPoint);

        const FLinearColor DotColor = GetDotColorForSettlement(Agent.SettlementID);

        const float Size = bInside ? PawnDotSize : EdgeIndicatorSize;
        const float Half = Size * 0.5f;

        DrawRect(
            FLinearColor::Black,
            DrawPoint.X - Half - 1.0f,
            DrawPoint.Y - Half - 1.0f,
            Size + 2.0f,
            Size + 2.0f
        );

        DrawRect(
            bInside ? DotColor : OutOfBoundsColor,
            DrawPoint.X - Half,
            DrawPoint.Y - Half,
            Size,
            Size
        );
    }
}

void AWorldForgeMiniMapHUD::DrawCameraLocation(const UVoxelWorldManager* VoxelWorldManager)
{
    const FVector2D Center(
        MiniMapPosition.X + MiniMapSize.X * 0.5f,
        MiniMapPosition.Y + MiniMapSize.Y * 0.5f
    );

    DrawLine(
        Center.X - CameraMarkerSize,
        Center.Y - CameraMarkerSize,
        Center.X + CameraMarkerSize,
        Center.Y + CameraMarkerSize,
        CameraColor,
        1.5f
    );

    DrawLine(
        Center.X - CameraMarkerSize,
        Center.Y + CameraMarkerSize,
        Center.X + CameraMarkerSize,
        Center.Y - CameraMarkerSize,
        CameraColor,
        1.5f
    );
}

void AWorldForgeMiniMapHUD::DrawDebugStats(
    const UProcessorManager* ProcessorManager,
    const UVoxelWorldManager* VoxelWorldManager)
{
    const int32 AgentCount = ProcessorManager ? ProcessorManager->GetAllAgents().Num() : 0;

    DrawText(
        FString::Printf(TEXT("Agents: %d"), AgentCount),
        FLinearColor::Green,
        MiniMapPosition.X,
        MiniMapPosition.Y + MiniMapSize.Y + 5.0f,
        nullptr,
        0.9f,
        false
    );
}

FLinearColor AWorldForgeMiniMapHUD::GetDotColorForSettlement(int32 SettlementID) const
{
    return (SettlementID == 0) ? PlayerDotColor : AIDotColor;
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
        const FIntPoint CameraCell = VoxelWorldManager->WorldToTerrainCell(CameraLocation);
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

    const FLinearColor DisplayColor = bUnderground
        ? FLinearColor(1.0f, 0.3f, 0.1f, 1.0f)
        : ZLevelColor;

    DrawText(
        DepthLabel,
        DisplayColor,
        DrawX,
        DrawY,
        nullptr,
        0.85f,
        false
    );

    const float BarY = DrawY + 16.0f;
    const float BarWidth = MiniMapSize.X;
    const float BarHeight = 6.0f;

    DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.8f), DrawX, BarY, BarWidth, BarHeight);

    const float NormZ = FMath::Clamp((RelativeZ + 100.0f) / 200.0f, 0.0f, 1.0f);
    const float MarkerX = DrawX + NormZ * BarWidth;

    DrawLine(
        DrawX + BarWidth * 0.5f,
        BarY,
        DrawX + BarWidth * 0.5f,
        BarY + BarHeight,
        FLinearColor::White,
        1.0f
    );

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

    FIntPoint CameraCell = FIntPoint(0, 0);
    if (VoxelWorldManager)
    {
        CameraCell = VoxelWorldManager->WorldToTerrainCell(CameraLocation);
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

    DrawText(
        FString::Printf(TEXT("Biome: %s"), *BiomeName),
        TerrainInfoColor,
        DrawX,
        DrawY,
        nullptr,
        0.8f,
        false
    );

    DrawY += LineHeight;

    DrawText(
        FString::Printf(TEXT("Elev: %.2f  Temp: %.2f  Rain: %.2f"), TileData.Elevation, TileData.Temperature, TileData.Rainfall),
        TerrainInfoColor,
        DrawX,
        DrawY,
        nullptr,
        0.8f,
        false
    );

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

    DrawText(
        UndergroundInfo,
        UGColor,
        DrawX,
        DrawY,
        nullptr,
        0.8f,
        false
    );

    DrawY += LineHeight;

    DrawText(
        FString::Printf(TEXT("Cell: (%d, %d)  WorldZ: %.0f"), CameraCell.X, CameraCell.Y, CameraLocation.Z),
        FLinearColor(0.6f, 0.6f, 0.6f, 1.0f),
        DrawX,
        DrawY,
        nullptr,
        0.75f,
        false
    );
}