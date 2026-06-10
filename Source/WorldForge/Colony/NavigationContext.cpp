#include "NavigationContext.h"
#include "SettlementManager.h"

const TMap<FIntPoint, FNavCell> UNavigationContext::EmptyGrid;

void UNavigationContext::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[NavigationContext] Initialized."));
}

void UNavigationContext::InitializeGrid(int32 SettlementID, int32 GridWidth, int32 GridHeight)
{
    // CUBE-TERRAIN-04U-STEP3: preserve walkability data if grid was already stamped this session.
    if (SettlementGrids.Contains(SettlementID) && SettlementGrids[SettlementID].Num() > 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CubeTerrainNavBuild] SettlementID=%d grid already exists (%d cells); preserving."),
            SettlementID,
            SettlementGrids[SettlementID].Num());
        return;
    }

    TMap<FIntPoint, FNavCell>& Grid = SettlementGrids.FindOrAdd(SettlementID);
    Grid.Empty();

    for (int32 Y = 0; Y < GridHeight; Y++)
    {
        for (int32 X = 0; X < GridWidth; X++)
        {
            FNavCell Cell;
            Cell.GridPos = FIntPoint(X, Y);
            Grid.Add(Cell.GridPos, Cell);
        }
    }

    GridDimensions.Add(SettlementID, FIntPoint(GridWidth, GridHeight));

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[NavigationContext] Grid %dx%d for settlement %d"),
        GridWidth,
        GridHeight,
        SettlementID
    );
}

TArray<FIntPoint> UNavigationContext::FindPath(
    int32 SettlementID,
    FIntPoint Start,
    FIntPoint Goal
) const
{
    if (Start == Goal)
    {
        return { Goal };
    }

    const TMap<FIntPoint, FNavCell>* GridPtr = SettlementGrids.Find(SettlementID);
    if (!GridPtr)
    {
        return {};
    }

    const TMap<FIntPoint, FNavCell>& Grid = *GridPtr;
    const FIntPoint* DimensionsPtr = GridDimensions.Find(SettlementID);

    const int32 GridWidth = DimensionsPtr ? DimensionsPtr->X : 64;
    const int32 GridHeight = DimensionsPtr ? DimensionsPtr->Y : 64;

    TMap<FIntPoint, float> GScore;
    TMap<FIntPoint, float> FScore;
    TMap<FIntPoint, FIntPoint> CameFrom;
    TArray<FIntPoint> OpenSet;

    GScore.Add(Start, 0.f);
    FScore.Add(Start, Heuristic(Start, Goal));
    OpenSet.Add(Start);

    for (int32 Iteration = 0; Iteration < GridWidth * GridHeight && OpenSet.Num() > 0; Iteration++)
    {
        FIntPoint Current = OpenSet[0];
        float BestFScore = FScore.FindOrAdd(Current);

        for (const FIntPoint& Point : OpenSet)
        {
            const float Score = FScore.FindOrAdd(Point);
            if (Score < BestFScore)
            {
                BestFScore = Score;
                Current = Point;
            }
        }

        if (Current == Goal)
        {
            TArray<FIntPoint> Path;
            FIntPoint Cursor = Current;

            while (CameFrom.Contains(Cursor))
            {
                Path.Insert(Cursor, 0);
                Cursor = CameFrom[Cursor];
            }

            Path.Insert(Cursor, 0);
            return Path;
        }

        OpenSet.Remove(Current);

        for (const FIntPoint& Neighbor : GetNeighbors(Current, GridWidth, GridHeight))
        {
            const FNavCell* Cell = Grid.Find(Neighbor);
            if (!Cell || !Cell->bWalkable)
            {
                continue;
            }

            const float TentativeG = GScore.FindOrAdd(Current) + Cell->PathCost;
            const float ExistingG = GScore.Contains(Neighbor)
                ? GScore[Neighbor]
                : TNumericLimits<float>::Max();

            if (TentativeG < ExistingG)
            {
                CameFrom.Add(Neighbor, Current);
                GScore.Add(Neighbor, TentativeG);
                FScore.Add(Neighbor, TentativeG + Heuristic(Neighbor, Goal));

                if (!OpenSet.Contains(Neighbor))
                {
                    OpenSet.Add(Neighbor);
                }
            }
        }
    }

    return {};
}

