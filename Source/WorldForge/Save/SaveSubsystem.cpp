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

// Phase 3B additions
#include "OverworldSubsystem.h"              // ADDED
#include "DiplomacySubsystem.h"              // ADDED
#include "NavalSystem.h"                     // ADDED
#include "CosmicSimulationSubsystem.h"       // ADDED
#include "CivilizationResilienceSystem.h"    // ADDED

namespace
{
constexpr int32 FixedSaveSlotSummaryCount = 8;

FString MakeFixedSaveSlotName(const int32 SlotIndex)
{
    return FString::Printf(TEXT("WorldForge_Slot%d"), SlotIndex);
}

FString MakeSummaryTextOrDefault(const FString& Value, const TCHAR* Fallback)
{
    const FString TrimmedValue = Value.TrimStartAndEnd();
    return TrimmedValue.IsEmpty() ? FString(Fallback) : TrimmedValue;
}
}

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

    // Phase 3A: economy state
    if (UWorldEconomySimulation* Econ = W->GetSubsystem<UWorldEconomySimulation>())
    {
        S->EconomySaveState = Econ->GetEconomySaveState();
    }

    // Phase 3B: overworld state
    if (UOverworldSubsystem* Overworld = W->GetSubsystem<UOverworldSubsystem>())
    {
        S->OverworldSaveState = Overworld->GetOverworldSaveState(); // ADDED
    }

    // Phase 3B: diplomacy state
    if (UDiplomacySubsystem* Diplomacy = W->GetSubsystem<UDiplomacySubsystem>())
    {
        S->DiplomacySaveState = Diplomacy->GetDiplomacySaveState(); // ADDED
    }

    // Phase 3B: naval state
    if (UNavalSystem* Naval = W->GetSubsystem<UNavalSystem>())
    {
        S->NavalSaveState = Naval->GetNavalSaveState(); // ADDED
    }

    // Phase 3B: cosmic state
    if (UCosmicSimulationSubsystem* Cosmic = W->GetSubsystem<UCosmicSimulationSubsystem>())
    {
        S->CosmicSaveState = Cosmic->GetCosmicSaveState(); // ADDED
    }

    // Phase 3B: resilience state
    if (UCivilizationResilienceSystem* Resilience = W->GetSubsystem<UCivilizationResilienceSystem>())
    {
        S->ResilienceSaveState = Resilience->GetResilienceSaveState(); // ADDED
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

    // Phase 3A: economy state
    if (UWorldEconomySimulation* Econ = W->GetSubsystem<UWorldEconomySimulation>())
    {
        Econ->RestoreEconomyFromSave(S->EconomySaveState);
    }

    // Phase 3B: restore in strict dependency order

    // 1. Overworld
    if (UOverworldSubsystem* Overworld = W->GetSubsystem<UOverworldSubsystem>())
    {
        Overworld->RestoreOverworldFromSave(S->OverworldSaveState); // ADDED
    }

    // 2. Diplomacy
    if (UDiplomacySubsystem* Diplomacy = W->GetSubsystem<UDiplomacySubsystem>())
    {
        Diplomacy->RestoreDiplomacyFromSave(S->DiplomacySaveState); // ADDED
    }

    // 3. Naval
    if (UNavalSystem* Naval = W->GetSubsystem<UNavalSystem>())
    {
        Naval->RestoreNavalFromSave(S->NavalSaveState); // ADDED
    }

    // 4. Cosmic
    if (UCosmicSimulationSubsystem* Cosmic = W->GetSubsystem<UCosmicSimulationSubsystem>())
    {
        Cosmic->RestoreCosmicFromSave(S->CosmicSaveState); // ADDED
    }

    // 5. Resilience
    if (UCivilizationResilienceSystem* Resilience = W->GetSubsystem<UCivilizationResilienceSystem>())
    {
        Resilience->RestoreResilienceFromSave(S->ResilienceSaveState); // ADDED
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

bool USaveSubsystem::DeleteSaveSlot(const FString& SaveSlotName, FString& OutReason)
{
    OutReason.Reset();

    const FString TrimmedSaveSlotName = SaveSlotName.TrimStartAndEnd();
    if (TrimmedSaveSlotName.IsEmpty())
    {
        OutReason = TEXT("Save slot name is empty.");
        UE_LOG(LogTemp, Warning, TEXT("[SaveSubsystem] Delete failed. Reason='%s'"), *OutReason);
        return false;
    }

    if (!UGameplayStatics::DoesSaveGameExist(TrimmedSaveSlotName, 0))
    {
        OutReason = FString::Printf(TEXT("Save slot does not exist: %s"), *TrimmedSaveSlotName);
        UE_LOG(LogTemp, Warning, TEXT("[SaveSubsystem] Delete failed. SaveSlotName='%s' Reason='%s'"),
            *TrimmedSaveSlotName,
            *OutReason);
        return false;
    }

    if (!UGameplayStatics::DeleteGameInSlot(TrimmedSaveSlotName, 0))
    {
        OutReason = FString::Printf(TEXT("Failed to delete save slot: %s"), *TrimmedSaveSlotName);
        UE_LOG(LogTemp, Error, TEXT("[SaveSubsystem] Delete failed. SaveSlotName='%s' Reason='%s'"),
            *TrimmedSaveSlotName,
            *OutReason);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[SaveSubsystem] Deleted save slot '%s'"), *TrimmedSaveSlotName);
    return true;
}

TArray<FString> USaveSubsystem::GetSaveSlots() const
{
    TArray<FString> Slots;
    Slots.Reserve(FixedSaveSlotSummaryCount);

    for (int32 SlotIndex = 1; SlotIndex <= FixedSaveSlotSummaryCount; ++SlotIndex)
    {
        Slots.Add(MakeFixedSaveSlotName(SlotIndex));
    }

    return Slots;
}

TArray<FWorldForgeSaveSlotSummary> USaveSubsystem::GetFixedSaveSlotSummaries(int32 MaxSlots) const
{
    MaxSlots = FixedSaveSlotSummaryCount;

    TArray<FWorldForgeSaveSlotSummary> Summaries;
    Summaries.Reserve(MaxSlots);

    int32 ExistingSaveCount = 0;

    for (int32 SlotIndex = 1; SlotIndex <= MaxSlots; ++SlotIndex)
    {
        const FString SlotName = MakeFixedSaveSlotName(SlotIndex);

        FWorldForgeSaveSlotSummary Summary;
        Summary.SlotIndex = SlotIndex;
        Summary.SaveSlotName = SlotName;
        Summary.WorldName = TEXT("Empty Slot");
        Summary.ColonyName.Reset();
        Summary.LastPlayedText.Reset();
        Summary.DayYearText.Reset();
        Summary.SaveVersionText.Reset();
        Summary.bHasSave = UGameplayStatics::DoesSaveGameExist(SlotName, 0);

        if (Summary.bHasSave)
        {
            ++ExistingSaveCount;

            Summary.WorldName = TEXT("Unknown World");
            Summary.ColonyName = TEXT("Unknown Colony");
            Summary.LastPlayedText = TEXT("Unknown");
            Summary.DayYearText = TEXT("Day ?");
            Summary.SaveVersionText = TEXT("Unknown Version");

            if (USaveGame* LoadedSave = UGameplayStatics::LoadGameFromSlot(SlotName, 0))
            {
                if (const UWorldForgeSaveGame* WorldForgeSave = Cast<UWorldForgeSaveGame>(LoadedSave))
                {
                    Summary.WorldName = MakeSummaryTextOrDefault(WorldForgeSave->WorldName, TEXT("Unknown World"));
                    Summary.ColonyName = MakeSummaryTextOrDefault(WorldForgeSave->ColonyName, TEXT("Unknown Colony"));
                    Summary.LastPlayedText = MakeSummaryTextOrDefault(WorldForgeSave->SaveTimestamp, TEXT("Unknown"));
                    Summary.DayYearText = FString::Printf(
                        TEXT("Day %d, Year %d"),
                        WorldForgeSave->SimCurrentDay,
                        WorldForgeSave->SimCurrentYear);
                    Summary.SaveVersionText = FString::Printf(TEXT("Version %d"), WorldForgeSave->SaveVersion);
                }
            }
        }

        Summaries.Add(Summary);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SaveSubsystem] Built save slot summary list. Slots=%d Existing=%d"),
        Summaries.Num(),
        ExistingSaveCount);

    return Summaries;
}
