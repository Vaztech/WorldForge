#include "SaveSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "SettlementManager.h"
#include "ProcessorManager.h"
#include "SimulationTimeManager.h"
#include "ReservationSystem.h"
#include "VoxelConstructionSystem.h"
#include "WorldMapSubsystem.h"
#include "WorldEconomySimulation.h"

// Phase 2 additions
#include "JobProcessor.h"
#include "ProductionProcessor.h"

void USaveSubsystem::Initialize(FSubsystemCollectionBase& C)
{
    Super::Initialize(C);
    UE_LOG(LogTemp, Log, TEXT("[SaveSubsystem] Initialized."));
}

void USaveSubsystem::SaveGame(const FString& Slot, UObject* WorldContextObject)
{
    if (bSaveInProgress)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SaveSubsystem] Save in progress!"));
        return;
    }

    // Resolve world from the supplied context object if possible.
    UWorld* W = nullptr;
    if (GEngine && WorldContextObject)
    {
        W = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
    }

    // Fallback: if Blueprint left the world-context pin unconnected,
    // use the GameInstance's active world.
    if (!W && GetGameInstance())
    {
        W = GetGameInstance()->GetWorld();
    }

    if (!W)
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSubsystem] World null!"));
        OnSaveComplete.Broadcast(false);
        return;
    }

    bSaveInProgress = true;

    UWorldForgeSaveGame* Data = CollectWorldState(W, Slot);
    if (!Data)
    {
        bSaveInProgress = false;
        UE_LOG(LogTemp, Error, TEXT("[SaveSubsystem] Failed to collect world state for slot %s"), *Slot);
        OnSaveComplete.Broadcast(false);
        return;
    }

    FAsyncSaveGameToSlotDelegate D;
    D.BindUObject(this, &USaveSubsystem::OnSaveCompleted);

    UGameplayStatics::AsyncSaveGameToSlot(Data, Slot, 0, D);

    UE_LOG(LogTemp, Log, TEXT("[SaveSubsystem] Async save: %s"), *Slot);
}

void USaveSubsystem::LoadGame(const FString& Slot, UObject* WorldContextObject)
{
    // Resolve world from the supplied context object if possible.
    UWorld* W = nullptr;
    if (GEngine && WorldContextObject)
    {
        W = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
    }

    // Fallback: if Blueprint left the world-context pin unconnected,
    // use the GameInstance's active world.
    if (!W && GetGameInstance())
    {
        W = GetGameInstance()->GetWorld();
    }

    if (!W)
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSubsystem] World null on load!"));
        OnLoadComplete.Broadcast(false);
        return;
    }

    if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
    {
        UE_LOG(LogTemp, Warning, TEXT("[SaveSubsystem] Save slot does not exist: %s"), *Slot);
        OnLoadComplete.Broadcast(false);
        return;
    }

    bLoadInProgress = true;

    UWorldForgeSaveGame* Data =
        Cast<UWorldForgeSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));

    if (!Data)
    {
        bLoadInProgress = false;
        UE_LOG(LogTemp, Error, TEXT("[SaveSubsystem] Failed to load save object from slot: %s"), *Slot);
        OnLoadComplete.Broadcast(false);
        return;
    }

    DistributeLoadedState(Data, W);

    bLoadInProgress = false;
    OnLoadComplete.Broadcast(true);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SaveSubsystem] Loaded: %s Step=%d"),
        *Slot,
        Data->SimStepNumber);
}

