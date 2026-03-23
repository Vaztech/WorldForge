#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldMapSubsystem.generated.h"

class UWorldGenPipeline;

// Inherits UWorldSubsystem for lifetime management
// Inherits FTickableGameObject for safe tick registration in UE5.7
UCLASS()
class WORLDFORGE_API UWorldMapSubsystem
    : public UWorldSubsystem
    , public FTickableGameObject
{
    GENERATED_BODY()

public:
    // UWorldSubsystem
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // FTickableGameObject
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return bInitialized; }
    virtual bool IsTickableInEditor() const override { return false; }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldMapSubsystem, STATGROUP_Tickables);
    }

    // Load from pipeline after GenerateWorld() completes
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void InitializeFromPipeline(UWorldGenPipeline* Pipeline);

    // Tile queries
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    bool GetTileData(int32 TileID, FWorldTileData& OutTile) const;
    const FWorldTileData* GetTileDataPtr(int32 TileID) const;
    FWorldTileData* GetMutableTileDataPtr(int32 TileID);

    // Civ queries
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    bool GetCivData(int32 CivID, FCivilizationData& OutCiv) const;

    // Fog of war
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void RevealTile(int32 TileID, EKnowledgeLevel NewLevel, float Accuracy = 1.0f);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    EKnowledgeLevel GetTileKnowledge(int32 TileID) const;

    int32 GetGridWidth() const { return GridWidth; }
    int32 GetGridHeight() const { return GridHeight; }
    int32 GetTileCount() const { return WorldTiles.Num(); }
    bool IsInitialized() const { return bInitialized; }

    // ---- Added accessors ----
    const TArray<FWorldTileData>& GetWorldTiles() const { return WorldTiles; }
    const TArray<FCivilizationData>& GetCivilizations() const { return Civilizations; }
    const TArray<FHistoryEventData>& GetHistoryLog() const { return HistoryLog; }

    TArray<FCivilizationData>& GetMutableCivilizations() { return Civilizations; } // ADDED

private:
    void TickLivingWorld();

    TArray<FWorldTileData> WorldTiles;
    TArray<FCivilizationData> Civilizations;
    TArray<FHistoryEventData> HistoryLog;

    int32 GridWidth = 256;
    int32 GridHeight = 256;
    bool bInitialized = false;
    float TickAccumulator = 0.0f;
    static constexpr float LiveWorldInterval = 30.0f;
};