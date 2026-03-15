#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "HistoricalSimulation.generated.h"

class UWorldForgeRNG;

USTRUCT(BlueprintType)
struct FHistoricalSimConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TotalYears = 200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float WarFrequency = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DisasterFrequency = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float EconomicComplexity = 0.5f;
};

UCLASS()
class WORLDFORGE_API UHistoricalSimulation : public UObject
{
    GENERATED_BODY()

public:
    void RunSimulation(
        FHistoricalSimConfig Config,
        TArray<FCivilizationData>& Civs,
        TArray<FWorldTileData>& Tiles,
        TArray<FCurrencyDefinition>& Currencies,
        TArray<FHistoryEventData>& OutHistory,
        UWorldForgeRNG* RNG
    );

    const TArray<FHistoryEventData>& GetAllEvents() const { return AllEvents; }

private:
    void SimulateEpoch_Emergence(int32 YS, int32 YE, TArray<FCivilizationData>& Civs, TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);
    void SimulateEpoch_Expansion(int32 YS, int32 YE, TArray<FCivilizationData>& Civs, TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);
    void SimulateEpoch_Conflict(int32 YS, int32 YE, TArray<FCivilizationData>& Civs, TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);
    void SimulateEpoch_Stabilization(int32 YS, int32 YE, TArray<FCivilizationData>& Civs, TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);

    void GenerateWarEvent(int32 Year, TArray<FCivilizationData>& Civs, TArray<FCurrencyDefinition>& C, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);
    void GenerateTradeEvent(int32 Year, TArray<FCivilizationData>& Civs, TArray<FCurrencyDefinition>& C, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);
    void GenerateDisasterEvent(int32 Year, TArray<FCivilizationData>& Civs, TArray<FWorldTileData>& Tiles, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);
    void GenerateCollapseEvent(int32 Year, TArray<FCivilizationData>& Civs, TArray<FCurrencyDefinition>& C, TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG);

    void RecordEvent(int32 Year, FGameplayTag EpochTag, FGameplayTag EventType, const TArray<int32>& PIDs, const FString& Result, const FString& Cause, float EconImpact, TArray<FHistoryEventData>& OutHistory);

    TArray<FHistoryEventData> AllEvents;
    int32 NextEventID = 0;
};