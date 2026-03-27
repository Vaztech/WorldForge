#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "HistorySim/HistoricalSimTypes.h"
#include "HistoricalSimulation.generated.h"

class UWorldForgeRNG;
class FHistPhaseEpochPass;
class FHistEventGenerationPass;
class FHistExpansionPass;
class FHistStabilityPass;
class FHistConflictPass;
struct FHistoricalSimContext;

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
    friend class FHistPhaseEpochPass;
    friend class FHistEventGenerationPass;
    friend class FHistExpansionPass;
    friend class FHistStabilityPass;
    friend class FHistConflictPass;

    void RecordEvent(
        int32 Year,
        FGameplayTag EpochTag,
        FGameplayTag EventType,
        const TArray<int32>& PIDs,
        const FString& Result,
        const FString& Cause,
        float EconImpact,
        FHistoricalSimContext& Context);

    TArray<FHistoryEventData> AllEvents;
    int32 NextEventID = 0;
};