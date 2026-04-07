#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "NavigationContext.generated.h"

UCLASS(BlueprintType)
class WORLDFORGE_API UNavigationContext : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    void InitializeGrid(int32 SettlementID, int32 GridWidth, int32 GridHeight);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    TArray<FIntPoint> FindPath(int32 SettlementID, FIntPoint Start, FIntPoint Goal) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    void SetCellWalkable(int32 SettlementID, FIntPoint Position, bool bWalkable);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    bool IsCellWalkable(int32 SettlementID, FIntPoint Position) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    void SetCellOccupant(int32 SettlementID, FIntPoint Position, int32 AgentID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    FIntPoint GetRandomWalkableCell(int32 SettlementID, int32 Seed) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    FVector GridToWorld(FIntPoint Position, float CellSize = 200.f) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    FIntPoint WorldToGrid(FVector Position, float CellSize = 200.f) const;

    // NEW:
    // Converts a settlement-local nav cell into a world terrain cell using the
    // settlement's stored world origin.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Navigation")
    FIntPoint NavToWorld(int32 SettlementID, FIntPoint NavCell) const;

    const TMap<FIntPoint, FNavCell>& GetGrid(int32 SettlementID) const;

private:
    float Heuristic(FIntPoint A, FIntPoint B) const;
    TArray<FIntPoint> GetNeighbors(FIntPoint Position, int32 GridWidth, int32 GridHeight) const;

    TMap<int32, TMap<FIntPoint, FNavCell>> SettlementGrids;
    TMap<int32, FIntPoint> GridDimensions;

    static const TMap<FIntPoint, FNavCell> EmptyGrid;
};