void UNavigationContext::SetCellWalkable(int32 SettlementID, FIntPoint Position, bool bWalkable)
{
    if (TMap<FIntPoint, FNavCell>* Grid = SettlementGrids.Find(SettlementID))
    {
        if (FNavCell* Cell = Grid->Find(Position))
        {
            Cell->bWalkable = bWalkable;
        }
    }
}

bool UNavigationContext::IsCellWalkable(int32 SettlementID, FIntPoint Position) const
{
    const TMap<FIntPoint, FNavCell>* Grid = SettlementGrids.Find(SettlementID);
    if (!Grid)
    {
        return false;
    }

    const FNavCell* Cell = Grid->Find(Position);
    return Cell && Cell->bWalkable;
}

void UNavigationContext::SetCellOccupant(int32 SettlementID, FIntPoint Position, int32 AgentID)
{
    if (TMap<FIntPoint, FNavCell>* Grid = SettlementGrids.Find(SettlementID))
    {
        if (FNavCell* Cell = Grid->Find(Position))
        {
            Cell->OccupantID = AgentID;
        }
    }
}

FVector UNavigationContext::GridToWorld(FIntPoint Position, float CellSize) const
{
    return FVector(Position.X * CellSize, Position.Y * CellSize, 0.f);
}

FIntPoint UNavigationContext::WorldToGrid(FVector Position, float CellSize) const
{
    return FIntPoint(
        FMath::FloorToInt(Position.X / CellSize),
        FMath::FloorToInt(Position.Y / CellSize)
    );
}

FIntPoint UNavigationContext::NavToWorld(int32 SettlementID, FIntPoint NavCell) const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return NavCell;
    }

    const USettlementManager* SettlementMgr = World->GetSubsystem<USettlementManager>();
    if (!SettlementMgr)
    {
        return NavCell;
    }

    const FSettlementState* Settlement = SettlementMgr->GetSettlement(SettlementID);
    if (!Settlement)
    {
        return NavCell;
    }

    return Settlement->WorldOrigin + NavCell;
}

TArray<int32> UNavigationContext::GetRegisteredSettlementIDs() const
{
    TArray<int32> SettlementIDs;
    SettlementGrids.GetKeys(SettlementIDs);
    SettlementIDs.Sort();
    return SettlementIDs;
}

const TMap<FIntPoint, FNavCell>& UNavigationContext::GetGrid(int32 SettlementID) const
{
    const TMap<FIntPoint, FNavCell>* Grid = SettlementGrids.Find(SettlementID);
    return Grid ? *Grid : EmptyGrid;
}

float UNavigationContext::Heuristic(FIntPoint A, FIntPoint B) const
{
    return static_cast<float>(FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y));
}

TArray<FIntPoint> UNavigationContext::GetNeighbors(FIntPoint Position, int32 GridWidth, int32 GridHeight) const
{
    TArray<FIntPoint> Neighbors;

    const FIntPoint Directions[4] =
    {
        FIntPoint(1, 0),
        FIntPoint(-1, 0),
        FIntPoint(0, 1),
        FIntPoint(0, -1)
    };

    for (const FIntPoint& Direction : Directions)
    {
        const FIntPoint Candidate = Position + Direction;
        if (
            Candidate.X >= 0 && Candidate.X < GridWidth &&
            Candidate.Y >= 0 && Candidate.Y < GridHeight
            )
        {
            Neighbors.Add(Candidate);
        }
    }

    return Neighbors;
}

FIntPoint UNavigationContext::GetRandomWalkableCell(
    int32 SettlementID,
    int32 Seed
) const
{
    const TMap<FIntPoint, FNavCell>* G = SettlementGrids.Find(SettlementID);
    if (!G || G->Num() == 0)
    {
        return FIntPoint(0, 0);
    }

    TArray<FIntPoint> Walkable;
    Walkable.Reserve(G->Num());

    for (const auto& Pair : *G)
    {
        if (Pair.Value.bWalkable)
        {
            Walkable.Add(Pair.Key);
        }
    }

    if (Walkable.Num() == 0)
    {
        return FIntPoint(0, 0);
    }

    return Walkable[FMath::Abs(Seed) % Walkable.Num()];
}