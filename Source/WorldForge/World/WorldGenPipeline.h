#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldGenPipeline.generated.h"

// NOTE: EWorldSize and FWorldGenConfig are defined in WorldForgeDataTypes.h
// Do NOT redefine them here.

class UWorld;
class UWorldForgeRNG;

UCLASS(BlueprintType)
class WORLDFORGE_API UWorldGenPipeline : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Main entry point.
     * RNG is passed explicitly.
     * World is resolved from the object's Outer so the function is Blueprint-friendly.
     * Create this object with NewObject<UWorldGenPipeline>(Owner) where Owner is an Actor or Subsystem.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldGen")
    void GenerateWorld(
        int64           WorldSeed,
        FWorldGenConfig Config,
        UWorldForgeRNG* RNG
    );

    // Results -- read after GenerateWorld() completes
    const TArray<FWorldTileData>& GetWorldTiles() const { return WorldTiles; }
    const TArray<FCivilizationData>& GetCivilizations() const { return Civilizations; }
    const TArray<FHistoryEventData>& GetHistoryLog() const { return HistoryLog; }
    const TArray<FCurrencyDefinition>& GetCurrencies() const { return Currencies; }
    const TArray<FGameplayTag>& GetWorldTraits() const { return WorldTraits; }

    int32 GetGridWidth() const { return GridWidth; }
    int32 GetGridHeight() const { return GridHeight; }
    bool  IsComplete() const { return bComplete; }

private:
    void Step01_InitializeSeed();
    void Step02_GenerateHeightmap();
    void Step03_RunTectonics();
    void Step04_AssignBiomes();
    void Step05_InitVoxelChunks();
    void Step06_PCGDecorationPass();
    void Step07_ApplyWorldDriver();
    void Step08_SeedResources();
    void Step09_PlaceCivilizations();
    void Step10_GenerateCulturalDNA();
    void Step11_GenerateEconomyLayer();
    void Step12_GenerateCosmicRoster();
    void Step13_RunPreHistory();
    void Step14_AssignWorldTraits();
    void Step15_RecordFinalState();
    void Step16_FinalizeWorldMap();

    int32 GetDimension(EWorldSize Size) const;
    int32 TileIndex(int32 X, int32 Y) const { return Y * GridWidth + X; }

    TArray<FWorldTileData>      WorldTiles;
    TArray<FCivilizationData>   Civilizations;
    TArray<FHistoryEventData>   HistoryLog;
    TArray<FCurrencyDefinition> Currencies;
    TArray<FGameplayTag>        WorldTraits;

    FWorldGenConfig CurrentConfig;
    int64           WorldSeed = 0;
    int32           GridWidth = 256;
    int32           GridHeight = 256;
    bool            bComplete = false;

    // Stored explicitly -- no GetWorld() calls inside UObject
    UPROPERTY(Transient)
    UWorldForgeRNG* RNGRef = nullptr;

    UPROPERTY(Transient)
    UWorld* WorldRef = nullptr;
};