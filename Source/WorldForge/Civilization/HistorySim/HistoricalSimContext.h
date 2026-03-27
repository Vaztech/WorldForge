#pragma once

#include "CoreMinimal.h"
#include "../Data/WorldForgeDataTypes.h"
#include "HistoricalSimTypes.h"

class UWorldForgeRNG;

struct FHistoricalSimContext
{
    FHistoricalSimConfig& Config;
    TArray<FCivilizationData>& Civs;
    TArray<FWorldTileData>& Tiles;
    TArray<FCurrencyDefinition>& Currencies;
    TArray<FHistoryEventData>& OutHistory;
    UWorldForgeRNG* RNG;
    TArray<FHistoryEventData>& AllEvents;
    int32& NextEventID;

    FHistoricalSimContext(
        FHistoricalSimConfig& InConfig,
        TArray<FCivilizationData>& InCivs,
        TArray<FWorldTileData>& InTiles,
        TArray<FCurrencyDefinition>& InCurrencies,
        TArray<FHistoryEventData>& InOutHistory,
        UWorldForgeRNG* InRNG,
        TArray<FHistoryEventData>& InAllEvents,
        int32& InNextEventID)
        : Config(InConfig)
        , Civs(InCivs)
        , Tiles(InTiles)
        , Currencies(InCurrencies)
        , OutHistory(InOutHistory)
        , RNG(InRNG)
        , AllEvents(InAllEvents)
        , NextEventID(InNextEventID)
    {
    }
};