UWorldForgeSaveGame* USaveSubsystem::CollectWorldState(UWorld* W, const FString& Slot)
{
    if (!W)
    {
        return nullptr;
    }

    UWorldForgeSaveGame* S =
        Cast<UWorldForgeSaveGame>(UGameplayStatics::CreateSaveGameObject(UWorldForgeSaveGame::StaticClass()));

    if (!S)
    {
        return nullptr;
    }

    S->SlotName = Slot;
    S->SaveTimestamp = FDateTime::Now().ToString();

    if (UWorldMapSubsystem* WM = W->GetSubsystem<UWorldMapSubsystem>())
    {
        S->WorldTiles = WM->GetWorldTiles();
        S->Civilizations = WM->GetCivilizations();
        S->HistoryLog = WM->GetHistoryLog();
        S->GridWidth = WM->GetGridWidth();
        S->GridHeight = WM->GetGridHeight();
    }

    if (USettlementManager* SM = W->GetSubsystem<USettlementManager>())
    {
        S->Settlements = SM->GetAllSettlements();
    }

    if (UProcessorManager* PM = W->GetSubsystem<UProcessorManager>())
    {
        S->AllAgents = PM->GetAllAgents();

        // =========================
        // Phase 2: Save Job Queue
        // =========================
        if (UJobProcessor* JP = PM->GetTypedProcessor<UJobProcessor>())
        {
            S->ActiveJobRequests = JP->GetJobsForSave();
        }

        // =========================
        // Phase 2: Save Production Tasks
        // =========================
        if (UProductionProcessor* PP = PM->GetTypedProcessor<UProductionProcessor>())
        {
            S->ActiveProductionTasks = PP->GetAllTasksForSave();
        }
    }

    if (UReservationSystem* RS = W->GetSubsystem<UReservationSystem>())
    {
        S->ActiveReservations = RS->GetAllReservations();
    }

    if (UVoxelConstructionSystem* VC = W->GetSubsystem<UVoxelConstructionSystem>())
    {
        S->ActiveBuildTasks = VC->GetAllTasks();
    }

    if (USimulationTimeManager* ST = W->GetSubsystem<USimulationTimeManager>())
    {
        S->SimStepNumber = ST->GetTotalStepCount();
        S->SimCurrentDay = ST->GetCurrentDay();
        S->SimCurrentYear = ST->GetCurrentYear();
    }

    if (UWorldEconomySimulation* EC = W->GetSubsystem<UWorldEconomySimulation>())
    {
        S->AllCurrencies = EC->GetAllCurrencies();
        S->AllGovernments = EC->GetAllGovernments();
    }

    return S;
}

void USaveSubsystem::DistributeLoadedState(UWorldForgeSaveGame* S, UWorld* W)
{
    if (!S || !W)
    {
        return;
    }

    if (USettlementManager* SM = W->GetSubsystem<USettlementManager>())
    {
        SM->RestoreFromSave(S->Settlements);
    }

    if (UProcessorManager* PM = W->GetSubsystem<UProcessorManager>())
    {
        PM->RestoreAgents(S->AllAgents);

        // =========================
        // Phase 2: Restore Job Queue
        // =========================
        if (UJobProcessor* JP = PM->GetTypedProcessor<UJobProcessor>())
        {
            JP->RestoreJobQueue(S->ActiveJobRequests);
        }

        // =========================
        // Phase 2: Restore Production Tasks
        // =========================
        if (UProductionProcessor* PP = PM->GetTypedProcessor<UProductionProcessor>())
        {
            PP->RestoreFromSave(S->ActiveProductionTasks);
        }
    }

    if (UReservationSystem* RS = W->GetSubsystem<UReservationSystem>())
    {
        RS->RestoreFromSave(S->ActiveReservations);
    }

    if (UVoxelConstructionSystem* VC = W->GetSubsystem<UVoxelConstructionSystem>())
    {
        VC->RestoreFromSave(S->ActiveBuildTasks);
    }

    if (USimulationTimeManager* ST = W->GetSubsystem<USimulationTimeManager>())
    {
        ST->RestoreStepCount(
            S->SimStepNumber,
            S->SimCurrentDay,
            S->SimCurrentYear);
    }
}

void USaveSubsystem::OnSaveCompleted(const FString& Slot, const int32, bool bOK)
{
    bSaveInProgress = false;

    if (bOK)
    {
        UE_LOG(LogTemp, Log, TEXT("[SaveSubsystem] Saved: %s"), *Slot);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSubsystem] FAILED: %s"), *Slot);
    }

    OnSaveComplete.Broadcast(bOK);
}

bool USaveSubsystem::SaveSlotExists(const FString& S) const
{
    return UGameplayStatics::DoesSaveGameExist(S, 0);
}

void USaveSubsystem::DeleteSaveSlot(const FString& S)
{
    UGameplayStatics::DeleteGameInSlot(S, 0);
}

TArray<FString> USaveSubsystem::GetSaveSlots() const
{
    return { TEXT("WorldForge_AutoSave"), TEXT("WorldForge_Slot1") };
}