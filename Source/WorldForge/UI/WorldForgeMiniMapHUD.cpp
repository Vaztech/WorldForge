#include "UI/WorldForgeMiniMapHUD.h"

#include "ProcessorManager.h"
#include "SettlementManager.h"
#include "VoxelWorldManager.h"
#include "WorldForgeDataTypes.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

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

    const UVoxelWorldManager* VoxelWorldManager = GetWorld()->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelWorldManager)
    {
        return MiniMapPosition;
    }

    const FIntPoint CameraCell = VoxelWorldManager->WorldToTerrainCell(CameraLocation);

    const int32 RelativeX = WorldCell.X - CameraCell.X;
    const int32 RelativeY = WorldCell.Y - CameraCell.Y;

    const float VisibleHalf = MiniMapZoom; // 🔥 now using property

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

//////////////////////////////////////////////////////////////////////////
// 🔥 UPDATED: Handles BOTH on-screen + off-screen pawns
void AWorldForgeMiniMapHUD::DrawPawnDots(const USettlementManager* SettlementManager, const UProcessorManager* ProcessorManager)
{
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

        // Outline
        DrawRect(
            FLinearColor::Black,
            DrawPoint.X - Half - 1.0f,
            DrawPoint.Y - Half - 1.0f,
            Size + 2.0f,
            Size + 2.0f
        );

        // Fill
        DrawRect(
            bInside ? DotColor : OutOfBoundsColor,
            DrawPoint.X - Half,
            DrawPoint.Y - Half,
            Size,
            Size
        );
    }
}

//////////////////////////////////////////////////////////////////////////

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

void AWorldForgeMiniMapHUD::DrawDebugStats(const UProcessorManager* ProcessorManager, const UVoxelWorldManager* VoxelWorldManager)
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