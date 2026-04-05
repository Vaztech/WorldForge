#include "Visual/VoxelSpawnSubsystem.h"

#include "Visual/DA_VoxelVisualRuleSet.h"
#include "Visual/SettlementVisualManager.h"
#include "SettlementManager.h"
#include "Engine/World.h"

namespace
{
    static const TCHAR* DefaultRuleSetPath =
        TEXT("/Game/WorldForge/Visual/RuleSets/DA_VisualRules_Default.DA_VisualRules_Default");
}

void UVoxelSpawnSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    SettlementManager = GetWorld() ? GetWorld()->GetSubsystem<USettlementManager>() : nullptr;
    DefaultRuleSet = TryLoadDefaultRuleSet();

    if (DefaultRuleSet)
    {
        UE_LOG(LogTemp, Log, TEXT("[VSS] Loaded default visual rule set."));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[VSS] Default visual rule set not found at %s"), DefaultRuleSetPath);
    }

    UE_LOG(LogTemp, Log, TEXT("[VSS] Initialized"));
}

void UVoxelSpawnSubsystem::Deinitialize()
{
    SpawnRecords.Reset();
    SettlementManagers.Reset();
    DefaultRuleSet = nullptr;
    SettlementManager = nullptr;

    Super::Deinitialize();
}

void UVoxelSpawnSubsystem::OnSimStep(int32 StepNumber)
{
    if (!SettlementManager)
    {
        return;
    }

    const TArray<int32> SettlementIDs = SettlementManager->GetAllSettlementIDs();
    for (const int32 SettlementID : SettlementIDs)
    {
        if (!SettlementManager->IsVisualDirty(SettlementID))
        {
            continue;
        }

        RebuildSettlementVisuals(SettlementID);
        SettlementManager->ClearVisualDirty(SettlementID);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VSS] Settlement %d rebuilt on dirty flag flush at step %d"),
            SettlementID,
            StepNumber
        );
    }
}

void UVoxelSpawnSubsystem::ForceRebuildSettlement(int32 SettlementID)
{
    if (!SettlementManager || SettlementID < 0)
    {
        return;
    }

    SettlementManager->SetVisualDirty(SettlementID);
    RebuildSettlementVisuals(SettlementID);
    SettlementManager->ClearVisualDirty(SettlementID);

    UE_LOG(LogTemp, Log, TEXT("[VSS] Force rebuild settlement %d"), SettlementID);
}

void UVoxelSpawnSubsystem::SetDefaultRuleSet(UDA_VoxelVisualRuleSet* InRuleSet)
{
    DefaultRuleSet = InRuleSet;

    for (TPair<int32, TObjectPtr<USettlementVisualManager>>& Pair : SettlementManagers)
    {
        if (Pair.Value)
        {
            Pair.Value->SetRuleSet(DefaultRuleSet);
        }
    }
}

FVSSSaveState UVoxelSpawnSubsystem::GetVSSSaveState() const
{
    FVSSSaveState Out;
    Out.SpawnRecords = SpawnRecords;
    return Out;
}

void UVoxelSpawnSubsystem::RestoreVSSFromSave(const FVSSSaveState& SaveState)
{
    SpawnRecords = SaveState.SpawnRecords;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VSS] RestoreVSSFromSave loaded %d spawn records"),
        SpawnRecords.Num()
    );
}

void UVoxelSpawnSubsystem::RebuildSettlementVisuals(int32 SettlementID)
{
    if (!SettlementManager)
    {
        return;
    }

    const FSettlementState* State = SettlementManager->GetSettlement(SettlementID);
    if (!State)
    {
        return;
    }

    USettlementVisualManager* Manager = GetOrCreateManager(SettlementID);
    if (!Manager)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[VSS] Failed to create SettlementVisualManager for settlement %d"),
            SettlementID);
        return;
    }

    Manager->RebuildBuildingVisuals(*State, SpawnRecords);
}

USettlementVisualManager* UVoxelSpawnSubsystem::GetOrCreateManager(int32 SettlementID)
{
    if (TObjectPtr<USettlementVisualManager>* Found = SettlementManagers.Find(SettlementID))
    {
        return Found->Get();
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    USettlementVisualManager* Manager = NewObject<USettlementVisualManager>(this);
    if (!Manager)
    {
        return nullptr;
    }

    Manager->Initialize(World, SettlementID, DefaultRuleSet);
    SettlementManagers.Add(SettlementID, Manager);
    return Manager;
}

UDA_VoxelVisualRuleSet* UVoxelSpawnSubsystem::TryLoadDefaultRuleSet()
{
    return LoadObject<UDA_VoxelVisualRuleSet>(nullptr, DefaultRuleSetPath);
}