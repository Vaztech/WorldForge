#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeSaveGame.generated.h"

UCLASS()
class WORLDFORGE_API UWorldForgeSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) TArray<FWorldTileData>         WorldTiles;
    UPROPERTY(SaveGame) TArray<FCivilizationData>      Civilizations;
    UPROPERTY(SaveGame) TArray<FHistoryEventData>      HistoryLog;
    UPROPERTY(SaveGame) int64                          WorldSeed = 0;
    UPROPERTY(SaveGame) int32                          GridWidth = 256;
    UPROPERTY(SaveGame) int32                          GridHeight = 256;
    UPROPERTY(SaveGame) TArray<FCurrencyDefinition>    AllCurrencies;
    UPROPERTY(SaveGame) TArray<FGovernmentEconomicProfile> AllGovernments;
    UPROPERTY(SaveGame) TArray<FSettlementState>       Settlements;
    UPROPERTY(SaveGame) TArray<FAgentData>             AllAgents;
    UPROPERTY(SaveGame) TArray<FReservationRecord>     ActiveReservations;
    UPROPERTY(SaveGame) TArray<FActiveBuildTask>       ActiveBuildTasks;
    UPROPERTY(SaveGame) int32 SimStepNumber = 0;
    UPROPERTY(SaveGame) int32 SimCurrentDay = 1;
    UPROPERTY(SaveGame) int32 SimCurrentYear = 1;
    UPROPERTY(SaveGame) FString SlotName;
    UPROPERTY(SaveGame) FString SaveTimestamp;
    UPROPERTY(SaveGame) int32   SaveVersion = 1;
};