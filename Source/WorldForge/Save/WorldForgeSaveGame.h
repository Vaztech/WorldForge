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
    // ====================================================================
    // WORLD STATE
    // ====================================================================
    UPROPERTY(SaveGame) TArray<FWorldTileData>         WorldTiles;
    UPROPERTY(SaveGame) TArray<FCivilizationData>      Civilizations;
    UPROPERTY(SaveGame) TArray<FHistoryEventData>      HistoryLog;

    // Phase 3.5: world history artifact storage
    UPROPERTY(SaveGame) TArray<FLegendaryArtifact>     Artifacts;
    UPROPERTY(SaveGame) TArray<FArtifactOwnershipRecord> ArtifactHistory;

    UPROPERTY(SaveGame) int64 WorldSeed = 0;
    UPROPERTY(SaveGame) int32 GridWidth = 256;
    UPROPERTY(SaveGame) int32 GridHeight = 256;

    // ====================================================================
    // ECONOMY
    // ====================================================================
    UPROPERTY(SaveGame) TArray<FCurrencyDefinition>          AllCurrencies;
    UPROPERTY(SaveGame) TArray<FGovernmentEconomicProfile>   AllGovernments;

    UPROPERTY(SaveGame) FEconomySaveState EconomySaveState;

    // ====================================================================
    // COLONY STATE
    // ====================================================================
    UPROPERTY(SaveGame) TArray<FSettlementState>       Settlements;
    UPROPERTY(SaveGame) TArray<FAgentData>             AllAgents;

    // ====================================================================
    // SYSTEM STATE
    // ====================================================================
    UPROPERTY(SaveGame) TArray<FReservationRecord>     ActiveReservations;

    // Active construction tasks (voxel/build system)
    UPROPERTY(SaveGame) TArray<FActiveBuildTask>       ActiveBuildTasks;

    // --------------------------------------------------------------------
    // Phase 2 additions
    // --------------------------------------------------------------------

    // Saved job queue state (JobProcessor / FJobQueueManager)
    UPROPERTY(SaveGame) TArray<FJobRequest>           ActiveJobRequests;

    // Saved production tasks (ProductionProcessor)
    UPROPERTY(SaveGame) TArray<FActiveProductionTask> ActiveProductionTasks;

    // ====================================================================
    // PHASE 3B SYSTEM SAVE STATE
    // ====================================================================
    UPROPERTY(SaveGame) FOverworldSaveState   OverworldSaveState;    // ADDED
    UPROPERTY(SaveGame) FDiplomacySaveState   DiplomacySaveState;    // ADDED
    UPROPERTY(SaveGame) FNavalSaveState       NavalSaveState;        // ADDED
    UPROPERTY(SaveGame) FCosmicSaveState      CosmicSaveState;       // ADDED
    UPROPERTY(SaveGame) FResilienceSaveState  ResilienceSaveState;   // ADDED

    // ====================================================================
    // SIMULATION TIME
    // ====================================================================
    UPROPERTY(SaveGame) int32 SimStepNumber = 0;
    UPROPERTY(SaveGame) int32 SimCurrentDay = 1;
    UPROPERTY(SaveGame) int32 SimCurrentYear = 1;

    // ====================================================================
    // SAVE METADATA
    // ====================================================================
    UPROPERTY(SaveGame) FString SlotName;
    UPROPERTY(SaveGame) FString SaveTimestamp;
    UPROPERTY(SaveGame) int32   SaveVersion = 1;
};