#include "FogOfWarManager.h"
#include "../World/WorldMapSubsystem.h"

void UFogOfWarManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UFogOfWarManager::InitializeFogForWorld(int32 W, int32 H)
{
    GridWidth = W;
    GridHeight = H;
    bFogInitialized = true;
}

void UFogOfWarManager::RevealTile(int32 TileID, EKnowledgeLevel NewLevel, float Accuracy)
{
    if (UWorldMapSubsystem* WM = GetWorld()->GetSubsystem<UWorldMapSubsystem>())
    {
        WM->RevealTile(TileID, NewLevel, Accuracy);
    }

    OnTileKnowledgeChanged(TileID, NewLevel, GetTileWorldPosition(TileID));
}

FVector UFogOfWarManager::GetTileWorldPosition(int32 TileID) const
{
    if (GridWidth <= 0)
    {
        return FVector::ZeroVector;
    }

    return FVector(
        float(TileID % GridWidth) * TileWorldSize,
        float(TileID / GridWidth) * TileWorldSize,
        0.f
    );
}