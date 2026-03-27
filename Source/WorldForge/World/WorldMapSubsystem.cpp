#include "WorldMapSubsystem.h"
#include "../World/WorldGenPipeline.h"

void UWorldMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Initialized."));
}

void UWorldMapSubsystem::Deinitialize()
{
    bInitialized = false;
    Super::Deinitialize();
}

void UWorldMapSubsystem::InitializeFromPipeline(UWorldGenPipeline* Pipeline)
{
    if (!Pipeline || !Pipeline->IsComplete())
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldMapSubsystem] Pipeline null or incomplete!"));
        return;
    }

    WorldTiles = Pipeline->GetWorldTiles();
    Civilizations = Pipeline->GetCivilizations();
    HistoryLog = Pipeline->GetHistoryLog();
    GridWidth = Pipeline->GetGridWidth();
    GridHeight = Pipeline->GetGridHeight();

    bInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Loaded %d tiles, %d civs, %d events"),
        WorldTiles.Num(), Civilizations.Num(), HistoryLog.Num());
}

void UWorldMapSubsystem::Tick(float DeltaTime)
{
    if (!bInitialized) return;

    TickAccumulator += DeltaTime;

    if (TickAccumulator >= LiveWorldInterval)
    {
        TickAccumulator = 0.0f;
        TickLivingWorld();
    }
}

void UWorldMapSubsystem::TickLivingWorld()
{
    UE_LOG(LogTemp, Verbose, TEXT("[WorldMapSubsystem] Living world tick"));
}

bool UWorldMapSubsystem::GetTileData(int32 TileID, FWorldTileData& OutTile) const
{
    if (!WorldTiles.IsValidIndex(TileID))
        return false;

    OutTile = WorldTiles[TileID];
    return true;
}

const FWorldTileData* UWorldMapSubsystem::GetTileDataPtr(int32 TileID) const
{
    return WorldTiles.IsValidIndex(TileID) ? &WorldTiles[TileID] : nullptr;
}

FWorldTileData* UWorldMapSubsystem::GetMutableTileDataPtr(int32 TileID)
{
    return WorldTiles.IsValidIndex(TileID) ? &WorldTiles[TileID] : nullptr;
}

bool UWorldMapSubsystem::GetCivData(int32 CivID, FCivilizationData& OutCiv) const
{
    for (const FCivilizationData& C : Civilizations)
    {
        if (C.CivID == CivID)
        {
            OutCiv = C;
            return true;
        }
    }

    return false;
}

void UWorldMapSubsystem::RevealTile(int32 TileID, EKnowledgeLevel NewLevel, float Accuracy)
{
    FWorldTileData* T = GetMutableTileDataPtr(TileID);
    if (!T) return;

    T->KnowledgeState.KnowledgeLevel = NewLevel;
    T->KnowledgeState.DataAccuracy = FMath::Clamp(Accuracy, 0.0f, 1.0f);
}

EKnowledgeLevel UWorldMapSubsystem::GetTileKnowledge(int32 TileID) const
{
    const FWorldTileData* T = GetTileDataPtr(TileID);
    return T ? T->KnowledgeState.KnowledgeLevel : EKnowledgeLevel::Unexplored;
}

// ADDED -- BF-05
int32 UWorldMapSubsystem::AddHistoryEvent(const FHistoryEventData& Event)
{
    FHistoryEventData NewEvent = Event;
    NewEvent.EventID = HistoryLog.Num();

    HistoryLog.Add(NewEvent);

    UE_LOG(LogTemp, Log,
        TEXT("[History] EventID=%d Type=%s"),
        NewEvent.EventID,
        *NewEvent.EventType.ToString());

    return NewEvent.EventID;
}