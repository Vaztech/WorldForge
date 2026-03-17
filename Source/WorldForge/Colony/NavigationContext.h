#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "NavigationContext.generated.h"

// FNavCell is defined in WorldForgeDataTypes.h -- do NOT redeclare here

UCLASS(BlueprintType)
class WORLDFORGE_API UNavigationContext : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    void InitializeGrid(int32 SettlementID, int32 GridWidth, int32 GridHeight);

    /**
     * Returns path from Start to Goal inclusive of both ends.
     * Start == Goal: returns {Goal} (one node -- already there).
     * No path found: returns empty array.
     * Path[0] = Start, Path.Last() = Goal.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    TArray<FIntPoint> FindPath(int32 SettlementID, FIntPoint Start, FIntPoint Goal) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    void SetCellWalkable(int32 SettlementID, FIntPoint Position, bool bWalkable);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    bool IsCellWalkable(int32 SettlementID, FIntPoint Position) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    void SetCellOccupant(int32 SettlementID, FIntPoint Position, int32 AgentID);

    // Returns a deterministic walkable cell for the given settlement.
    // This is used by Phase 2 job creation so harvest/work jobs get a valid
    // TargetGridPos instead of defaulting to (0,0).
    // Seed is provided by the caller to keep selection deterministic.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    FIntPoint GetRandomWalkableCell(int32 SettlementID, int32 Seed) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    FVector GridToWorld(FIntPoint Position, float CellSize = 200.f) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    FIntPoint WorldToGrid(FVector Position, float CellSize = 200.f) const;

    // Read-only access to a settlement grid.
    // Returns EmptyGrid if the settlement has no initialized grid.
    const TMap<FIntPoint, FNavCell>& GetGrid(int32 SettlementID) const;

private:
    // Heuristic used by pathfinding.
    float Heuristic(FIntPoint A, FIntPoint B) const;

    // Returns valid neighboring cells inside grid bounds.
    TArray<FIntPoint> GetNeighbors(FIntPoint Position, int32 GridWidth, int32 GridHeight) const;

    // Per-settlement navigation grids.
    TMap<int32, TMap<FIntPoint, FNavCell>> SettlementGrids;

    // Cached grid dimensions per settlement.
    TMap<int32, FIntPoint> GridDimensions;

    // Safe empty grid returned when a settlement grid is missing.
    static const TMap<FIntPoint, FNavCell> EmptyGrid;
};